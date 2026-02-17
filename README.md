# aOS

**A bare metal operating system built from scratch in C and x86 assembly.**

[![Version](https://img.shields.io/badge/version-0.9.1-blue.svg)](https://github.com/axrxvm/aos)
[![License](https://img.shields.io/badge/license-CC%20BY--NC%204.0-orange.svg)](LICENSE)
[![Architecture](https://img.shields.io/badge/arch-i386%20%7C%20x86__64-green.svg)](https://github.com/axrxvm/aos)

## Project Links

- **Releases**: <https://github.com/axrxvm/aos/releases>
- **Issues**: <https://github.com/axrxvm/aos/issues>
- **Contributing Guide**: [CONTRIBUTING.md](CONTRIBUTING.md)
- **Security Policy**: [SECURITY.md](SECURITY.md)
- **License**: [LICENSE](LICENSE)
- **Code of Conduct**: [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)

---

## Overview

aOS is a bare-metal, multiboot-compliant operating system kernel engineered without relying on any external libraries or existing operating system services. It targets `i386` and `x86_64`, and demonstrates comprehensive low-level system programming concepts including memory management, process scheduling, virtual file systems, networking stacks, and device drivers, all implemented from first principles.

### Key Capabilities

- **Full TCP/IP networking stack** with Ethernet, ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS, HTTP, FTP, and TLS support
- **Multiple filesystem implementations**: ramfs (volatile), SimpleFS, FAT32, devfs, procfs with VFS abstraction
- **Advanced memory management**: Physical Memory Manager (PMM), two-level paging, Virtual Memory Manager with 2MB kernel heap
- **Process management**: Multi-tasking support, fork/exec semantics, priority scheduling, IPC mechanisms
- **Security features**: User/group authentication with SHA-256 password hashing, file permissions, sandboxing ("Cage" system)
- **Kernel modules**: Dynamic `.akm` module loading with bytecode VM execution and API bindings
- **Hardware support**: ATA/IDE PIO, E1000/PCnet NICs, PS/2 keyboard/mouse, VGA text mode, serial console, PCI enumeration
- **Package management**: aOS Package Manager (APM) with HTTP-based repository and SHA-256 integrity verification
- **Init system**: Service management with runlevels, dependency resolution, and automatic restart

## Building & Running

### Prerequisites

**Required tools:**

- GCC with 32-bit multilib support (`gcc-multilib`)
- NASM assembler
- GNU ld linker
- GRUB tools (`grub-mkrescue`, `xorriso`)
- QEMU emulator (`qemu-system-i386` and/or `qemu-system-x86_64`)

**Optional (for TAP networking):**

- bridge-utils, iptables, dnsmasq, iproute2

**Automated setup (Ubuntu/Debian):**

```bash
sudo ./scripts/get-ready.sh
```

### Build Commands

```bash
# Build ISO image
make iso

# Build and run with VGA + serial output
make run

# Build and run x86_64
make ARCH=x86_64 run

# Run with serial console only (no graphics)
make run-nographic

# Run with debugging (guest errors, unimplemented features)
make run-debug

# Run with 50MB disk image
make run-s

# Run with disk + TAP networking (requires sudo)
make run-sn

# Run with disk + user-mode networking (no sudo)
make run-sn-user

# Clean build artifacts
make clean

# Show architecture info
make arch-info
```

## License

Copyright (c) 2024-2026 Aarav Mehta

This project is licensed under the **Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)** license.

**You are free to:**

- **Share**: Copy and redistribute in any medium or format
- **Adapt**: Remix, transform, and build upon the material

**Under the following terms:**

- **Attribution**: Give appropriate credit, link to license, indicate changes
- **NonCommercial**: No commercial use without permission
- **No additional restrictions**: No legal/tech measures that restrict others

Full license text: <https://creativecommons.org/licenses/by-nc/4.0/>

**SPDX-License-Identifier:** CC-BY-NC-4.0

---

## Contributing

We welcome contributions from experienced systems programmers. Please read [CONTRIBUTING.md](CONTRIBUTING.md) for:

- Build setup requirements
- Coding style and conventions
- PR submission guidelines
- Testing expectations

**Quick Start for Contributors:**

1. Fork repository
2. Create feature branch
3. Follow coding standards (see above)
4. Test thoroughly in QEMU
5. Submit PR with detailed description

**Security Issues:**
Report privately to maintainers, not via public issues. See [SECURITY.md](SECURITY.md).

---

## Acknowledgments

**Built from scratch without external libraries.**

Inspired by:

- **OSDev Wiki**: Comprehensive OS development resources
- **Intel/AMD Manuals**: x86 architecture specifications
- **POSIX Standards**: System call semantics
- **Linux Kernel**: Design patterns and best practices

Special thanks to:

- GRUB developers for multiboot specification
- QEMU project for excellent emulation
- GCC/NASM toolchain maintainers

---

## Contact & Resources

- **Repository**: [https://github.com/axrxvm/aos](https://github.com/axrxvm/aos)
- **Package Repository**: [https://github.com/axrxvm/aos-repo/](https://github.com/axrxvm/aos-repo/)
- **Issues**: [Bug Tracker](https://github.com/axrxvm/aos/issues)

**Maintainer:** Aarav Mehta & the Open Source Community

---

**aOS: A real operating system, built from nothing.**
