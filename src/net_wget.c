/*
 * CherryNet - HTTP Client (wget) Implementation
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#include "net_wget.h"
#include "cherry_vfs.h"
#include "cherry_printk.h"
#include "cherry_kal.h"
#include "arpa/inet.h"
#include <string.h>

static int parse_url(const char *url, char *host, size_t host_sz, int *port, char *path, size_t path_sz)
{
    if (!url || !host || !port || !path) return -1;

    *port = 80;
    const char *p = url;

    /* Skip optional http:// prefix */
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

    /* Extract host and optional port */
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    if (colon && (!slash || colon < slash)) {
        size_t hlen = (size_t)(colon - p);
        if (hlen >= host_sz) hlen = host_sz - 1;
        strncpy(host, p, hlen);
        host[hlen] = '\0';

        *port = 0;
        colon++;
        while (*colon >= '0' && *colon <= '9') {
            *port = *port * 10 + (*colon - '0');
            colon++;
        }
    } else {
        size_t hlen = slash ? (size_t)(slash - p) : strlen(p);
        if (hlen >= host_sz) hlen = host_sz - 1;
        strncpy(host, p, hlen);
        host[hlen] = '\0';
    }

    if (slash) {
        strncpy(path, slash, path_sz - 1);
        path[path_sz - 1] = '\0';
    } else {
        strncpy(path, "/", path_sz - 1);
        path[path_sz - 1] = '\0';
    }

    return 0;
}

int wget_fetch(const char *url, const char *out_filepath, char *out_buf, size_t buf_size)
{
    char host[64];
    char path[128];
    int port = 80;

    if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0) {
        pr_err("wget: invalid url '%s'\n", url ? url : "(null)");
        return -EINVAL;
    }

    uint32_t ip = inet_addr(host);
    if (ip == (uint32_t)-1) {
        /* Default fallback to localhost if loopback string */
        if (strcmp(host, "localhost") == 0) {
            ip = IP4_ADDR(127, 0, 0, 1);
        } else {
            pr_err("wget: unable to resolve host '%s'\n", host);
            return -EHOSTUNREACH;
        }
    }

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        pr_err("wget: failed to create socket\n");
        return sfd;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons((uint16_t)port);
    saddr.sin_addr.s_addr = ip;

    if (connect(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) != 0) {
        pr_err("wget: failed to connect to %s:%d\n", host, port);
        close(sfd);
        return -ECONNREFUSED;
    }

    /* Send HTTP 1.0 GET request */
    char req[256];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "User-Agent: CherryOS-wget/1.0\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, host);

    send(sfd, req, strlen(req), 0);

    /* Open output file if specified */
    int out_fd = -1;
    if (out_filepath) {
        out_fd = open(out_filepath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (out_fd < 0) {
            pr_err("wget: unable to open output file '%s'\n", out_filepath);
            close(sfd);
            return out_fd;
        }
    }

    /* Read response */
    char chunk[512];
    bool header_parsed = false;
    size_t total_body_bytes = 0;

    for (;;) {
        ssize_t n = recv(sfd, chunk, sizeof(chunk) - 1, 0);
        if (n <= 0) break;

        chunk[n] = '\0';
        char *body_start = chunk;

        if (!header_parsed) {
            char *sep = strstr(chunk, "\r\n\r\n");
            if (sep) {
                header_parsed = true;
                body_start = sep + 4;
            } else {
                continue;
            }
        }

        size_t body_len = (size_t)(chunk + n - body_start);
        if (body_len > 0) {
            if (out_fd >= 0) {
                write(out_fd, body_start, body_len);
            }
            if (out_buf && total_body_bytes < buf_size) {
                size_t cpy = (total_body_bytes + body_len < buf_size) ?
                             body_len : (buf_size - total_body_bytes - 1);
                memcpy(out_buf + total_body_bytes, body_start, cpy);
                out_buf[total_body_bytes + cpy] = '\0';
            }
            total_body_bytes += body_len;
        }
    }

    if (out_fd >= 0) {
        close(out_fd);
    }
    close(sfd);

    pr_info("wget: downloaded %u bytes from %s\n", (unsigned int)total_body_bytes, url);
    return 0;
}

int cmd_wget(int argc, char **argv)
{
    if (argc < 2) {
        pr_info("Usage: wget <url> [-O <output_file>]\n");
        return -1;
    }

    const char *url = argv[1];
    const char *outfile = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-O") == 0 && (i + 1 < argc)) {
            outfile = argv[++i];
        }
    }

    return wget_fetch(url, outfile, NULL, 0);
}
