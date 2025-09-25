
#ifndef VIRTIO_NET_DRIVER_H
#define VIRTIO_NET_DRIVER_H

#include <linux/virtio.h>
#include <linux/virtio_net.h>      // provides struct virtio_net_config, feature bits, etc.
#include <linux/netdevice.h>
#include "virtio_pci.h"            // your wrapper for PCI-specific structures

/* Wrapper struct for your virtio-net device */
struct virtio_net_dev {
    struct virtio_pci_dev *vpci_dev;   /* PCI device */
    struct net_device *netdev;         /* Linux net_device */
};

/* Driver init and exit functions */
int virtio_net_init(struct virtio_pci_dev *vpci_dev);
void virtio_net_exit(struct virtio_net_dev *vnet_dev);
int virtio_net_init(struct virtio_pci_dev *vpci_dev);
void virtio_net_exit(struct virtio_net_dev *vnet_dev);
int virtio_net_open(struct net_device *dev);
int virtio_net_stop(struct net_device *dev);
netdev_tx_t virtio_net_xmit(struct sk_buff *skb, struct net_device *dev);
void virtio_net_receive(struct virtqueue *vq);
#endif /* VIRTIO_NET_DRIVER_H */
