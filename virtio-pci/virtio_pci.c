#include <linux/module.h>
#include <linux/pci.h>
#include <linux/virtio_config.h>
#include "virtio/virtio_pci.h"

/* Offsets for fields in struct virtio_pci_cap */
#define VIRTIO_PCI_CAP_VNDR_OFFSET      0  /* cap_vndr, cap_next, cap_len, cfg_type */
#define VIRTIO_PCI_CAP_BAR_OFFSET       4  /* bar, id, padding[2] */
#define VIRTIO_PCI_CAP_OFFSET_OFFSET    8  /* offset */
#define VIRTIO_PCI_CAP_LENGTH_OFFSET   12  /* length */

#define VIRTIO_PCI_MIN_VECTORS          1 
#define VIRTIO_PCI_MAX_VECTORS          1 

static const struct pci_device_id virtio_pci_id_table[] = {
    {PCI_DEVICE(0x1AF4, PCI_ANY_ID)}, 
    {0}
};
MODULE_DEVICE_TABLE(pci, virtio_pci_id_table); 

static int virtio_pci_map_common_cfg(struct virtio_pci_dev *vpci_dev, u8 pos)
{
    struct pci_dev *pdev = vpci_dev->pdev; 
    struct virtio_pci_cap cap = {0}; 
    void __iomem *base;   
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
         dev_err(&pdev, "Expected common cfg capablilty at 0x%x, got type %d\n", pos, cap.cfg_type); 
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
    base = pci_ioremap_bar(pdev, cap.bar);
    if (!base)
    {
        dev_err(&pdev->dev, "Failed to map BAR %d for common cfg\n", cap.bar);
        return -ENOMEM;
    } 
    
    /*points directly to the common_cfg structure */ 
    vpci_dev->common_cfg = base + cap.offset;

    /*check if memory is mapped */ 
    if(ioread8(&vpci_dev->common_cfg->device_status) == 0xFF)
    {
        dev_err(&pdev->dev, "Common cfg region at BAR %d Offset 0x%x is invaild\n"
                cap.bar, cap.offset); 
        iounmap(base); 
        vpci_dev->common_cfg = NULL; 
        return -EIO; 
    }

    return 0; 
}

static int virtio_pci_map_notify_cfg(struct virtio_pci_dev *vpci_dev, u8 pos)
{
    struct pci_dev *pdev = vpci_dev->pdev; 
    struct virtio_pci_map_notify_cfg notify_cap = {0}; 
    void __iomem *base;  /*ptr to mapped BAR region */ 
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
    notify_cap.cap.notify_off_multiplier = le32_to_cpu(notify_cap.cap.notify_off_multiplier);

    /*validate bar index */ 
    if(notify_cap.cap.bar >= PCI_STD_NUM_BARS)
    {
        dev_err(&pdev->dev, "Invalid BAR index %d for notify config capablilty\n",
                notify_cap.cap.bar); 
        return -EINVAL; 
    }

    /*validate the length(must be sufficent for queue notifications 
     * each queue notifcation register is u16*/)
    if(notify_cap.cap.length < sizeof(u16))
    {
        dev_err(&pdev->dev, "Notify cfg capability length %d too small\n",
                notify_cap.cap.length);
        return -EINVAL;
    }

    base = pci_ioremap_bar(pdev, notify_cap.cap.bar); 
    if(!base)
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
    vpci_dev->notify_cap = kmalloc(sizeof(struct virtio_pci_notify_cap), GFP_KERNEL); 
    if(!vpci_dev->notify_cap)
    {
        iounmap(base); 
        return -ENOMEM; 
    }

    *vpci_dev->notify_cap = notify_cap;
    vpci_dev->notify_cap->cap.offset = (unsigned long )(base + notify_cap.offset); 

    /*verify if mapped region is accessible */ 
    if (ioread16((void __iomem *)(unsigned long)vpci_dev->notify_cap->cap.offset) == 0xFFFF)
    {
        dev_err(&pdev->dev, "Notify cfg region at BAR %d offset 0x%x is invalid\n",
                notify_cap.cap.bar, notify_cap.cap.offset);
        kfree(vpci_dev->notify_cap);
        vpci_dev->notify_cap = NULL;
        iounmap(base);
        return -EIO;
    }
}

static int virtio_pci_map_isr_cfg(struct virtio_pci_dev *vpci_dev, u8 pos)
{
    struct pci_dev *pdev = vpci_dev->pdev;    // PCI device pointer
    struct virtio_pci_cap cap = {0};        // Initialize capability structure
    void __iomem *base;                     // Pointer to mapped BAR region
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

    if (cap.cap_len < sizeof(struct virtio_pci_cap)) {
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
    if (cap.length < sizeof(struct virtio_pci_isr_data)) {
        dev_err(&pdev->dev, "ISR cfg capability length %d too small\n", cap.length);
        return -EINVAL;
    }

    base = pci_ioremap_bar(pdev, cap.bar);
    if (!base) {
        dev_err(&pdev->dev, "Failed to map BAR %d for ISR cfg\n", cap.bar);
        return -ENOMEM;
    }

    vpci_dev->isr_data = base + cap.offset;

    if (ioread32(vpci_dev->isr_data) == 0xFFFFFFFF) {
        dev_err(&pdev->dev, "ISR cfg region at BAR %d offset 0x%x is invalid\n",
                cap.bar, cap.offset);
        iounmap(base);
        vpci_dev->isr_data = NULL;
        return -EIO;
    }
    return 0;
}

static irqreturn_t virtio_pci_interupt(int irq, void *data)
{
    struct virtio_pci_dev *vpci_dev = data; 
    struct virtio_pci_isr_data *isr = vpci_dev->isr_data; 
    u32 isr_status; 

    isr_status = ioread32(vpci_dev->isr_data); 

    /*queue interrput */ 
    if(isr->queue_intr)
    {
        dev_dbg(&vpci_dev->pdev->dev, "Queue interrput triggered\n"); 

        /*if device is a network card */ 
        if(vpci_dev->virtio_dev.id.device == PCI_DEVICE_ID_VIRTIO_NET)
        {
            struct virtio_net_dev *vnet_dev = vpci_dev->virtio_dev.priv; 
            virtio_net_receive_packet(vnet_dev, NULL, NULL);  
        }
    }

    /*configuration interrput */ 
    if(isr->config_intr)
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

    return IRQ_HANDLED 
}

static int virtio_pci_map_device_cfg(struct virtio_pci_dev *vpci_dev, u8 pos)
{
    struct pci_dev *pdev = vpci_dev->pdev;    
    struct virtio_pci_cap cap = {0};    
    void __iomem *base;                     
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
    base = pci_ioremap_bar(pdev, cap.bar);
    if (!base)
    {
        dev_err(&pdev->dev, "Failed to map BAR %d for device cfg\n", cap.bar);
        return -ENOMEM;
    }

    /* store the mapped device-specific configuration region address */ 
    vpci_dev->device_cfg = base + cap.offset;

    /* verify that the mapped region is accessible (optional)
     * Note: Device-specific validation depends on the VirtIO device type */ 
    if (ioread32(vpci_dev->device_cfg) == 0xFFFFFFFF) 
    {
        dev_err(&pdev->dev, "Device cfg region at BAR %d offset 0x%x is invalid\n",
                cap.bar, cap.offset);
        iounmap(base);
        vpci_dev->device_cfg = NULL;
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

            case VIRTIO_PCI_NOTIFY_CFG:
                ret = virtio_pci_map_notify_cfg(vpci_dev, pos);
                break; 

            case VIRTIO_PCI_ISR_CFG:
                ret = virtio_pci_map_isr_cfg(vpci_dev, pos);
                break;
            
            case VIRTIO_PCI_CAP_DEVICE_CFG; 
                ret = virtio_pci_map_device_cfg(vpci_dev, pos); 
                break;

            default:
                dev_dbg(&pdev->dev, "Ignoring capablilty type %d\n", cfg_type); 
                break 
        }
        if(ret)
        {
            dev_err(&pdev->dev, "Failed to map capablilty type %d"); 
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
    ret = request_irq(pci_irq_vector(pdev, 0), virtio_pci_interupt, IRQF_SHARED,
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

static int virtio_pci_enable(struct virtio_pci_dev *vpci_dev)
{
    struct virtio_device *dev = &vpci_dev->virtio_dev; 
    u8 status; 

    /*acknowledge device */ 
    status = ioread8(&vpci_dev->common_cfg->device_status);
    iowrite8(status | VIRTIO_CONFIG_S_ACKNOWLEDGE, &vpci_dev->common_cfg->device_status); 

     

}
static int virtio_pci_probe(struct pci_dev *pdev, const struct pci_device_id)
{


}



