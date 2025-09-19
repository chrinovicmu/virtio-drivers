# VirtIO Drivers (Linux Kernel, VirtIO 1.2)

**Status:** ⚠️ Still in development

This repository is a modular framework for developing and testing **Linux VirtIO 1.2 drivers**. It provides:
- Shared PCI capability and common configuration handling.
- Reusable virtqueue infrastructure for device I/O.
- Support for creating multiple device drivers (e.g., network, block, console).

## Features

- Fully modular design for multiple VirtIO device types.
- Handles common VirtIO PCI configuration (common_cfg, notify_cap, ISR).
- Reusable driver skeletons to accelerate development of new VirtIO drivers.
- Designed for Linux kernel module testing and experimentation.