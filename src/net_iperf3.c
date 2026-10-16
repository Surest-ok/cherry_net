/*
 * CherryNet - iperf3 Throughput Benchmark Engine Implementation
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#include "net_iperf3.h"
#include "cherry_vfs.h"
#include "cherry_printk.h"
#include "cherry_kal.h"
#include "arpa/inet.h"
#include <string.h>

#define IPERF3_COOKIE_LEN 37

static const char *s_iperf3_cookie = "cherryos-iperf3-cookie-0123456789abc";

static uint32_t parse_num(const char *s)
{
    uint32_t val = 0;
    if (!s) return 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val;
}

struct iperf3_srv_args {
    uint16_t port;
    volatile bool ready;
    volatile bool done;
    uint64_t total_bytes;
    uint64_t elapsed_us;
};

static int iperf3_server_worker(void *data)
{
    struct iperf3_srv_args *args = (struct iperf3_srv_args *)data;
    uint16_t port = args ? args->port : IPERF3_DEFAULT_PORT;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        pr_err("iperf3: server socket creation failed\n");
        if (args) args->done = true;
        return -1;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, (struct sockaddr *)&saddr, sizeof(saddr)) != 0) {
        pr_err("iperf3: server bind failed on port %d\n", port);
        close(listen_fd);
        if (args) args->done = true;
        return -1;
    }

    if (listen(listen_fd, 4) != 0) {
        pr_err("iperf3: server listen failed\n");
        close(listen_fd);
        if (args) args->done = true;
        return -1;
    }

    pr_info("iperf3: server listening on port %d\n", port);
    if (args) {
        args->ready = true;
    }

    char *buf = (char *)kmalloc(2048, GFP_KERNEL);
    if (!buf) {
        close(listen_fd);
        if (args) args->done = true;
        return -ENOMEM;
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int cfd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
    if (cfd >= 0) {
        /* Set 500ms receive timeout to avoid unbounded hangs */
        uint32_t rcv_timeout_ms = 500;
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &rcv_timeout_ms, sizeof(rcv_timeout_ms));

        /* Read cookie handshake */
        recv(cfd, buf, IPERF3_COOKIE_LEN, 0);
        send(cfd, "\x01", 1, 0);

        uint64_t start_ms = ktime_get_ms();
        size_t total_bytes = 0;

        for (;;) {
            ssize_t n = recv(cfd, buf, 2048, 0);
            if (n <= 0) break;
            total_bytes += (size_t)n;
        }

        uint64_t elapsed_ms = ktime_get_ms() - start_ms;
        if (elapsed_ms == 0) elapsed_ms = 1;

        uint64_t mbits = (total_bytes * 8ULL * 1000ULL) / (elapsed_ms * 1000000ULL);
        pr_info("iperf3: [Server] transferred %u bytes in %u ms (%llu Mbits/sec)\n",
                (unsigned int)total_bytes, (unsigned int)elapsed_ms, (unsigned long long)mbits);

        close(cfd);
    }

    kfree(buf);
    close(listen_fd);
    if (args) {
        args->done = true;
    }
    return 0;
}

int iperf3_server_start(uint16_t port)
{
    static struct iperf3_srv_args s_args;
    s_args.port = port;
    s_args.ready = false;
    s_args.done = false;

    struct task_struct *t = kthread_run(iperf3_server_worker, &s_args, "iperf3_srv");
    return t ? 0 : -1;
}

int iperf3_client_run(const char *server_ip, uint16_t port, uint32_t duration_sec, uint32_t buf_len)
{
    if (!server_ip) server_ip = "127.0.0.1";
    if (port == 0) port = IPERF3_DEFAULT_PORT;
    if (duration_sec == 0) duration_sec = 3;
    if (buf_len == 0 || buf_len > 4096) buf_len = 1024;

    uint32_t ip = inet_addr(server_ip);
    if (ip == (uint32_t)-1) {
        if (strcmp(server_ip, "localhost") == 0) {
            ip = IP4_ADDR(127, 0, 0, 1);
        } else {
            return -EHOSTUNREACH;
        }
    }

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) return sfd;

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = ip;

    if (connect(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) != 0) {
        pr_err("iperf3: connect to %s:%d failed\n", server_ip, port);
        close(sfd);
        return -ECONNREFUSED;
    }

    /* Send cookie */
    send(sfd, s_iperf3_cookie, strlen(s_iperf3_cookie), 0);

    /* Wait for ACK */
    char ack = 0;
    recv(sfd, &ack, 1, 0);

    char *send_buf = (char *)kmalloc(buf_len, GFP_KERNEL);
    if (!send_buf) {
        close(sfd);
        return -ENOMEM;
    }
    memset(send_buf, 0x5A, buf_len);

    uint64_t bytes_to_send = 64 * 1024;
    uint64_t sent = 0;
    uint64_t t0 = ktime_get_ns();
    int retries = 0;

    while (sent < bytes_to_send && retries < 1000) {
        size_t to_write = (bytes_to_send - sent > buf_len) ? buf_len : (size_t)(bytes_to_send - sent);
        ssize_t s = send(sfd, send_buf, to_write, 0);
        if (s <= 0) {
            msleep(1);
            retries++;
            continue;
        }
        sent += (size_t)s;
        retries = 0;
    }

    uint64_t t1 = ktime_get_ns();
    uint64_t elapsed_us = (t1 - t0) / 1000ULL;

    /* Drain pending queue buffers and signal EOF */
    msleep(2);
    shutdown(sfd, 2);
    msleep(2);
    close(sfd);

    uint64_t msec = elapsed_us / 1000ULL;
    if (msec == 0) msec = 1;
    uint64_t mbps = (sent * 8ULL) / (msec * 1000ULL);
    pr_info("iperf3: [Client] transferred %u bytes in %u ms (%llu Mbits/sec)\n",
            (unsigned int)sent, (unsigned int)msec, (unsigned long long)mbps);

    kfree(send_buf);
    return (sent >= bytes_to_send) ? 0 : -1;
}

int iperf3_run_self_test(void)
{
    pr_info("cherry_net: starting iperf3 multi-core throughput benchmark...\n");

    struct iperf3_srv_args s_args;
    s_args.port = 5201;
    s_args.ready = false;
    s_args.done = false;

    struct task_struct *srv_t = kthread_run(iperf3_server_worker, &s_args, "iperf3_srv");
    if (srv_t) {
        kthread_bind(srv_t, 1);
    }

    while (!s_args.ready) {
        msleep(2);
    }

    /* Client on Core 0 */
    int ret = iperf3_client_run("127.0.0.1", 5201, 1, 1024);

    int wait_limit = 500;
    while (!s_args.done && --wait_limit > 0) {
        msleep(2);
    }

    return ret;
}

int cmd_iperf3(int argc, char **argv)
{
    if (argc < 2) {
        pr_info("Usage: iperf3 -s [-p port] | iperf3 -c <server_ip> [-p port] [-t time]\n");
        return -1;
    }

    bool is_server = false;
    const char *server_ip = NULL;
    uint16_t port = IPERF3_DEFAULT_PORT;
    uint32_t duration = 3;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            is_server = true;
        } else if (strcmp(argv[i], "-c") == 0 && (i + 1 < argc)) {
            server_ip = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && (i + 1 < argc)) {
            port = (uint16_t)parse_num(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && (i + 1 < argc)) {
            duration = parse_num(argv[++i]);
        }
    }

    if (is_server) {
        return iperf3_server_start(port);
    } else if (server_ip) {
        return iperf3_client_run(server_ip, port, duration, 1024);
    }

    pr_info("Usage: iperf3 -s [-p port] | iperf3 -c <server_ip> [-p port] [-t time]\n");
    return -1;
}
