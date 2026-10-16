/*
 * CherryNet - HTTP Client (wget)
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#ifndef _CHERRY_NET_WGET_H_
#define _CHERRY_NET_WGET_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fetch content from HTTP URL.
 * If out_filepath is non-NULL, writes content to file.
 * If out_buf is non-NULL, stores content to buffer up to buf_size.
 * Returns 0 on success, negative error code on failure.
 */
int wget_fetch(const char *url, const char *out_filepath, char *out_buf, size_t buf_size);

/* Command handler for Shell / Busybox */
int cmd_wget(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* _CHERRY_NET_WGET_H_ */
