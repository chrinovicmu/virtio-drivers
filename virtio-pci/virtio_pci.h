
#ifndef VIRTIO_PCI_H
#define VIRTIO_PCI_H

#include <linux/virtio_pci.h>
#include <linux/virtio_config.h>
#include <linux/virtio.h>
#include <linux/pci.h> 
#include <linux/virtio_pci.h>
#include <linux/spinlock.h> 

/* Offsets for fields in struct virtio_pci_cap */
#define VIRTIO_PCI_CAP_VNDR_OFFSET      0  /* cap_vndr, cap_next, cap_len, cfg_type */
#define VIRTIO_PCI_CAP_BAR_OFFSET       4  /* bar, id, padding[2] */
#define VIRTIO_PCI_CAP_OFFSET_OFFSET    8  /* offset */
#define VIRTIO_PCI_CAP_LENGTH_OFFSET   12  /* length */

#define VIRTIO_PCI_MIN_VECTORS          1 
#define VIRTIO_PCI_MAX_VECTORS          1 

/* Feature selector values */
#define VIRTIO_FSEL_0_31                0x0   /* Select feature bits 0..31 */
#define VIRTIO_FSEL_32_63               0x1   /* Select feature bits 32..63 */
#define VIRTIO_FSEL_64_95               0x2   /* Select feature bits 64..95 (if device supports) */
#define VIRTIO_FSEL_96_127              0x3   /* Select feature bits 96..127 */

#define VIRTIO_NET_QUEUE_RX             0
#define VIRTIO_NET_QUEUE_TX             1
#define VIRTIO_NET_QUEUE_CTRL           2

#define VIRTIO_VIRTQUEUE_ENABLE         1 
#define VIRTIO_VIRTQUEUE_DISABLE        0

#ifndef PCI_VENDOR_ID_VIRTIO
#define PCI_VENDOR_ID_VIRTIO 0x1AF4
#endif

#ifndef PCI_DEVICE_ID_VIRTIO_NET
#define PCI_DEVICE_ID_VIRTIO_NET 0x1000
#endif


/* Driver-specific structure */
struct virtio_pci_dev {
    struct virtio_device virtio_dev;
    struct pci_dev *pdev;
    u64 device_features;    /* device-offered features */
    u64 guest_features;     /* driver-accepted features */

    struct virtio_pci_common_cfg __iomem *common_cfg;
    void __iomem *common_cfg_base; 

    struct virtio_pci_notify_cap *notify_cap;
    void __iomem *notify_cap_base; 

    void __iomem *isr_data; 
    void __iomem *isr_bar_base; 

    void __iomem *device_cfg; 
    void __iomem *device_cfg_base; 

    struct virtqueue **vqs; 
    int num_queues;

    spinlock_t vq_lock; 
};

/* Driver functions */
int virtio_pci_init(struct virtio_pci_dev *vpci_dev);
void virtio_pci_exit(struct virtio_pci_dev *vpci_dev);

#endif // VIRTIO_PCI_H
