/*
 * CherryNet - Network Device Model Implementation
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#include "net_device.h"
#include "net_queue.h"
#include "cherry_vfs.h"
#include "cherry_printk.h"
#include "cherry_kal.h"
#include <string.h>

static struct net_device *g_netdev_list = NULL;
static struct net_device *g_default_netdev = NULL;
static spinlock_t         g_netdev_lock;
static bool               g_netdev_inited = false;

static void netdev_system_init_once(void)
{
    if (!g_netdev_inited) {
        spin_lock_init(&g_netdev_lock);
        g_netdev_inited = true;
    }
}

static int netdev_netif_send(struct netif *nif, struct net_buf *buf)
{
    if (!nif || !buf) return -1;
    struct net_device *dev = (struct net_device *)nif->priv;
    if (!dev) return -1;
    return dev_queue_xmit(dev, buf);
}

static void ether_setup(struct net_device *dev)
{
    dev->type = ARPHRD_ETHER;
    dev->mtu = 1500;
    dev->flags = IFF_BROADCAST | IFF_MULTICAST;
    memset(dev->broadcast, 0xFF, ETH_ALEN);
}

struct net_device *alloc_netdev(size_t priv_size, const char *name, void (*setup)(struct net_device *))
{
    netdev_system_init_once();

    size_t total_size = sizeof(struct net_device) + priv_size;
    struct net_device *dev = (struct net_device *)kmalloc(total_size, GFP_KERNEL);
    if (!dev) return NULL;

    memset(dev, 0, total_size);
    if (name) {
        strncpy(dev->name, name, sizeof(dev->name) - 1);
    }
    if (priv_size > 0) {
        dev->priv = (void *)(dev + 1);
    }

    if (setup) {
        setup(dev);
    }
    return dev;
}

struct net_device *alloc_etherdev(size_t priv_size)
{
    return alloc_netdev(priv_size, "eth%d", ether_setup);
}

void free_netdev(struct net_device *dev)
{
    if (!dev) return;
    if (dev->sys_netif) {
        kfree(dev->sys_netif);
        dev->sys_netif = NULL;
    }
    kfree(dev);
}

int register_netdev(struct net_device *dev)
{
    if (!dev) return -EINVAL;
    netdev_system_init_once();

    /* If name contains %d, assign first available index */
    char *fmt = strstr(dev->name, "%d");
    if (fmt) {
        char prefix[IFNAMSIZ];
        size_t plen = (size_t)(fmt - dev->name);
        strncpy(prefix, dev->name, plen);
        prefix[plen] = '\0';

        int idx = 0;
        char cand[IFNAMSIZ];
        for (;;) {
            snprintf(cand, sizeof(cand), "%s%d", prefix, idx);
            if (dev_get_by_name(cand) == NULL) {
                strncpy(dev->name, cand, sizeof(dev->name) - 1);
                break;
            }
            idx++;
        }
    }

    /* Allocate associated CherryNet netif if not already present */
    if (!dev->sys_netif) {
        dev->sys_netif = (struct netif *)kmalloc(sizeof(struct netif), GFP_KERNEL);
        if (!dev->sys_netif) return -ENOMEM;
        memset(dev->sys_netif, 0, sizeof(struct netif));
    }

    struct netif *nif = dev->sys_netif;
    strncpy(nif->name, dev->name, sizeof(nif->name) - 1);
    nif->mtu = (uint16_t)dev->mtu;
    memcpy(nif->hwaddr, dev->dev_addr, ETH_ALEN);
    nif->priv = dev;
    nif->send = netdev_netif_send;
    nif->flags = (uint16_t)dev->flags;

    /* Register netif with core stack */
    netif_register(nif, dev->name, (uint16_t)dev->mtu);

    /* Open device via ops if available */
    if (dev->netdev_ops && dev->netdev_ops->ndo_open) {
        int ret = dev->netdev_ops->ndo_open(dev);
        if (ret != 0) {
            return ret;
        }
    }

    dev->flags |= IFF_UP | IFF_RUNNING;
    nif->flags |= NETIF_FLAG_UP | NETIF_FLAG_RUNNING;

    /* Add to global list */
    unsigned long flags = 0;
    spin_lock_irqsave(&g_netdev_lock, flags);
    dev->next = g_netdev_list;
    g_netdev_list = dev;
    if (!g_default_netdev) {
        g_default_netdev = dev;
        netif_set_default(nif);
    }
    spin_unlock_irqrestore(&g_netdev_lock, flags);

    pr_info("cherry_net: registered network device '%s' (mtu=%u)\n", dev->name, dev->mtu);
    return 0;
}

int unregister_netdev(struct net_device *dev)
{
    if (!dev) return -EINVAL;

    if (dev->netdev_ops && dev->netdev_ops->ndo_stop) {
        dev->netdev_ops->ndo_stop(dev);
    }
    dev->flags &= ~(IFF_UP | IFF_RUNNING);

    unsigned long flags = 0;
    spin_lock_irqsave(&g_netdev_lock, flags);
    struct net_device **curr = &g_netdev_list;
    while (*curr) {
        if (*curr == dev) {
            *curr = dev->next;
            dev->next = NULL;
            break;
        }
        curr = &(*curr)->next;
    }
    if (g_default_netdev == dev) {
        g_default_netdev = g_netdev_list;
    }
    spin_unlock_irqrestore(&g_netdev_lock, flags);

    return 0;
}

int netif_rx(struct net_device *dev, struct net_buf *buf)
{
    if (!dev || !buf) return -EINVAL;

    dev->stats.rx_packets++;
    dev->stats.rx_bytes += buf->len;

    buf->netif = dev->sys_netif;
    return net_queue_dispatch(buf);
}

int dev_queue_xmit(struct net_device *dev, struct net_buf *buf)
{
    if (!dev || !buf) return -EINVAL;

    dev->stats.tx_packets++;
    dev->stats.tx_bytes += buf->len;

    if (dev->netdev_ops && dev->netdev_ops->ndo_start_xmit) {
        return dev->netdev_ops->ndo_start_xmit(dev, buf);
    }

    net_buf_free(buf);
    return -ENOSYS;
}

struct net_device *dev_get_by_name(const char *name)
{
    if (!name) return NULL;
    netdev_system_init_once();

    unsigned long flags = 0;
    spin_lock_irqsave(&g_netdev_lock, flags);
    struct net_device *curr = g_netdev_list;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            spin_unlock_irqrestore(&g_netdev_lock, flags);
            return curr;
        }
        curr = curr->next;
    }
    spin_unlock_irqrestore(&g_netdev_lock, flags);
    return NULL;
}

struct net_device *dev_get_default(void)
{
    netdev_system_init_once();
    return g_default_netdev;
}

void dev_set_default(struct net_device *dev)
{
    netdev_system_init_once();
    unsigned long flags = 0;
    spin_lock_irqsave(&g_netdev_lock, flags);
    g_default_netdev = dev;
    if (dev && dev->sys_netif) {
        netif_set_default(dev->sys_netif);
    }
    spin_unlock_irqrestore(&g_netdev_lock, flags);
}
