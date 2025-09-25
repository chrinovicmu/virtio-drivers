#include <linux/module.h>
#include <linux/kernel.h> 
#include <linux/pci.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>
#include <linux/slab.h>
#include <linux/errno.h> 
#include <linux/virtio_pci.h>
#include <linux/dma-mapping.h> 
#include <linux/virtio.h> 
#include <linux/virtio_ids.h> 
#include "virtio_net.h"
#include "virtio_pci.h"


static const struct pci_device_id virtio_pci_id_table[] = {
    {PCI_DEVICE(PCI_VENDOR_ID_VIRTIO, PCI_DEVICE_ID_VIRTIO_NET)}, 
    {0}
};
MODULE_DEVICE_TABLE(pci, virtio_pci_id_table); 

static void virtio_pci_get(struct virtio_device *vdev, unsigned offset, 
                           void *buf, unsigned int len)
{
    struct virtio_pci_dev *vpci_dev = vdev->priv; 

    void __iomem *device_cfg = vpci_dev->device_cfg;
    if(!device_cfg)
    {
        dev_err(&vpci_dev->pdev->dev, "No device config region for get at offset %u\n", offset); 
        return; 
    }

    switch(len)
    {
        case 1:
            *(u8*)buf = ioread8(device_cfg + offset); 
            break; 
        case 2: 
            *(u16*)buf = le16_to_cpu(ioread16(device_cfg + offset)); 
            break; 
        case 4:
            *(u32 *)buf = le32_to_cpu(ioread32(device_cfg + offset)); 
            break; 
        
        default:
            dev_err(&vpci_dev->pdev->dev, "Invaliid get PCI length %u at offset %u\n", len, offset); 
            break; 
    }
}

static void virtio_pci_set(struct virtio_device *vdev, unsigned offset, 
                           const void *buf, unsigned len)
{
    struct virtio_pci_dev *vpci_dev = vdev->priv; 
    void __iomem *device_cfg = vpci_dev->device_cfg; 

    switch(len)
    {
        case 1: 
            iowrite8(*(u8*)buf, device_cfg + offset); 
            break;
        case 2: 
            iowrite16(cpu_to_le16(*(u16*)buf), device_cfg + offset);
            break; 
        case 4:
            iowrite32(cpu_to_le32(*(u32*)buf), device_cfg + offset); 
            break; 

        default:
            dev_err(&vpci_dev->pdev->dev, "Invalid set PCI %u at offset %u\n", len, offset); 
            break; 
    }
}

/*return generation number to detect if the config changed while reading
 * (for atomic snapshop constistency) */ 
static u32 virtio_pci_generation(struct virtio_device *vdev)
{
    struct virtio_pci_dev *vpci_dev = vdev->priv; 
    return ioread8(&vpci_dev->common_cfg->config_generation); 
}

static u8 virtio_pci_get_status(struct virtio_device *vdev)
{
    struct virtio_pci_dev *vpci_dev = vdev->priv; 
    return ioread8(&vpci_dev->common_cfg->device_status); 
}

static void virtio_pci_set_status(struct virtio_device *vdev, u8 status)
{
    struct virtio_pci_dev *vpci_dev = vdev->priv;
    iowrite8(status, &vpci_dev->common_cfg->device_status);  
}

static void virtio_pci_reset(struct virtio_device *vdev)  
{
    struct virtio_pci_dev *vpci_dev = vdev->priv;  
    iowrite8(0, &vpci_dev->common_cfg->device_status);  
}

static u64 virtio_pci_get_features(struct virtio_device *vdev)
{
    struct virtio_pci_dev *vpci_dev = vdev->priv; 
    u64 features; 

    iowrite32(VIRTIO_FSEL_0_31, &vpci_dev->common_cfg->device_feature_select);
    features = ioread32(&vpci_dev->common_cfg->device_feature); 
    iowrite32(VIRTIO_FSEL_32_63, &vpci_dev->common_cfg->device_feature_select); 
    features |= (u64)ioread32(&vpci_dev->common_cfg->device_feature) << 32; 

    return features; 
}

static void virtio_pci_set_features(struct virtio_device *vdev, u64 features)  /* Implements the set_features operation to write driver-accepted features. */
{
    struct virtio_pci_dev *vpci_dev = vdev->priv;

    iowrite32(VIRTIO_FSEL_0_31, &vpci_dev->common_cfg->guest_feature_select); 
    iowrite32(features & 0xFFFFFFFF, &vpci_dev->common_cfg->guest_feature);

    iowrite32(VIRTIO_FSEL_32_63, &vpci_dev->common_cfg->guest_feature_select);  
    iowrite32(features >> 32, &vpci_dev->common_cfg->guest_feature);

}

static int virtio_pci_finalize_features(struct virtio_device* vdev)
{
    struct virtio_pci_dev *vpci_dev = vdev->priv; 
    u8 status; 

    status = ioread8(&vpci_dev->common_cfg->device_status); 
    iowrite8(status | VIRTIO_CONFIG_S_FEATURES_OK, &vpci_dev->common_cfg->device_status); 

    status = ioread8(&vpci_dev->common_cfg->device_status);
    if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    {
        dev_err(&vpci_dev->pdev->dev, "Failed to finalize features"); 
        return -EINVAL; 
    }

    return 0; 
}

/*
static struct virtqueue *virtio_pci_setup_vq(struct virtio_device *vdev, unsigned int index,
                                             void (*callback)(struct virtqueue *vq),
                                             const char *name, bool ctx)
{
    struct virtio_pci_dev *vpci_dev = vdev->priv; 
    struct virtio_pci_common_cfg *cfg = vpci_dev->common_cfg; 
    struct virtqueue *vq;
    u64 addr; 
    u16 msix_vector = VIRTIO_MSI_NO_VECTOR; 

    iowrite16(index, &cfg->queue_select); 
    if(ioread16(&cfg->queue_size) == 0)
    {
        dev_err(&vpci_dev->pdev->dev, "Queue %u has size 0\n", index); 
        return ERR_PTR(-EINVAL); 
    }

    vq = vring_create_virtqueue(index, ioread16(&cfg->queue_size), 16,
                                vdev, true, ctx, callback, name); 

    if(!vq)
    {
        dev_err(&vpci_dev->pdev->dev, "Failed to create virtqueue %u", index); 
        return ERR_PTR(-ENOMEM); 
    }

    // write physical addr of virtqueue sections to approate common configurations fields 
    addr = virt_to_phys(vq->desc); 
    iowrite64(addr, &cfg->queue_desc); 
    addr = virt_to_phys(vq->avail); 
    iowrite64(addr, &cfg->queue_driver);
    addr = virt_to_phys(vq->used); 
    iowrite64(addr, &cfg->queue_device); 

    iowrite16(msix_vector, &cfg->queue_msix_vector);
    iowrite16(VIRTIO_VIRTQUEUE_ENABLE, &cfg->queue_enable); 
    
    /*read and write back notification offset for constistency
    iowrite16(ioread16(&cfg->queue_notify_off), &cfg->queue_notify_off); 

    return vq; 
}
*/



static bool virtio_pci_notify(struct virtqueue *vq)
{
    return true;
}

static struct virtqueue *virtio_pci_setup_vq(struct virtio_device *vdev,
                                             unsigned int index,
                                             vq_callback_t *callback)
{
    struct virtio_pci_dev *vpci_dev = vdev->priv;
    struct virtqueue *vq;
    u16 qsize;
    
    iowrite16(index, &vpci_dev->common_cfg->queue_select);
    
    qsize = ioread16(&vpci_dev->common_cfg->queue_size);
    
    if (qsize == 0) {
        dev_err(&vpci_dev->pdev->dev, "Queue %u has size 0\n", index);
        return ERR_PTR(-EINVAL);  // Return error pointer for invalid argument
    }
    
    vq = vring_create_virtqueue(
        index,                    // unsigned int index - Queue index identifier
        qsize,                    // unsigned int num - Queue size (number of entries)
        16,                       // unsigned int vring_align - Alignment requirement (16 bytes)
        vdev,                     // struct virtio_device *vdev - VirtIO device pointer
        true,                     // bool weak_barriers - Use weaker memory barriers for performance
        false,                    // bool may_reduce_num - Allow queue size reduction
        NULL,                     // void *context - Optional context pointer (usually NULL)
        virtio_pci_notify,        // bool (*notify)(struct virtqueue *) - Notification function
        callback,                 // void (*callback)(struct virtqueue *) - RX callback function
        "virtio-pci-vq");         // const char *name - Queue name for debugging
    
    if (!vq) {
        dev_err(&vpci_dev->pdev->dev, "Failed to create virtqueue %u\n", index);
        return ERR_PTR(-ENOMEM);  // Return error pointer for out of memory
    }
    
    iowrite16(VIRTIO_VIRTQUEUE_ENABLE, &vpci_dev->common_cfg->queue_enable);
    
    return vq;
}

/*
static void virtio_pci_del_vq(struct virtio_device *vdev, struct virtqueue *vq)
{
    struct virtio_pci_dev *vpci_dev = vdev->priv; 
    iowrite16(vq->index, &vpci_dev->common_cfg->queue_select); 
    iowrite16(VIRTIO_VIRTQUEUE_DISABLE, &vpci_dev->common_cfg->queue_enable); 
    vring_del_virtqueue(vq); 
}

static void virtio_pci_del_vqs(struct virtio_device *vdev)
{
    struct virtqueue *vq, *n;
    list_for_each_entry_safe(vq, n, &vdev->vqs, list){
        virtio_pci_del_vq(vdev, vq); 
    }
}

static int virtio_pci_find_vqs(struct virtio_device *vdev, unsigned nvqs, 
                               struct virtqueue *vqs[], vq_callback_t *callbacks[], 
                               const char *const names[], const bool *ctx, 
                               struct irq_affinity *desc)
{
    struct virtio_pci_dev *vpci_dev = vdev->priv; 
    unsigned x; 

    for(x = 0; x < nvqs; x++)
    {
        vqs[x] = virtio_pci_setup_vq(vdev, x, callbacks[x], names[x], ctx ? ctx[x] : false); 
        if(IS_ERR(vqs[x]))
        {
            int err = PTR_ERR(vqs[x]);
            virtio_pci_del_vqs(vdev); 
            return err;
        }
    }
    return 0; 
}

*/ 
static void virtio_pci_del_vq(struct virtqueue *vq)
{
    if (!vq)
        return;

    /* vring_del_virtqueue handles freeing the queue */ 
    vring_del_virtqueue(vq);
}

static void virtio_pci_del_vqs(struct virtio_device *vdev)
{
    struct virtqueue *vq, *n;

    list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
        virtio_pci_del_vq(vq);
    }
}

static const struct virtio_config_ops virtio_pci_config_ops = {
    .get = virtio_pci_get, 
    .set = virtio_pci_set, 
    .generation = virtio_pci_generation, 
    .get_status = virtio_pci_get_status, 
    .set_status = virtio_pci_set_status, 
    .reset = virtio_pci_reset, 
    .get_features = virtio_pci_get_features, 
//    .set_features = virtio_pci_set_features, 
    .finalize_features = virtio_pci_finalize_features, 
  //  .find_vqs = virtio_pci_find_vqs, 
    .del_vqs = virtio_pci_del_vqs, 
}; 

static int virtio_pci_map_common_cfg(struct virtio_pci_dev *vpci_dev, u8 pos)
{
    struct pci_dev *pdev = vpci_dev->pdev; 
    struct virtio_pci_cap cap = {0}; 
    void __iomem *bar_base;   
    int ret; 

    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_VNDR_OFFSET, (u32 *)&cap);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to read capability at 0x%x (vendor fields)\n",
                pos + VIRTIO_PCI_CAP_VNDR_OFFSET);
        return ret;
    }

    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_BAR_OFFSET, (u32 *)&cap.bar);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to read capability at 0x%x (BAR fields)\n",
                pos + VIRTIO_PCI_CAP_BAR_OFFSET);
        return ret;
    }

    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_OFFSET_OFFSET, &cap.offset);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to read capability at 0x%x (offset field)\n",
                pos + VIRTIO_PCI_CAP_OFFSET_OFFSET);
        return ret;
    }

    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_LENGTH_OFFSET, &cap.length);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to read capability at 0x%x (length field)\n",
                pos + VIRTIO_PCI_CAP_LENGTH_OFFSET);
        return ret;
    }

    if(cap.cfg_type != VIRTIO_PCI_CAP_COMMON_CFG) 
    {
         dev_err(&pdev->dev, "Expected common cfg capablilty at 0x%x, got type %d\n", pos, cap.cfg_type); 
        return -EINVAL; 
    }

    if(cap.bar >= PCI_STD_NUM_BARS)
    {
        dev_err(&pdev->dev, "Invalid Bar index %d for commong cfg capablilty\n", cap.bar); 
        return -EINVAL; 
    }

    if (cap.length < sizeof(struct virtio_pci_common_cfg)) {
        dev_err(&pdev->dev, "Common cfg capability length %d too small\n", cap.length);
        return -EINVAL;
    }

    /*map physial address at BAR[cap.bar] ino kernel virtual memory */ 
    bar_base = pci_ioremap_bar(pdev, cap.bar);
    if (!bar_base)
    {
        dev_err(&pdev->dev, "Failed to map BAR %d for common cfg\n", cap.bar);
        return -ENOMEM;
    } 
    
    /*points directly to the common_cfg structure */ 
    vpci_dev->common_cfg = bar_base + cap.offset;
    vpci_dev->common_cfg_base = bar_base; 

    /*check if memory is mapped */ 
    if(ioread8(&vpci_dev->common_cfg->device_status) == 0xFF)
    {
        dev_err(&pdev->dev, "Common cfg region at BAR %d Offset 0x%x is invaild\n",
                cap.bar, cap.offset); 
        iounmap(bar_base); 
        vpci_dev->common_cfg = NULL; 
        vpci_dev->common_cfg_base = NULL; 
        return -EIO; 
    }

    return 0; 
}

static int virtio_pci_map_notify_cfg(struct virtio_pci_dev *vpci_dev, u8 pos)
{
    struct pci_dev *pdev = vpci_dev->pdev; 
    struct virtio_pci_notify_cap notify_cap = {0}; 
    void __iomem *bar_base;  /*ptr to mapped BAR region */ 
    void __iomem *notify_base; 
    int ret; 

    /*read entire virtio_pci_notify_cap structure (20 bytes)
     * 16 bytes : virtio_pci_cap
     * 4 bytes : notify_off_multiplier*/

    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_VNDR_OFFSET,
                                (u32 *)&notify_cap.cap);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to read notify cfg at 0x%x (vendor fields)\n",
                pos + VIRTIO_PCI_CAP_VNDR_OFFSET);
        return ret;
    }
    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_BAR_OFFSET,
                                (u32 *)&notify_cap.cap.bar);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to read notify cfg at 0x%x (BAR fields)\n",
                pos + VIRTIO_PCI_CAP_BAR_OFFSET);
        return ret;
    }
    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_OFFSET_OFFSET,
                                &notify_cap.cap.offset);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to read notify cfg at 0x%x (offset field)\n",
                pos + VIRTIO_PCI_CAP_OFFSET_OFFSET);
        return ret;
    }
    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_LENGTH_OFFSET,
                                &notify_cap.cap.length);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to read notify cfg at 0x%x (length field)\n",
                pos + VIRTIO_PCI_CAP_LENGTH_OFFSET);
        return ret;
    }
    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_LENGTH_OFFSET + 4,
                                &notify_cap.notify_off_multiplier);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to read notify cfg at 0x%x (multiplier field)\n",
                pos + VIRTIO_PCI_CAP_LENGTH_OFFSET + 4);
        return ret;
    }

    notify_cap.cap.offset = le32_to_cpu(notify_cap.cap.offset);
    notify_cap.cap.length = le32_to_cpu(notify_cap.cap.length); 
    notify_cap.notify_off_multiplier = le32_to_cpu(notify_cap.notify_off_multiplier);

    /*validate bar index */ 
    if(notify_cap.cap.bar >= PCI_STD_NUM_BARS)
    {
        dev_err(&pdev->dev, "Invalid BAR index %d for notify config capablilty\n",
                notify_cap.cap.bar); 
        return -EINVAL; 
    }

    /*validate the length(must be sufficent for queue notifications 
     * each queue notifcation register is u16*/

    if(notify_cap.cap.length < sizeof(u16))
    {
        dev_err(&pdev->dev, "Notify cfg capability length %d too small\n",
                notify_cap.cap.length);
        return -EINVAL;
    }

    /*validate offse + lenght doesn't excced BAR size */ 
    if((notify_cap.cap.offset + notify_cap.cap.length) > pci_resource_len(pdev, notify_cap.cap.bar))
    {
        dev_err(&pdev->dev, "Notify cfg excced beyopng BAR %bounds\n",
                notify_cap.cap.bar); 
        return -EINVAL; 
    }

    bar_base = pci_ioremap_bar(pdev, notify_cap.cap.bar); 
    if(!bar_base)
    {
        dev_err(&pdev->dev, "Failed to map BAR %d for notify cfg\n", 
                notify_cap.cap.bar); 
        return -ENOMEM; 
    }

    /*allocate for notify capablilty structure */ 
    if(vpci_dev->notify_cap)
    {
        dev_warn(&pdev->dev, "Overwriting existing notify cfg capablilty\n"); 
        kfree(vpci_dev->notify_cap); 
    }

    vpci_dev->notify_cap = kmalloc(sizeof(*vpci_dev->notify_cap), GFP_KERNEL); 
    if(!vpci_dev->notify_cap)
    {
        iounmap(bar_base); 
        return -ENOMEM; 
    }

    *vpci_dev->notify_cap = notify_cap;

    notify_base = bar_base + notify_cap.cap.offset; 
    vpci_dev->notify_cap_base = bar_base;
    vpci_dev->notify_base =  notify_base; 

    /*verify if mapped region is accessible */ 
    if (ioread16(vpci_dev->notify_base) == 0xFFFF)
    {
        dev_err(&pdev->dev, "Notify cfg region at BAR %d offset 0x%x is invalid\n",
                notify_cap.cap.bar, notify_cap.cap.offset);
        kfree(vpci_dev->notify_cap);
        vpci_dev->notify_cap = NULL;
        vpci_dev->notify_cap_base = NULL;
        vpci_dev->notify_base =  NULL;
        iounmap(bar_base);
        return -EIO;
    }

    return 0;
}

static int virtio_pci_map_isr_cfg(struct virtio_pci_dev *vpci_dev, u8 pos)
{
    struct pci_dev *pdev = vpci_dev->pdev;    // PCI device pointer
    struct virtio_pci_cap cap = {0};        // Initialize capability structure
    void __iomem *bar_base;                     // Pointer to mapped BAR region
    int ret;

    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_VNDR_OFFSET, (u32 *)&cap);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to read ISR cfg at 0x%x (vendor fields)\n",
                pos + VIRTIO_PCI_CAP_VNDR_OFFSET);
        return ret;
    }
    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_BAR_OFFSET, (u32 *)&cap.bar);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to read ISR cfg at 0x%x (BAR fields)\n",
                pos + VIRTIO_PCI_CAP_BAR_OFFSET);
        return ret;
    }
    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_OFFSET_OFFSET, &cap.offset);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to read ISR cfg at 0x%x (offset field)\n",
                pos + VIRTIO_PCI_CAP_OFFSET_OFFSET);
        return ret;
    }
    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_LENGTH_OFFSET, &cap.length);
    if (ret)
    {
        dev_err(&pdev->dev, "Failed to read ISR cfg at 0x%x (length field)\n",
                pos + VIRTIO_PCI_CAP_LENGTH_OFFSET);
        return ret;
    }

    // validate the capability type
    if (cap.cfg_type != VIRTIO_PCI_CAP_ISR_CFG) {
        dev_err(&pdev->dev, "Expected ISR cfg capability at 0x%x, got type %d\n",
                pos, cap.cfg_type);
        return -EINVAL;
    }

    if (cap.length < sizeof(struct virtio_pci_cap)) {
        dev_err(&pdev->dev, "ISR cfg capability at 0x%x has invalid length %d\n",
                pos, cap.cap_len);
        return -EINVAL;
    }

    cap.offset = le32_to_cpu(cap.offset);
    cap.length = le32_to_cpu(cap.length);

    if (cap.bar >= PCI_STD_NUM_BARS) {
        dev_err(&pdev->dev, "Invalid BAR index %d for ISR cfg capability\n", cap.bar);
        return -EINVAL;
    }

    /* validate the length (must be sufficient for struct virtio_pci_isr_data) */ 
    if (cap.length < 4 )
    {
        dev_err(&pdev->dev, "ISR cfg capability length %d too small\n", cap.length);
        return -EINVAL;
    }

    bar_base = pci_ioremap_bar(pdev, cap.bar);
    if (!bar_base) {
        dev_err(&pdev->dev, "Failed to map BAR %d for ISR cfg\n", cap.bar);
        return -ENOMEM;
    }

    vpci_dev->isr_bar_base = bar_base; 

    vpci_dev->isr_data = bar_base + cap.offset;

    if (ioread32(vpci_dev->isr_data) == 0xFFFFFFFF)
    {
        dev_err(&pdev->dev, "ISR cfg region at BAR %d offset 0x%x is invalid\n",
                cap.bar, cap.offset);

        iounmap(bar_base);
        vpci_dev->isr_data = NULL;
        vpci_dev->isr_bar_base = NULL; 
        return -EIO;
    }

    return 0;
}

static irqreturn_t virtio_pci_interrupt(int irq, void *data)
{
    struct virtio_pci_dev *vpci_dev = data; 
    u32 isr_status; 

    isr_status = ioread32(vpci_dev->isr_data); 

    /*queue interrput */ 
    if(isr_status & 0x1)
    {
        dev_dbg(&vpci_dev->pdev->dev, "Queue interrput triggered\n"); 

        /*if device is a network card */ 
        if(vpci_dev->virtio_dev.id.device == PCI_DEVICE_ID_VIRTIO_NET)
        {
            struct virtio_net_dev *vnet_dev = vpci_dev->virtio_dev.priv; 
            virtio_net_receive(vnet_dev);  
        }
    }

    /*configuration interrput */ 
    if(isr_status & 0x2)
    {
        u8 device_status = ioread8(&vpci_dev->common_cfg->device_status); 
        dev_dbg(&vpci_dev->pdev->dev, "configuration interrput triggered, status : 0x%x\n", 
                device_status); 
        /* TODO: 
         * handle configuration change 
         */ 
    }

    /*handle device-specific interrput (if any)*/ 
    if(isr_status & ~0x3)
    {
        dev_dbg(&vpci_dev->pdev->dev, "Device-specific interrput status: 0x%x\n", 
                isr_status & ~0x3);  
        /* TODO: 
         * handle device-specific interrput */ 
    }

    iowrite32(isr_status, vpci_dev->isr_data); 

    return IRQ_HANDLED; 
}

static int virtio_pci_map_device_cfg(struct virtio_pci_dev *vpci_dev, u8 pos)
{
    struct pci_dev *pdev = vpci_dev->pdev;    
    struct virtio_pci_cap cap = {0};    
    void __iomem *bar_base;                     
    int ret;

    /* Read the entire virtio_pci_cap structure (16 bytes) */ 
    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_VNDR_OFFSET, (u32 *)&cap);
    if (ret)
    {
        dev_err(&pdev->dev, "Failed to read device cfg at 0x%x (vendor fields)\n",
                pos + VIRTIO_PCI_CAP_VNDR_OFFSET);
        return ret;
    }

    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_BAR_OFFSET, (u32 *)&cap.bar);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to read device cfg at 0x%x (BAR fields)\n",
                pos + VIRTIO_PCI_CAP_BAR_OFFSET);
        return ret;
    }

    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_OFFSET_OFFSET, &cap.offset);
    if (ret)
    {
        dev_err(&pdev->dev, "Failed to read device cfg at 0x%x (offset field)\n",
                pos + VIRTIO_PCI_CAP_OFFSET_OFFSET);
        return ret;
    }

    ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_LENGTH_OFFSET, &cap.length);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to read device cfg at 0x%x (length field)\n",
                pos + VIRTIO_PCI_CAP_LENGTH_OFFSET);
        return ret;
    }

    /* validate the capability type */ 
    if (cap.cfg_type != VIRTIO_PCI_CAP_DEVICE_CFG) 
    {
        dev_err(&pdev->dev, "Expected device cfg capability at 0x%x, got type %d\n",
                pos, cap.cfg_type);
        return -EINVAL;
    }

    /* validate the capability length */ 
    if (cap.cap_len < sizeof(struct virtio_pci_cap))
    {
        dev_err(&pdev->dev, "Device cfg capability at 0x%x has invalid length %d\n",
                pos, cap.cap_len);
        return -EINVAL;
    }

    /* convert little-endian fields to host endianness */ 
    cap.offset = le32_to_cpu(cap.offset);
    cap.length = le32_to_cpu(cap.length);

    /* validate the BAR index */ 
    if (cap.bar >= PCI_STD_NUM_BARS) 
    {
        dev_err(&pdev->dev, "Invalid BAR index %d for device cfg capability\n", cap.bar);
        return -EINVAL;
    }

    /* validate the length (must be non-zero; exact size depends on device type) */ 
    if (cap.length == 0)
    {
        dev_err(&pdev->dev, "Device cfg capability length is zero\n");
        return -EINVAL;
    }

    /* map the BAR region specified in the capability */ 
    bar_base = pci_ioremap_bar(pdev, cap.bar);
    if (!bar_base)
    {
        dev_err(&pdev->dev, "Failed to map BAR %d for device cfg\n", cap.bar);
        return -ENOMEM;
    }

    vpci_dev->device_cfg_base = bar_base; 

    /* store the mapped device-specific configuration region address */ 
    vpci_dev->device_cfg = bar_base + cap.offset;

    /* verify that the mapped region is accessible (optional)
     * Note: Device-specific validation depends on the VirtIO device type */ 
    if (ioread32(vpci_dev->device_cfg) == 0xFFFFFFFF) 
    {
        dev_err(&pdev->dev, "Device cfg region at BAR %d offset 0x%x is invalid\n",
                cap.bar, cap.offset);
        iounmap(bar_base);
        vpci_dev->device_cfg = NULL;
        vpci_dev->device_cfg_base = NULL;
        return -EIO;
    }

    return 0;
}

static int virtio_pci_find_caps(struct virtio_pci_dev *vpci_dev)
{
    struct pci_dev *pdev = vpci_dev->pdev; 
    u8 pos; 
    int ret; 

    /*PCI_CAP_ID_VNDR (0x09)- indiactes vendor specofoc capablilty
     * returns offset if the capablilty in the configuration space */
    pos = pci_find_capability(pdev, PCI_CAP_ID_VNDR); 
    if(!pos)
    {
        dev_err(&pdev->dev, "No vendor-specific capablilty found\n");
         return -EINVAL; 
    }

    while(pos)
    {
        struct virtio_pci_cap cap;
        u8 cfg_type; 

        /*read config space at POS, and store into cap 
         * read first 4 bytes , which includes 
         * (u8)cap_vndr
         * (u8)cap_next
         * (u8)cap_len
         * (u8)cfg_type*/ 
        ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_VNDR_OFFSET, (u32 *)&cap);
        if(ret)
            goto read_error; 

        ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_BAR_OFFSET, (u32 *)&cap.bar); 
        if(ret)
            goto read_error; 
        
        ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_OFFSET_OFFSET, &cap.offset); 
        if(ret)
            goto read_error; 

        ret = pci_read_config_dword(pdev, pos + VIRTIO_PCI_CAP_LENGTH_OFFSET, &cap.length); 
        if(ret)
            goto read_error;

        if(cap.cap_len < sizeof(struct virtio_pci_cap))
        {
            dev_err(&pdev->dev, "Capablilty at 0x%x has invalid length %d\n", pos, cap.cap_len); 
            return -EINVAL; 
        }

        cfg_type = cap.cfg_type; 
        cap.offset = le32_to_cpu(cap.offset); 
        cap.length = le32_to_cpu(cap.length);

        switch(cfg_type)
        {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                ret  = virtio_pci_map_common_cfg(vpci_dev, pos); 
                break; 

            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                ret = virtio_pci_map_notify_cfg(vpci_dev, pos);
                break; 

            case VIRTIO_PCI_CAP_ISR_CFG:
                ret = virtio_pci_map_isr_cfg(vpci_dev, pos);
                break;
            
            case VIRTIO_PCI_CAP_DEVICE_CFG: 
                ret = virtio_pci_map_device_cfg(vpci_dev, pos); 
                break;

            default:
                dev_dbg(&pdev->dev, "Ignoring capablilty type %d\n", cfg_type); 
                break; 
        }
        if(ret)
        {
            dev_err(&pdev->dev, "Failed to map capablilty type %d\n", cfg_type); 
            return ret; 
        }
        pos = cap.cap_next; 
        continue; 

read_error:
        dev_err(&pdev->dev, "Failed to read capablilty at 0x%x\n", pos); 
        return ret; 
    }
    if(!vpci_dev->common_cfg)
    {
        dev_err(&pdev->dev, "Common config not found\n"); 
        return -EINVAL; 
    }
    return 0; 

}

static int virtio_pci_setup_interrupts(struct virtio_pci_dev *vpci_dev)
{
    struct pci_dev *pdev = vpci_dev->pdev; 
    int ret; 

    /*allocate IRQ vector */ 
    ret = pci_alloc_irq_vectors(pdev, VIRTIO_PCI_MIN_VECTORS, VIRTIO_PCI_MAX_VECTORS, 
                                PCI_IRQ_MSI | PCI_IRQ_LEGACY);
    if(ret < 0)
    {
        dev_err(&pdev->dev, "Failed to allocate IRQ vectors: %d\n", ret); 
        return ret; 
    }

    /*register interrput handler */ 
    ret = request_irq(pci_irq_vector(pdev, 0), virtio_pci_interrupt, IRQF_SHARED,
                      "virtio-pci", vpci_dev);
    if(ret)
    {
        dev_err(&pdev->dev, "Failed to request IRQ %d\n", ret); 
        pci_free_irq_vectors(pdev); 
        return ret; 
    }

    return 0; 
}

static void virtio_pci_cleanup_interrupts(struct virtio_pci_dev *vpci_dev)
{
    struct pci_dev *pdev = vpci_dev->pdev; 

    /*unregister interrput handler */ 
    free_irq(pci_irq_vector(pdev, 0), vpci_dev); 
    pci_free_irq_vectors(pdev); 
}


static int virtio_pci_enable_device(struct virtio_pci_dev *vpci_dev)
{
    struct virtio_device *vdev = &vpci_dev->virtio_dev;
    u8 status;
    u32 device_features, guest_features;

    /* acknowledge device */
    status = ioread8(&vpci_dev->common_cfg->device_status);
    iowrite8(status | VIRTIO_CONFIG_S_ACKNOWLEDGE,
             &vpci_dev->common_cfg->device_status);

    /* set driver status */
    status = ioread8(&vpci_dev->common_cfg->device_status);
    iowrite8(status | VIRTIO_CONFIG_S_DRIVER,
             &vpci_dev->common_cfg->device_status);

    /* read device features */
    device_features = ioread32(&vpci_dev->common_cfg->device_feature);

    /* select features we want */
    guest_features = device_features & (1ULL<< VIRTIO_F_VERSION_1);
    vdev->features = guest_features;

    /* write accepted features to guest_feature */
    iowrite32(guest_features, &vpci_dev->common_cfg->guest_feature);

    /* features OK */
    status = ioread8(&vpci_dev->common_cfg->device_status);
    iowrite8(status | VIRTIO_CONFIG_S_FEATURES_OK,
             &vpci_dev->common_cfg->device_status);

    /* check features OK */
    status = ioread8(&vpci_dev->common_cfg->device_status);
    if (!(status & VIRTIO_CONFIG_S_FEATURES_OK)) {
        dev_err(&vpci_dev->pdev->dev, "Failed to negotiate features\n");
        return -EINVAL;
    }

    /* set driver OK */
    iowrite8(status | VIRTIO_CONFIG_S_DRIVER_OK,
             &vpci_dev->common_cfg->device_status);

    return 0;
}

static int virtio_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct virtio_pci_dev *vpci_dev; 
    struct virtio_net_dev *vnet_dev = NULL;
    int ret; 

    vpci_dev = kzalloc(sizeof(struct virtio_pci_dev), GFP_KERNEL); 
    if(!vpci_dev)
    {
        dev_err(&pdev->dev, "Failed to allocate memory for virtio_pci_dev\n"); 
        return -ENOMEM; 
    }

    /*link  virtio device as child of physical pci device*/ 
    vpci_dev->virtio_dev.dev.parent = &pdev->dev;
    vpci_dev->virtio_dev.id.device = id->device; 
    vpci_dev->virtio_dev.id.vendor = PCI_VENDOR_ID_VIRTIO; 
    vpci_dev->virtio_dev.config = &virtio_pci_config_ops;
    vpci_dev->virtio_dev.priv = vpci_dev; 

    ret = pci_enable_device(pdev); 
    if(ret)
    {
        dev_err(&pdev->dev, "Failed to enable PCI device\n"); 
        goto err_free_dev; 
    }

    ret = pci_request_regions(pdev, "virtio-pci"); 
    if(ret)
    {
        dev_err(&pdev->dev, "Failed to requeat PCI regions\n"); 
        goto err_disable_dev; 
    }

    /*set address type for dma */ 
    ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(64)); 
    if(ret)
    {
        ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32)); 
        if(ret)
        {
            dev_err(&pdev->dev, "Failed to set DMA mask\n"); 
            goto err_release_regions; 
        }
    }

    /*dma mask for buffers that remain mapped */ 
    pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64)); 
    
    /*map VIRTIO capablilties */ 
    ret = virtio_pci_find_caps(vpci_dev); 
    if(ret)
    {
        dev_err(&pdev->dev, "Failed to map Virtio capablilties\n"); 
        goto err_release_regions; 
    }

    /*set up interrputs for VIRTIO device */ 
    ret = virtio_pci_setup_interrupts(vpci_dev);
    if(ret)
    {
        dev_err(&pdev->dev, "Failed to set up interrputs on VIRTIO device\n"); 
        goto err_cleanup_caps; 
    }

    /*set up 3 virtqueues (RX, TX, CTRL) */
    ret = virtio_pci_find_vqs(&vpci_dev->virtio_dev, 3, vpci_dev->vqs, 
                              (vq_callback_t *[]){virtio_net_receive, NULL, NULL}, 
                              (const char*[]){"rx", "tx", "ctrl"}, NULL, NULL);

    if(ret)
    {
        dev_err(&pdev->dev, "Failed to set up virtqueues\n"); 
        goto err_cleanup_interrupts; 
    }

    /*enable virtio device by setting status bits */ 
    ret = virtio_pci_enable_device(vpci_dev);
    if(ret)
    {
        dev_err(&pdev->dev, "Failed to enable VIRTIO device\n"); 
        goto err_cleanup_vqs;  
    }

    /*check if device is virtio-net device (ID 0x100)*/ 
    if(id->device == PCI_DEVICE_ID_VIRTIO_NET)
    {
        ret = virtio_net_init(vpci_dev); 
        if(ret)
        {
            dev_err(&pdev->dev, "Failed to register VirtIO device\n"); 
            goto err_cleanup_device; 
        }
        vnet_dev = vpci_dev->virtio_dev.priv; 
    }

    ret = register_virtio_device(&vpci_dev->virtio_dev); 
    if(ret)
    {
        dev_err(&pdev->dev, "Failed to register VIRTIO device\n"); 
        goto err_cleanup_net; 
    }

    /*store vpci_dev as driver data for PCI device */ 
    pci_set_drvdata(pdev, vpci_dev);
    
    dev_info(&pdev->dev, "VIRTIO PCI device probed, ID 0x%04x\n", id->device); 

    return 0; 

err_cleanup_net:
    if (vnet_dev)
        virtio_net_exit(vnet_dev);

err_cleanup_device:
    iowrite8(0, &vpci_dev->common_cfg->device_status);

err_cleanup_vqs:
    virtio_pci_del_vqs(&vpci_dev->virtio_dev);

err_cleanup_interrupts:
    virtio_pci_cleanup_interrupts(vpci_dev);

err_cleanup_caps:
    if (vpci_dev->device_cfg)
        iounmap(vpci_dev->device_cfg);
    if (vpci_dev->isr_data)
        iounmap(vpci_dev->isr_data);
    if (vpci_dev->notify_cap) {
        iounmap((void __iomem *)(unsigned long)vpci_dev->notify_cap->cap.offset);
        kfree(vpci_dev->notify_cap);
    }
    if (vpci_dev->common_cfg)
        iounmap(vpci_dev->common_cfg);

err_release_regions:
    pci_release_regions(pdev);

err_disable_dev:
    pci_disable_device(pdev);

err_free_dev:
    kfree(vpci_dev);
    return ret;
}

static void virtio_pci_remove(struct pci_dev *pdev)
{
    struct virtio_pci_dev *vpci_dev = pci_get_drvdata(pdev); 
    struct virtio_net_dev *vnet_dev = vpci_dev->virtio_dev.priv;

    /* unregister virtio device */
    unregister_virtio_device(&vpci_dev->virtio_dev); 

    /* clean up virtio-net specific device */
    if (vpci_dev->virtio_dev.id.device == PCI_DEVICE_ID_VIRTIO_NET && vnet_dev)
        virtio_net_exit(vnet_dev); 

    /* reset the device */
    if (vpci_dev->common_cfg)
        iowrite8(VIRTIO_CONFIG_S_RESET, &vpci_dev->common_cfg->device_status);

    /* delete virtqueues */
    virtio_pci_del_vqs(&vpci_dev->virtio_dev); 

    /* cleanup interrupts */
    virtio_pci_cleanup_interrupts(vpci_dev);

    /* unmap device config */
    if (vpci_dev->device_cfg) {
        iounmap(vpci_dev->device_cfg); 
        vpci_dev->device_cfg = NULL;
        vpci_dev->device_cfg_base = NULL;
    }

    /* unmap ISR */
    if (vpci_dev->isr_data) {
        iounmap(vpci_dev->isr_data); 
        vpci_dev->isr_data = NULL;
        vpci_dev->isr_bar_base = NULL;
    }

    /* unmap notify capability */
    if (vpci_dev->notify_cap) {
        if (vpci_dev->notify_cap_base)
            iounmap(vpci_dev->notify_cap_base); 
        kfree(vpci_dev->notify_cap);
        vpci_dev->notify_cap = NULL;
        vpci_dev->notify_cap_base = NULL;
        vpci_dev->notify_base = NULL;
    }

    /* unmap common config */
    if (vpci_dev->common_cfg) {
        iounmap(vpci_dev->common_cfg); 
        vpci_dev->common_cfg = NULL;
        vpci_dev->common_cfg_base = NULL;
    }

    /* release PCI resources */
    pci_release_regions(pdev); 
    pci_disable_device(pdev); 

    /* finally free the virtio_pci_dev struct */
    kfree(vpci_dev); 
}

static struct pci_driver virtio_pci_driver = {
    .name = "virtio-pci", 
    .id_table = virtio_pci_id_table, 
    .probe  = virtio_pci_probe, 
    .remove = virtio_pci_remove, 
};

module_pci_driver(virtio_pci_driver);
MODULE_LICENSE("GPL"); 
MODULE_DESCRIPTION("VirtIO PCI driver for VirtIO 1.2 devices");
MODULE_AUTHOR("Chrinoic M");
