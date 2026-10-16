/*
 * CherryNet - iperf3 Throughput Benchmark Engine
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#ifndef _CHERRY_NET_IPERF3_H_
#define _CHERRY_NET_IPERF3_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IPERF3_DEFAULT_PORT 5201

/* Start iperf3 server daemon on given port */
int iperf3_server_start(uint16_t port);

/* Run iperf3 client connecting to server */
int iperf3_client_run(const char *server_ip, uint16_t port, uint32_t duration_sec, uint32_t buf_len);

/* Cross-core loopback performance self-test */
int iperf3_run_self_test(void);

/* Shell / CLI command */
int cmd_iperf3(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* _CHERRY_NET_IPERF3_H_ */
