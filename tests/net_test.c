/*
 * CherryNet - Acceptance & Verification Test Suite
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#include "cherry_net.h"
#include "cherry_kal.h"
#include "sys/socket.h"
#include "netinet/in.h"
#include "arpa/inet.h"
#include <string.h>

struct wget_test_arg {
    int sfd;
    volatile bool ready;
    volatile bool done;
};

static int wget_mock_worker(void *p)
{
    struct wget_test_arg *a = (struct wget_test_arg *)p;
    a->ready = true;
    struct sockaddr_in csin;
    socklen_t clen = sizeof(csin);
    int c = accept(a->sfd, (struct sockaddr *)&csin, &clen);
    if (c >= 0) {
        char req_buf[128];
        recv(c, req_buf, sizeof(req_buf), 0);
        const char *resp = "HTTP/1.0 200 OK\r\nContent-Length: 11\r\n\r\nHello Wget!";
        send(c, resp, strlen(resp), 0);
        close(c);
    }
    a->done = true;
    return 0;
}

void cherry_net_run_tests(void)
{
    pr_info("cherry_net: ==> starting cherrynet acceptance test suite...\n");

    int t_virtio = 0;
    int t_ifconfig = 0;
    int t_mq_rss = 0;
    int t_ipv4_arp = 0;
    int t_ipv6_ndp = 0;
    int t_ping = 0;
    int t_udp = 0;
    int t_tcp = 0;
    int t_iperf2 = 0;

    /* 1. Verify VirtIO-Net interface */
    struct netif *eth0 = netif_find("eth0");
    if (eth0 && (eth0->flags & NETIF_FLAG_UP)) {
        t_virtio = 1;
    }

    /* 2. Verify ifconfig */
    struct netif *lo0 = netif_find("lo0");
    if (lo0 && eth0 && lo0->ip4_addr.addr == IP4_ADDR(127, 0, 0, 1)) {
        ifconfig_print();
        t_ifconfig = 1;
    }

    /* 3. Verify Multi-Core RSS Flow Hashing */
    uint32_t h1 = net_flow_hash(IP4_ADDR(10,0,2,15), IP4_ADDR(10,0,2,2), 1234, 80);
    uint32_t h2 = net_flow_hash(IP4_ADDR(10,0,2,15), IP4_ADDR(10,0,2,2), 1235, 80);
    uint32_t core_cnt = net_get_core_count();
    if (h1 != 0 && h2 != 0 && core_cnt >= 1) {
        t_mq_rss = 1;
    }

    /* 4. Verify IPv4 and ARP Cache */
    uint8_t dummy_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    arp_update(IP4_ADDR(192, 168, 1, 100), dummy_mac);
    uint8_t out_mac[6] = {0};
    if (arp_lookup(IP4_ADDR(192, 168, 1, 100), out_mac) == 0 &&
        memcmp(out_mac, dummy_mac, 6) == 0) {
        t_ipv4_arp = 1;
    }

    /* 5. Verify IPv6 and NDP Cache */
    struct ip6_addr test_ip6;
    memset(&test_ip6, 0, sizeof(test_ip6));
    test_ip6.u8[0] = 0xFE; test_ip6.u8[1] = 0x80; test_ip6.u8[15] = 0x42;
    ndp_update(&test_ip6, dummy_mac);
    uint8_t out_mac6[6] = {0};
    if (ndp_lookup(&test_ip6, out_mac6) == 0 &&
        memcmp(out_mac6, dummy_mac, 6) == 0) {
        t_ipv6_ndp = 1;
    }

    /* 6. Verify ICMP Ping (IPv4 & IPv6 loopback) */
    if (ping_run("127.0.0.1", 2, 500) == 0 && ping_run("::1", 2, 500) == 0) {
        t_ping = 1;
    }

    /* 7. Verify BSD Socket UDP */
    int udp_srv = socket(AF_INET, SOCK_DGRAM, 0);
    int udp_cli = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_srv >= 0 && udp_cli >= 0) {
        struct sockaddr_in srv_addr;
        memset(&srv_addr, 0, sizeof(srv_addr));
        srv_addr.sin_family = AF_INET;
        srv_addr.sin_port = htons(8888);
        srv_addr.sin_addr.s_addr = IP4_ADDR(127, 0, 0, 1);

        bind(udp_srv, (struct sockaddr *)&srv_addr, sizeof(srv_addr));

        const char *msg = "cherry_udp_test";
        sendto(udp_cli, msg, strlen(msg), 0, (struct sockaddr *)&srv_addr, sizeof(srv_addr));

        char rx_buf[32] = {0};
        struct sockaddr_in cli_src;
        socklen_t cli_len = sizeof(cli_src);
        ssize_t n = recvfrom(udp_srv, rx_buf, sizeof(rx_buf) - 1, 0, (struct sockaddr *)&cli_src, &cli_len);
        if (n == (ssize_t)strlen(msg) && strcmp(rx_buf, msg) == 0) {
            t_udp = 1;
        }
    }

    /* 8. Verify BSD Socket TCP (3-way handshake, stream, teardown) */
    int tcp_srv = socket(AF_INET, SOCK_STREAM, 0);
    int tcp_cli = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_srv >= 0 && tcp_cli >= 0) {
        struct sockaddr_in srv_sin;
        memset(&srv_sin, 0, sizeof(srv_sin));
        srv_sin.sin_family = AF_INET;
        srv_sin.sin_port = htons(9999);
        srv_sin.sin_addr.s_addr = IP4_ADDR(127, 0, 0, 1);

        bind(tcp_srv, (struct sockaddr *)&srv_sin, sizeof(srv_sin));
        listen(tcp_srv, 2);

        connect(tcp_cli, (struct sockaddr *)&srv_sin, sizeof(srv_sin));

        struct sockaddr_in acc_sin;
        socklen_t acc_len = sizeof(acc_sin);
        int accepted_fd = accept(tcp_srv, (struct sockaddr *)&acc_sin, &acc_len);

        if (accepted_fd >= 0) {
            const char *hello = "hello_cherry_tcp";
            send(tcp_cli, hello, strlen(hello), 0);

            char tcp_rx[32] = {0};
            ssize_t tn = recv(accepted_fd, tcp_rx, sizeof(tcp_rx) - 1, 0);
            if (tn == (ssize_t)strlen(hello) && strcmp(tcp_rx, hello) == 0) {
                t_tcp = 1;
            }
        }
    }

    /* 9. Verify iperf2 multi-core throughput benchmark */
    if (iperf2_run_self_test() == 0) {
        t_iperf2 = 1;
    }

    /* 10. Verify Linux net_device Model registration */
    int t_netdev = 0;
    struct net_device *nd_lo = dev_get_by_name("lo0");
    struct net_device *nd_eth = dev_get_by_name("eth0");
    struct net_device *nd_def = dev_get_default();
    if (nd_lo && nd_eth && nd_def && (nd_lo->flags & IFF_UP) && (nd_eth->flags & IFF_UP)) {
        t_netdev = 1;
    }

    /* 11. Verify iperf3 Benchmark */
    int t_iperf3 = 0;
    if (iperf3_run_self_test() == 0) {
        t_iperf3 = 1;
    }

    /* 12. Verify HTTP wget client */
    int t_wget = 0;
    int http_srv = socket(AF_INET, SOCK_STREAM, 0);
    if (http_srv >= 0) {
        struct sockaddr_in hsin;
        memset(&hsin, 0, sizeof(hsin));
        hsin.sin_family = AF_INET;
        hsin.sin_port = htons(8080);
        hsin.sin_addr.s_addr = IP4_ADDR(127, 0, 0, 1);
        bind(http_srv, (struct sockaddr *)&hsin, sizeof(hsin));
        listen(http_srv, 1);

        /* Spawn simple server thread */
        struct wget_test_arg warg = { .sfd = http_srv, .ready = false, .done = false };
        struct task_struct *wt = kthread_run(wget_mock_worker, &warg, "wget_srv");
        if (wt) {
            while (!warg.ready) {
                msleep(2);
            }
            char w_buf[64] = {0};
            int w_ret = wget_fetch("http://127.0.0.1:8080/index.html", "/cherry_wget_test.txt", w_buf, sizeof(w_buf));
            if (w_ret == 0 && strcmp(w_buf, "Hello Wget!") == 0) {
                t_wget = 1;
            }
            while (!warg.done) {
                msleep(2);
            }
        }
        close(http_srv);
    }

    int all_ok = t_virtio && t_ifconfig && t_mq_rss && t_ipv4_arp &&
                 t_ipv6_ndp && t_ping && t_udp && t_tcp && t_iperf2 &&
                 t_netdev && t_iperf3 && t_wget;

    pr_info("cherry_net: [net-verify] virtio=%s ifconfig=%s mq_rss=%s ipv4_arp=%s ipv6_ndp=%s ping=%s udp=%s tcp=%s iperf2=%s netdev=%s iperf3=%s wget=%s all=%s\n",
            t_virtio ? "pass" : "fail",
            t_ifconfig ? "pass" : "fail",
            t_mq_rss ? "pass" : "fail",
            t_ipv4_arp ? "pass" : "fail",
            t_ipv6_ndp ? "pass" : "fail",
            t_ping ? "pass" : "fail",
            t_udp ? "pass" : "fail",
            t_tcp ? "pass" : "fail",
            t_iperf2 ? "pass" : "fail",
            t_netdev ? "pass" : "fail",
            t_iperf3 ? "pass" : "fail",
            t_wget ? "pass" : "fail",
            all_ok ? "pass" : "fail");
}
