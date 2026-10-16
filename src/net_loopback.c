/*
 * CherryNet - Loopback Interface lo0 (127.0.0.1, ::1) via Net Device Model
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#include "net_device.h"
#include "net_if.h"
#include "net_queue.h"
#include "net_eth.h"
#include <string.h>

static struct net_device s_loopback_dev;
static struct netif      s_loopback_netif;

static int loopback_start_xmit(struct net_device *dev, struct net_buf *buf)
{
    if (!dev || !buf) return -1;

    /* Loop packet directly back to receiver queue */
    return netif_rx(dev, buf);
}

static const struct net_device_ops s_loopback_ops = {
    .ndo_start_xmit = loopback_start_xmit,
};

static void loopback_setup(struct net_device *dev)
{
    dev->type = ARPHRD_LOOPBACK;
    dev->mtu = 16384;
    dev->flags = IFF_LOOPBACK | IFF_UP | IFF_RUNNING;
    dev->netdev_ops = &s_loopback_ops;
}

int net_loopback_init(void)
{
    memset(&s_loopback_dev, 0, sizeof(s_loopback_dev));
    strncpy(s_loopback_dev.name, "lo0", sizeof(s_loopback_dev.name) - 1);
    loopback_setup(&s_loopback_dev);

    memset(&s_loopback_netif, 0, sizeof(s_loopback_netif));
    s_loopback_netif.flags = NETIF_FLAG_UP | NETIF_FLAG_LOOPBACK | NETIF_FLAG_LINK_UP;
    s_loopback_netif.mtu = 16384;

    /* 127.0.0.1 / 8 */
    s_loopback_netif.ip4_addr.addr = IP4_ADDR(127, 0, 0, 1);
    s_loopback_netif.ip4_netmask.addr = IP4_ADDR(255, 0, 0, 0);
    s_loopback_netif.ip4_gw.addr = IP4_ADDR(127, 0, 0, 1);

    /* ::1 */
    s_loopback_netif.ip6_global.u8[15] = 1;
    s_loopback_netif.ip6_ll.u8[15] = 1;
    s_loopback_netif.ip6_prefix_len = 128;

    s_loopback_dev.sys_netif = &s_loopback_netif;
    return register_netdev(&s_loopback_dev);
}
