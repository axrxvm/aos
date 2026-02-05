# aOS

**A bare metal operating system built from scratch in C and x86 assembly.**

[![Version](https://img.shields.io/badge/version-0.8.8-blue.svg)](https://github.com/axrxvm/aos)
[![License](https://img.shields.io/badge/license-CC%20BY--NC%204.0-orange.svg)](LICENSE)
[![Architecture](https://img.shields.io/badge/arch-i386-green.svg)](https://github.com/axrxvm/aos)

---

## Overview

aOS is a bare-metal, multiboot-compliant i386 (as of now) operating system kernel engineered without relying on any external libraries or existing operating system services. It demonstrates comprehensive low-level system programming concepts including memory management, process scheduling, virtual file systems, networking stacks, and device drivers—all implemented from first principles.

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

---

## Architecture

### Memory Map (Identity Mapped 0-8MB)

```
0x000000 - 0x0FFFFF   Low Memory (BIOS, VGA, etc.)
0x100000 - 0x107FFF   Kernel Code/Data (~32KB)
0x500000 - 0x6FFFFF   Kernel Heap (2MB)
0xB8000               VGA Text Buffer
0xC0000000+           Kernel Virtual Space (3GB+)
```

### System Stack

| Layer | Components |
|-------|------------|
| **Shell & Command Registry** | 50+ built-in commands |
| **Application Protocols** | HTTP, FTP, DNS, DHCP, TLS |
| **Transport Layer** | TCP, UDP, Socket API |
| **Network Layer** | IPv4, ICMP, ARP, NAT |
| **Link Layer** | Ethernet, Loopback |
| **Network Drivers** | E1000, PCnet (PCI) |
| **VFS Layer** | Path resolution, vnodes, mounts |
| **Filesystems** | ramfs, SimpleFS, FAT32, devfs, procfs |
| **Storage Drivers** | ATA/IDE PIO (512B sectors) |
| **Process Management** | Scheduling, fork/exec, IPC |
| **Memory Management** | PMM → Paging → VMM |
| **Interrupt System** | IDT, PIC, ISRs, IRQs |
| **Hardware Abstraction** | CPU (GDT/IDT), Devices |

### Boot Sequence

1. **GRUB Multiboot**: Loads kernel at 1MB physical address
2. **CPU Initialization**: GDT setup, segment selectors configuration
3. **Early I/O**: Serial port (COM1) for debug logging
4. **Interrupt Setup**: IDT, PIC configuration, timer (PIT) initialization
5. **Memory Subsystem**: PMM bitmap allocator → Paging tables → VMM heap
6. **PCI Enumeration**: Bus scanning for network cards
7. **ACPI**: Power management interface initialization
8. **Networking**: Protocol stack initialization, NIC driver probing
9. **VFS Initialization**: Register filesystem drivers
10. **Storage**: ATA driver init, disk detection
11. **Root Filesystem Mount**: SimpleFS (if formatted disk) or ramfs fallback
12. **Virtual Filesystems**: Mount devfs at `/dev`, procfs at `/proc`
13. **Filesystem Layout**: Create standard directories (`/bin`, `/home`, `/etc`, etc.)
14. **APM**: Package manager initialization with repository config
15. **User System**: Load user database, create default accounts
16. **Init Services**: Start registered services per runlevel
17. **Shell**: Login prompt or direct shell based on configuration

---

## Features

### Networking Stack

**Complete implementation from Ethernet frames to application protocols:**

- **Link Layer**: Ethernet II framing, loopback interface
- **ARP**: Address Resolution Protocol for IPv4-to-MAC mapping
- **IPv4**: Packet routing, fragmentation, checksum validation
- **ICMP**: Echo request/reply (ping), error reporting
- **UDP**: Connectionless datagram service, socket API
- **TCP**: Full connection-oriented protocol with 3-way handshake, sliding window, retransmission
- **DHCP Client**: Automatic IP configuration (DISCOVER/OFFER/REQUEST/ACK)
- **DNS Resolver**: Domain name resolution with query/response parsing
- **HTTP Client**: GET/POST methods, header parsing, chunked transfer
- **FTP Client**: Command/data channel separation, binary/ASCII modes
- **TLS 1.2**: Handshake protocol, RSA-based key exchange, AES encryption (partial implementation)
- **NAT**: Network Address Translation for port forwarding

**Supported NICs:**

- Intel E1000 (e1000) via MMIO
- AMD PCnet-PCI II / PCnet-FAST III

### Filesystems

#### Virtual File System (VFS)

- Unified interface for all filesystem implementations
- Mount point management with transparent overlay
- Path resolution with `.` and `..` traversal
- Per-process current working directory tracking
- File descriptor table with open/close/read/write/seek operations

#### ramfs

- Volatile in-memory filesystem
- Used as root when no formatted disk present
- Directory hierarchy with dynamic allocation
- File creation, deletion, reading, writing

#### SimpleFS v2

- Disk-backed persistent filesystem
- 512-byte fixed block size aligned with ATA sectors
- Inode-based file/directory metadata
- Linked-list block allocation for files

#### FAT32

- Standard FAT32 implementation with VFAT long filenames
- Compatible with Windows/Linux formatted disks
- Full read/write support with cluster allocation
- Persistent storage for LOCAL mode

#### devfs

- Device node pseudo-filesystem mounted at `/dev`
- Character and block device representation
- Serial ports, null device, zero device

#### procfs

- Process information pseudo-filesystem mounted at `/proc`
- Runtime system statistics
- Kernel state introspection

### Memory Management

#### Physical Memory Manager (PMM)

- Bitmap-based free frame tracking
- 4KB page granularity
- Allocation/deallocation interface for paging system

#### Paging

- Two-level page directory/table architecture
- 4KB pages with present/write/user flags
- Identity mapping for first 8MB (kernel space)
- Page fault handler with copy-on-write support
- TLB flush management

#### Virtual Memory Manager (VMM)

- Kernel heap at `0x500000-0x6FFFFF` (2MB)
- Bump allocator for early boot
- Page-aligned allocation interface
- Future: User-space heap management

### Process Management

**Process Control Blocks (PCB) with:**

- Process ID (PID), parent PID, process name
- State machine: READY, RUNNING, BLOCKED, SLEEPING, ZOMBIE, DEAD
- Priority-based scheduling (5 levels: IDLE to REALTIME)
- CPU context preservation (registers, flags, page directory)
- Separate kernel and user stacks
- Exit status tracking
- Process tree with parent/child/sibling links

**System Calls (19 total):**

- Process: `fork`, `execve`, `exit`, `waitpid`, `getpid`, `kill`, `yield`, `sleep`
- I/O: `open`, `close`, `read`, `write`, `lseek`, `readdir`
- Filesystem: `mkdir`, `rmdir`, `unlink`, `stat`
- Memory: `sbrk`

### Security & Sandboxing

#### User Management

- Multi-user support with UID/GID system
- SHA-256 password hashing (no plaintext storage)
- Root (UID 0) with administrative privileges
- User database persistence on formatted disk
- Login shell with authentication
- Default user: `root`

#### Cage System (Sandboxing)

aOS's equivalent to chroot jails with enhanced isolation:

- **5 Isolation Levels**: NONE, LIGHT, STANDARD, STRICT, LOCKED
- **Syscall Filtering**: Granular permission bitmasks (I/O, process, memory, network, device, IPC)
- **Resource Limits**: Max memory, open files, child processes, CPU time
- **Root Caging**: Chroot-like directory isolation
- **Immutability**: Read-only cages, no-exec enforcement
- **Predefined Profiles**: Minimal, Standard, Trusted, System

#### File Permissions

- Owner-based access control (read, write, execute)
- UID/GID association per file/directory
- Permission checking on VFS operations

### Kernel Modules

**Dual Module System:**

#### Traditional .akm Modules (v1)

- ELF-like binary format with magic number `0x004D4B41`
- Init/cleanup entry points
- Dependency declaration (up to 4 dependencies)
- Version compatibility checking
- Runtime loading/unloading via commands

#### Bytecode Modules (AKM VM)

- Stack-based virtual machine with 70+ opcodes
- Compiled from high-level language via `akmcc`
- Arithmetic, bitwise, comparison, control flow, memory operations
- Kernel API bindings (35+ functions): logging, memory allocation, I/O, networking
- Safe execution with validation
- No direct hardware access (sandboxed)

**Module Operations:**

- `modlist` - List loaded modules
- `modload <path>` - Load module
- `modunload <name>` - Unload module
- `kernelver` - Display kernel version

### aOS Package Manager (APM)

**HTTP-based package distribution system:**

- **Repository**: `http://repo.aosproject.workers.dev/main/i386`
- **Package Format**: APM currently supports managing   `.akm` kernel modules with JSON metadata
- **Integrity**: SHA-256 checksum verification
- **Caching**: Local repository list at `/sys/apm/kmodule.list.source`
- **Storage**: Modules installed to `/sys/apm/modules/`

**Commands:**

```bash
apm update                        # Fetch repository listing
apm kmodule list                  # Show available modules
apm kmodule list --installed      # Show installed modules
apm kmodule info <name>           # Display module details
apm kmodule install <name>        # Download, verify, install (alias: i)
apm kmodule remove <name>         # Uninstall module (alias: u)
```

### Device Drivers

**Storage:**

- ATA/IDE PIO mode (primary master/slave, secondary master/slave)
- 512-byte sector I/O
- LBA28 addressing
- Read/write operations with polling

**Network:**

- Intel E1000 (PCI, MMIO-based, interrupt-driven RX/TX)
- AMD PCnet (PCI, port I/O, descriptor rings)

**Input:**

- PS/2 Keyboard (scancode to ASCII translation, polling mode)
- PS/2 Mouse (3-button support, movement tracking)

**Display:**

- VGA Text Mode 80x25
- 16-color palette support
- Cursor positioning, scrolling
- Color attribute control

**Serial:**

- COM1-COM4 ports
- 115200 baud default
- Used for kernel debug logging

**PCI:**

- Configuration space access
- Bus/device/function enumeration
- BAR (Base Address Register) parsing for MMIO

### Cryptography

**Implemented from scratch (no external libraries):**

- **SHA-256**: Hashing for passwords, file integrity, package verification
- **AES-128/256**: Block cipher with CBC mode
- **HMAC**: Message authentication codes
- **RSA**: Public-key cryptography with PKCS#1 v1.5 padding
- **BigInt**: Arbitrary-precision integer arithmetic for RSA
- **X.509**: Certificate parsing for TLS (basic support)

### Init System

**SysV-inspired service management:**

- **Runlevels**: BOOT (0), SINGLE (1), MULTI (2), SHUTDOWN (3)
- **Service Types**: SYSTEM, DAEMON, ONESHOT
- **Auto-restart**: Watchdog monitoring for failed services
- **Priority-based startup**: Dependency-aware initialization order

**Service Operations:**

- Register services with start/stop callbacks
- Start/stop/restart individual services
- List service status
- Runlevel transitions

### Shell & Commands

**Built-in shell with 50+ commands organized by category:**

#### System

```bash
help [category]          # Display all commands by category
version                  # Show OS version and build information
clear                    # Clear screen and reset cursor
echo [-n] [-e] [-c] <text> # Print text (-n: no newline, -e: escapes, -c: clear)
uptime                   # System uptime in ticks
reboot                   # Warm reboot
halt                     # Halt CPU
shutdown [-c] [+sec|now] # Power off (default: 20s, -c: cancel)
poweroff [-c] [+sec|now] # Alias for shutdown
```

#### Filesystem

```bash
lst [path]               # List directory contents
view <file>              # Display file contents
edit <file>              # Edit file in text editor
create <file> [--empty]  # Create new file
write <file> <content>   # Write content to file
rm [--force] <path>      # Remove file or directory
mkfld <dir>              # Create directory
go <dir>                 # Change working directory
pwd                      # Print working directory
disk-info                # Display disk information
format                   # Format disk with SimpleFS
test-fs                  # Test VFS operations
test-disk                # Test disk operations
```

#### Process Management

```bash
procs                    # List active tasks
terminate <task_id>      # Terminate task by ID
pause <milliseconds>     # Pause execution
await <task_id>          # Wait for task completion
show <filename>          # Display file contents
chanmake                 # Create IPC channel
chaninfo                 # Display IPC channels
```

#### Memory

```bash
mem                      # Display system memory
vmm                      # Display virtual memory status
showmem                  # Display memory usage
test-page                # Test page allocation
```

#### Network

```bash
ping <ip> [count]        # ICMP echo request
ifconfig [iface]         # Show/configure network interfaces
netstat                  # Display network connections
arp [-d]                 # Display or clear ARP cache
nslookup <hostname>      # Resolve hostname to IP
dns [-c|-s <ip>]         # Configure DNS settings
wget <url> [output]      # Download file via HTTP
aurl [-v] <url>          # Advanced HTTP client
ftp <command>            # FTP client
dhcp [interface]         # Request IP via DHCP
netconfig <iface> <cmd>  # Configure network interface
hostname [name]          # Display or set hostname
```

#### User Management

```bash
adduser <user> [--admin] # Create user account
deluser <user>           # Delete user account
listusers                # List all user accounts
passwd                   # Change password
fsmode                   # Display filesystem mode
```

#### Security

```bash
sandbox [pid]            # Sandbox operations
cage <level>             # Set cage isolation level
cageroot <path>          # Set cage root directory
perms <path>             # Display file permissions
```

#### Environment

```bash
envars                   # List environment variables
setenv <name>=<value>    # Set environment variable
getenv <name>            # Get environment variable value
```

#### Modules & Packages

```bash
modlist                  # List loaded kernel modules
modload <path>           # Load kernel module
modunload <name>         # Unload kernel module
kernelver                # Display kernel version
apm update               # Update package repository
apm kmodule list         # List available modules
apm kmodule list --installed # List installed modules
apm kmodule info <name>  # Show module information
apm kmodule install <name> # Install module
apm kmodule remove <name>  # Remove module
```

#### Partitions

```bash
partitions               # List disk partitions
partmount <id> <path> <fs> # Mount partition
```

#### Init System

```bash
initctl <cmd> [service]  # Control services (start/stop/restart/status/list)
runlevel [level]         # Get or set runlevel (0-3)
servicestat              # Show service status report
```

---

## Building & Running

### Prerequisites

**Required tools:**

- GCC with 32-bit multilib support (`gcc-multilib`)
- NASM assembler
- GNU ld linker
- GRUB tools (`grub-mkrescue`, `xorriso`)
- QEMU emulator (`qemu-system-i386`)

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

### Build Process

1. **Compilation**: C sources compiled with `-m32 -ffreestanding -O2`, assembly with NASM `-f elf32`
2. **Linking**: Custom `linker.ld` script ensures `.multiboot` section at start, identity-mapped at 1MB
3. **ISO Creation**: GRUB bootloader + kernel.elf packaged into bootable ISO with `grub-mkrescue`

### Runtime Configuration

**QEMU Parameters:**

- `-m 128M` - 128MB RAM
- `-serial stdio` - Serial output to stdout (debug logging)
- `-drive file=disk.img,format=raw` - Attach persistent disk
- `-device e1000,netdev=net0` - Intel E1000 NIC
- `-netdev user,id=net0` - User-mode networking (NAT)

**Accessing the system:**

- Default user: `root` (no password initially - set via `passwd` after boot)
- Create users: `adduser <username>` then set password
- Serial output is logged to `serial.log`
- VGA output in QEMU window (if not using `-nographic`)

---

## Development

### Project Structure

```
aOS/
├── boot/
│   └── grub/
│       └── grub.cfg           # GRUB bootloader configuration
├── include/
│   ├── *.h                    # Core kernel headers
│   ├── arch/i386/             # x86-specific headers (GDT, IDT, paging)
│   ├── crypto/                # Cryptography headers
│   ├── dev/                   # Device driver headers
│   ├── fs/                    # Filesystem headers
│   └── net/                   # Networking stack headers
├── src/
│   ├── arch/i386/             # x86-specific code (boot.s, GDT, IDT, paging)
│   ├── crypto/                # Cryptographic implementations
│   ├── dev/                   # Device drivers
│   ├── fs/                    # Filesystem implementations
│   ├── kernel/                # Core kernel + shell commands
│   ├── lib/                   # Standard library (string, stdlib, malloc)
│   ├── mm/                    # Memory management (PMM, VMM)
│   └── net/                   # Networking stack
├── scripts/                   # Build/test scripts
├── linker.ld                  # Kernel linker script
├── Makefile                   # Build system
├── CONTRIBUTING.md            # Contribution guidelines
└── LICENSE                    # CC BY-NC 4.0 license
```

### Coding Standards

**File Headers:**
Every source file must include:

```c
/*
 * === AOS HEADER BEGIN ===
 * ./path/to/file.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.8
 * === AOS HEADER END ===
 */
```

**Style Guidelines:**

- **Indentation**: 4 spaces (no tabs)
- **Braces**: K&R style (opening brace on same line)
- **Naming**:
  - Functions: `snake_case` (`vfs_open`, `tcp_connect`)
  - Structures: `snake_case_t` (`vnode_t`, `tcp_socket_t`)
  - Constants/Macros: `UPPER_SNAKE_CASE` (`PAGE_SIZE`, `VFS_OK`)
- **Comments**: `//` for brief, `/* */` for detailed

**Output Patterns:**

- Early boot: `serial_puts()` to COM1 (always available)
- User-facing: `vga_puts()` for console
- Commands: `kprint()` wrapper (uses VGA)
- Debugging: Serial output visible in `make run` or `serial.log`

### Adding Features

**Device Driver:**

1. Create header in `include/dev/mydevice.h`
2. Implement in `src/dev/mydevice.c` using `inb()`/`outb()`
3. Initialize in `kernel_main()` (see `src/kernel/kernel.c`)
4. Use `serial_puts()` for init status messages

**System Call:**

1. Define number in `include/syscall.h` (`#define SYS_MYCALL N`)
2. Implement handler in `src/kernel/syscall.c`
3. Register in `init_syscalls()`: `syscall_table[SYS_MYCALL] = ...`
4. Add userspace wrapper using inline assembly

**VFS Filesystem:**

1. Define `filesystem_t` with ops table (mount, get_root)
2. Implement vnode operations: open, read, write, finddir, readdir, mkdir, unlink
3. Register with `vfs_register_filesystem()` in kernel init
4. Mount via `vfs_mount(source, target, fstype, flags)`

**Shell Command:**

1. Implement `void cmd_mycommand(int argc, char** argv)` in `src/kernel/cmd_*.c`
2. Register in `init_command_registry()` at `src/kernel/command_registry.c`
3. Use `vga_puts()` for output, parse args with `argc`/`argv`

**Interrupt Handler:**

- Register via `register_interrupt_handler(vector, handler_function)`
- Handler signature: `void handler(registers_t* regs)`
- Send EOI to PIC after IRQ handling: `pic_send_eoi(irq_number)`

### Testing

**No automated test suite yet.** Manual testing procedure:

1. Build kernel: `make iso`
2. Boot in QEMU: `make run` or `make run-s`
3. Exercise affected subsystems via shell commands
4. Check serial output for errors: `cat serial.log`
5. Verify functionality with specific test cases

**Common test scenarios:**

- Filesystem: `format`, `mkdir /test`, `touch /test/file`, `ls /test`, `rm /test/file`
- Networking: `ifconfig`, `ping 8.8.8.8`, `wget http://example.com`
- Memory: `meminfo`, check for leaks after repeated operations
- Modules: `apm update`, `apm kmodule install test`, `lsmod`

### Debugging

**Serial Logging:**

```bash
make run           # Serial output to stdout + serial.log
make run-nographic # Serial-only mode
```

**QEMU Monitor:**

```bash
# Press Ctrl+Alt+2 to access QEMU monitor
info registers     # CPU state
info mem           # Memory mappings
info pic           # PIC state
```

---

## Architecture Support

**Current:**

- i386 (32-bit x86) - **fully supported**

**Planned (framework in place):**

- x86_64 (64-bit x86)
- ARM (ARMv7-A)
- RISC-V (RV32I)

To build for different architecture:

```bash
make ARCH=i386 run    # Default
make ARCH=x86_64 run  # Future
```

## Performance Characteristics

**Boot Time:** ~2-3 seconds in QEMU (includes GRUB, kernel init, service startup)

**Memory Footprint:**

- Kernel image: ~32KB
- Runtime: ~2-4MB with all subsystems initialized

**Disk I/O:**

- ATA PIO: ~5-10 MB/s (polling overhead)
- SimpleFS: No caching, direct sector access

**Network:**

- E1000: ~100 Mbps theoretical (limited by processing overhead)
- TCP: Basic slow start, no congestion control tuning

**Scalability:**

- Max processes: 256 (configurable)
- Max files: Limited by filesystem capacity
- Max modules: 32 (configurable)

## License

Copyright (c) 2024-2026 Aarav Mehta and aOS Contributors

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
Report privately to maintainers, not via public issues.

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
