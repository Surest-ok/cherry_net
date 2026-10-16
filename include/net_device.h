/*
 * CherryNet - Network Device Model (Linux-compatible struct net_device)
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#ifndef _CHERRY_NET_DEVICE_H_
#define _CHERRY_NET_DEVICE_H_

#include "net_types.h"
#include "net_buf.h"
#include "net_if.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

/* Interface flags (compatible with Linux netdevice.h / if.h) */
#ifndef IFF_UP
#define IFF_UP          0x0001  /* Interface is up */
#define IFF_BROADCAST   0x0002  /* Broadcast address valid */
#define IFF_DEBUG       0x0004  /* Turn on debugging */
#define IFF_LOOPBACK    0x0008  /* Is a loopback net */
#define IFF_POINTOPOINT 0x0010  /* Interface is has p-p link */
#define IFF_NOTRAILERS  0x0020  /* Avoid use of trailers */
#define IFF_RUNNING     0x0040  /* Resources allocated */
#define IFF_NOARP       0x0080  /* No ARP protocol */
#define IFF_PROMISC     0x0100  /* Receive all packets */
#define IFF_ALLMULTI    0x0200  /* Receive all multicast packets */
#define IFF_MULTICAST   0x1000  /* Supports multicast */
#endif

/* Hardware type */
#define ARPHRD_ETHER    1       /* Ethernet 10/100/1000/10G */
#define ARPHRD_LOOPBACK 772     /* Loopback device */

struct net_device;

/* Network device statistics */
struct net_device_stats {
    unsigned long rx_packets;
    unsigned long tx_packets;
    unsigned long rx_bytes;
    unsigned long tx_bytes;
    unsigned long rx_errors;
    unsigned long tx_errors;
    unsigned long rx_dropped;
    unsigned long tx_dropped;
    unsigned long collisions;
};

/* Network device operations */
struct net_device_ops {
    int  (*ndo_open)(struct net_device *dev);
    int  (*ndo_stop)(struct net_device *dev);
    int  (*ndo_start_xmit)(struct net_device *dev, struct net_buf *buf);
    int  (*ndo_set_mac_address)(struct net_device *dev, const void *addr);
    int  (*ndo_get_stats)(struct net_device *dev, struct net_device_stats *stats);
    int  (*ndo_do_ioctl)(struct net_device *dev, int cmd, void *arg);
};

/* Linux-compatible struct net_device */
struct net_device {
    char                    name[IFNAMSIZ];
    unsigned int            flags;
    unsigned int            type;           /* ARPHRD_* */
    unsigned int            mtu;
    unsigned char           dev_addr[ETH_ALEN];
    unsigned char           broadcast[ETH_ALEN];

    const struct net_device_ops *netdev_ops;
    void                   *priv;           /* Driver private data */
    struct netif           *sys_netif;      /* Associated CherryNet netif */
    struct net_device_stats stats;

    struct net_device      *next;
};

/* Memory allocation / free for network device */
struct net_device *alloc_netdev(size_t priv_size, const char *name, void (*setup)(struct net_device *));
struct net_device *alloc_etherdev(size_t priv_size);
void               free_netdev(struct net_device *dev);

/* Register / unregister net_device into OS */
int                register_netdev(struct net_device *dev);
int                unregister_netdev(struct net_device *dev);

/* Packet RX/TX API */
int                netif_rx(struct net_device *dev, struct net_buf *buf);
int                dev_queue_xmit(struct net_device *dev, struct net_buf *buf);

/* Device lookup */
struct net_device *dev_get_by_name(const char *name);
struct net_device *dev_get_default(void);
void               dev_set_default(struct net_device *dev);

#ifdef __cplusplus
}
#endif

#endif /* _CHERRY_NET_DEVICE_H_ */
