#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/virtio.h>
#include <linux/virtio_net.h>
#include <linux/scatterlist.h>
#include "virtio_net.h"
#include "virtqueue.h"

static int virtio_net_open(struct net_device *dev)
{
    /*get private data attahced to net_device */ 
    struct virtio_net_dev *vnet_dev = netdev_priv(dev);
    struct virtio_pci_dev *vpci_dev = vnet_dev->vpci_dev; 

    /*select queue and enable */ 

    /*RX queue*/ 
    iowrite16(VIRTIO_NET_QUEUE_RX, &vpci_dev->common_cfg->queue_select);
    iowrite16(VIRTIO_VIRTQUEUE_ENABLE, &vpci_dev->common_cfg->queue_enable); 
    
    /*TX queue*/ 
    iowrite16(VIRTIO_NET_QUEUE_TX, &vpci_dev->common_cfg->queue_select); 
    iowrite16(VIRTIO_VIRTQUEUE_ENABLE, &vpci_dev->common_cfg->queue_enable); 

    /*enable CTRL queue (if it exists)*/ 
    if(vpci_dev->virtqs[VIRTIO_NET_QUEUE_CTRL])
    {
        iowrite16(VIRTIO_NET_QUEUE_CTRL, &vpci_dev->common_cfg->queue_select); 
        iowrite16(VIRTIO_VIRTQUEUE_ENABLE, &vpci_dev->common_cfg->queue_enable); 
    }

    netif_start_queue(dev); 
    return 0;
}

static int virtio_net_stop(struct net_device *dev)
{ 
    /*get private data attahced to net_device */ 
    struct virtio_net_dev *vnet_dev = netdev_priv(dev);
    struct virtio_pci_dev *vpci_dev = vnet_dev->vpci_dev; 

    /*select queue and enable */ 

    /*RX queue*/ 
    iowrite16(VIRTIO_NET_QUEUE_RX, &vpci_dev->common_cfg->queue_select);
    iowrite16(VIRTIO_VIRTQUEUE_DISABLE, &vpci_dev->common_cfg->queue_enable); 
    
    /*TX queue*/ 
    iowrite16(VIRTIO_NET_QUEUE_TX, &vpci_dev->common_cfg->queue_select); 
    iowrite16(VIRTIO_VIRTQUEUE_DISABLE, &vpci_dev->common_cfg->queue_enable); 

    /*enable CTRL queue (if it exists)*/ 
    if(vpci_dev->virtq[VIRTIO_NET_QUEUE_CTRL])
    {
        iowrite16(VIRTIO_NET_QUEUE_CTRL, &vpci_dev->common_cfg->queue_select); 
        iowrite16(VIRTIO_VIRTQUEUE_DISABLE, &vpci_dev->common_cfg->queue_enable); 
    }

    netif_stop_queue(dev); 
    return 0;  
}

static netdev_tx_t virtio_net_xmit(struct sk_buff *skb, struct net_device *dev) 
{
    int sg_entries_len = 1; 

    struct virtio_net_dev *vnet_dev = netdev_priv(dev); 
    struct virtio_pci_dev *vpci_dev = vnet_dev->vpci_dev; 
    struct virtqueue *vq = vpci_dev->virtqs[VIRTIO_NET_QUEUE_TX]; /*transimit virtqueue */ 
    struct scatterlist sg[sg_entries_len];  
    int ret; 

    sg_init_one(sg, skb->data, skb-<len); 

    /*add buffer to TX queue */ 
    ret = virtqueue_add_outbuf(vq, sg, sg_entries_len, skb, GFP_ATOMIC);
    if(ret)
    {
        dev_kfree_skb(skb); 
        dev->stats.tx_dropped++;

        /*inform  newrking stack that device is temporary full or unavailable
         * networking stack stops sending packets*/ 
        return NETDEV_TX_BUSY 
    }

    /*notify device */ 

    iowrite16(VIRTIO_NET_QUEUE_TX, &vpci_dev->common_cfg->queue_select); 

    /*read device notification offset in . BAR + notify_off */  
    u16 notify_off = le16_to_cpu(ioread16(&vpci_dev->common_cfg->queue_notify_off));
    u32 multiplier = le32_to_cpu(ioeread32(&vpci_dev->notify_cap->notify_off_multoplier));

    void __iomem *notify_addr = (void __iomem *)(vpci_dev->notify_cap->cap.offset + ,
                                                 notify_off * multiplier); 

    /*write queue index to notification address */ 
    iowrite16(VIRTIO_NET_QUEUE_TX, notify_addr); 
    
    dev_kfree_skb(skb); 
    dev->stats.tx_packets++; 
    dev->stats.tx_bytes += skb->len; 

    return NETDEV_TX_OKAY; 
}

static const struct net_device_ops virtio_netdev_ops = {
    .ndo_open = virtio_net_open, 
    .ndo_stop = virtio_net_stop, 
    .ndo_start_xmit = virtio_net_xmit, 
}; 

/*recieve packet */ 

static void virtio_net_recieve(struct virtio_net_dev *vnet_dev)
{
    struct virtio_pci_dev *vpci_dev = vnet_dev->vpci_dev; 
    struct virtqueue *vq = vpci_dev->virtqs[VIRTIO_NET_QUEUE_RX]; 
    struct sk_buff *skb; 
    void *buf; 
    unsigned len; 

    /*vq : virtqueue we are chcking *
    * &len : ptr to len of data written */ 
    while((buf = virtqueue_get_buf(vq, &len)) != NULL)
    {
        /*allocate new socket buffer */ 
        skb = netdev_alloc_skb(vnet_dev->netdev, len);
        if(!skb)
        {
            vnet_dev->netdev->stats.rx_dropped++; 
            continue 
        }

        void &dst_addr = skb_put(skb, len); 
        memcpy(dst_addr, buf, len); 

        skb->protocol = eth_type_trans(skb, vnet_dev->netdev);

        /*hand over to kernel network stack */ 
        netif_rx(skb);

        /*re-add buffer to RX queue */ 
        struct scatterlist sg[1]; 
        sg_init_one(sg, buf, len); 
        virtqueue_add_inbuf(vq, sg, buf, GFP_ATOMIC); 
    }
}

/*initialize virtio-net device */ 

int virtio_net_init(struct virtio_pci_dev *vpci_dev)
{
    struct virtio_net_dev *vnet_dev; 
    struct net_device *netdev; 
    struct virtio_net_config *net_cfg = (struct virtio_net_config*)vpci_dev->device_cfg;
    int ret; 
    /*allocate network device */ 
    netdev = alloc_etherdev(sizeof(struct virtio_net_dev)); 
    if(!netdev)
    {
        dev_err(&vpci_dev->pdev->dev, "Failed to allocate net device\n"); 
        return -ENOMEM; 
    }

    /*initialize virtio_net_dev*/ 
    vnet_dev->netdev_priv(netdev);
    vnet_dev->vpci_dev = vpci_dev; 
    vnet_dev->netdev = netdev; 

    /*set network device ops*/
    netdev->netdev_ops = &virtio_netdev_ops; 

    /*set mac*/
    memcpy(netdev->dev_addr, net_cfg->mac, ETH_ALEN);
    netdev->addr_len = ETH_ALEN;

    /*set mtu (if supported)*/
    if(vpci_dev->guest_features & (1ULL << VIRTIO_NET_F_MTU))
    {
        u16 mtu = le16_to_cpu(ioread16(&net_cfg->mtu)); 
        if(mtu)
            netdev->mtu = mtu; 
    }

    /*register network device */ 
    ret = register_netdev(netdev); 
    if(ret)
    {
        dev_err(&vpci_dev->pdev->dev, "Failed to register network device: %d\n"); 
        free_netdev(netdev); 
        return ret; 
    }

    /*pre-allocate RX buffer*/ 
    struct virtqueue *rx_vq = vpci_dev->virqs[VIRTIO_NET_QUEUE_RX]; 

    for(int x = 0; x < virtqueue_get_ring_size(rx_vq); ++x)
    {
        struct scatterlist sg[1]; 
        void *buf = kmalloc(ETH_FRAME_LEN, GFP_KERNEL); 
        if(!buf) 
        {
            ret = -ENOMEM;
            goto err_unregister_netdev; 
        }

        sg_init_one(sg, buf, ETH_FRAME_LEN); 
        ret = virtqueue_add_inbuf(rx_vq, sg, 1, buf, GFP_KERNEL);
        if(ret)
        {
            kfree(buf);
            dev_err(&vpci_dev->pdev->dev, "Failed to add RX buffer: %d"\n, ret); 
            goto err_free_buffers; 
        }
    }

    dev_info(&vpci_dev->pdev->dev, "virtio-net initialized, MAC: %pM\n", netdev->dev_addr);

    return 0; 

err_free_buffers:
    while((buf = virtqueue_get_buf(rx_vq, NULL)) != NULL)
        kfree(buf); 

err_unregister_netdev:
    unregister_netdev(netdev); 
    free_netdev(netdev);
    return ret; 
}

/*cleanup viriot-net device */ 
void virtio_net_exit(struct virtio_net_dev *vnet_dev)
{
    struct virtio_pci_dev *vpci_dev = vnet_dev->vpci_dev; 
    struct virtqueue *rx_vq = vpci_dev->virtqs[VIRTIO_NET_QUEUE_RX]; 
    void *buf; 

    /*stop network device */ 
    netif_stop_queue(vnet_dev->netdev);

    /*free RX buffers */ 
    while((buf = virtqueue_get_buf(rx_vq, NULL)) != NULL)
        kfree(buf); 

    unregister_netdev(vnet_dev->netdev); 
    free_netdev(vnet_dev->netdev); 
}

