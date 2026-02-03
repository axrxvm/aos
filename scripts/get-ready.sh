#!/bin/bash
# aOS Tester Dependency Installation Script
# Installs all required dependencies to run aOS via QEMU with disk and network support
# Compatible with Ubuntu/Debian, Fedora, and Arch-based systems

set -e

echo "========================================"
echo "  aOS Tester - Dependency Installation  "
echo "========================================"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "This script requires root privileges to install packages."
    echo "Please run with sudo: sudo ./get-ready.sh"
    exit 1
fi

# Detect distribution
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "$ID"
    elif [ -f /etc/lsb-release ]; then
        . /etc/lsb-release
        echo "$DISTRIB_ID" | tr '[:upper:]' '[:lower:]'
    else
        echo "unknown"
    fi
}

DISTRO=$(detect_distro)
echo "Detected distribution: $DISTRO"
echo ""

# Install packages based on distribution
case "$DISTRO" in
    ubuntu|debian|pop|linuxmint)
        echo "Installing packages for Debian/Ubuntu-based system..."
        apt-get update
        apt-get install -y qemu-system-x86 \
                          qemu-utils \
                          bridge-utils \
                          iptables \
                          dnsmasq \
                          iproute2 \
                          net-tools
        echo "Packages installed successfully!"
        ;;
    
    fedora|rhel|centos|rocky|almalinux)
        echo "Installing packages for Red Hat-based system..."
        dnf install -y qemu-system-x86 \
                      qemu-img \
                      bridge-utils \
                      iptables \
                      dnsmasq \
                      iproute \
                      net-tools
        echo "Packages installed successfully!"
        ;;
    
    arch|manjaro|endeavouros)
        echo "Installing packages for Arch-based system..."
        pacman -Sy --noconfirm qemu-system-x86 \
                               bridge-utils \
                               iptables \
                               dnsmasq \
                               iproute2 \
                               net-tools
        echo "Packages installed successfully!"
        ;;
    
    opensuse*|suse)
        echo "Installing packages for openSUSE..."
        zypper install -y qemu-x86 \
                         bridge-utils \
                         iptables \
                         dnsmasq \
                         iproute2 \
                         net-tools
        echo "Packages installed successfully!"
        ;;
    
    *)
        echo "Unknown or unsupported distribution: $DISTRO"
        echo ""
        echo "Please manually install the following packages:"
        echo "  - qemu-system-x86 (or qemu-system-i386)"
        echo "  - qemu-utils / qemu-img"
        echo "  - bridge-utils"
        echo "  - iptables"
        echo "  - dnsmasq (for DHCP server)"
        echo "  - iproute2 (for network interface management)"
        echo "  - net-tools (optional, for legacy network tools)"
        exit 1
        ;;
esac

echo ""
echo "========================================"
echo "  Verifying Installation...            "
echo "========================================"
echo ""

# Verify QEMU installation
if command -v qemu-system-i386 &> /dev/null; then
    QEMU_VERSION=$(qemu-system-i386 --version | head -n1)
    echo "✓ QEMU installed: $QEMU_VERSION"
else
    echo "✗ QEMU not found! Please check your installation."
    exit 1
fi

# Verify bridge-utils
if command -v brctl &> /dev/null; then
    echo "✓ bridge-utils installed"
else
    echo "✗ bridge-utils not found!"
fi

# Verify dnsmasq
if command -v dnsmasq &> /dev/null; then
    DNSMASQ_VERSION=$(dnsmasq --version | head -n1)
    echo "✓ dnsmasq installed: $DNSMASQ_VERSION"
else
    echo "✗ dnsmasq not found!"
fi

# Verify iptables
if command -v iptables &> /dev/null; then
    echo "✓ iptables installed"
else
    echo "✗ iptables not found!"
fi

echo ""
echo "========================================"
echo "  Setup Complete!                      "
echo "========================================"
echo ""
echo "All dependencies have been installed."
echo "You can now run aOS using:"
echo "  ./test-aos.sh            # Run with disk and network support"
echo "  ./test-aos.sh --no-network   # Run with disk only"
echo ""
echo "Note: Network support requires sudo privileges for TAP interface setup."
echo ""
