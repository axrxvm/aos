#!/bin/bash
# aOS Tester - Run Script
# Run aOS in QEMU with optional network support

set -e

ISO_FILE="aOS.iso"
DISK_IMG="disk.img"
SERIAL_LOG="serial.log"
NETWORK_MODE="tap"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --no-network)
            NETWORK_MODE="none"
            shift
            ;;
        -h|--help)
            echo "aOS Tester - Run Script"
            echo ""
            echo "Usage: ./test-aos.sh [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --no-network    Run without network support (disk only)"
            echo "  -h, --help      Show this help message"
            echo ""
            echo "Examples:"
            echo "  ./test-aos.sh                # Run with disk and TAP networking (requires sudo)"
            echo "  ./test-aos.sh --no-network  # Run with disk only (no sudo required)"
            echo ""
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

echo "========================================"
echo "    aOS Tester - Starting aOS v0.8.5    "
echo "========================================"
echo ""

# Check if ISO file exists
if [ ! -f "$ISO_FILE" ]; then
    echo "Error: ISO file '$ISO_FILE' not found!"
    echo "Please ensure the aOS.iso file is in the current directory."
    exit 1
fi

# Check if QEMU is installed
if ! command -v qemu-system-i386 &> /dev/null; then
    echo "Error: QEMU is not installed!"
    echo "Please run ./get-ready.sh to install required dependencies."
    exit 1
fi

# Create disk image if it doesn't exist
if [ ! -f "$DISK_IMG" ]; then
    echo "Creating 50MB disk image at $DISK_IMG..."
    dd if=/dev/zero of="$DISK_IMG" bs=1M count=50 2>/dev/null
    echo "Disk image created."
else
    echo "Using existing disk image at $DISK_IMG"
fi

echo ""

# Run based on network mode
if [ "$NETWORK_MODE" = "tap" ]; then
    echo "Running aOS with TAP networking (requires sudo)..."
    echo "Serial output will be saved to $SERIAL_LOG"
    echo ""
    echo "Network features available:"
    echo "  - DHCP client (dhcp command)"
    echo "  - DNS resolution (nslookup command)"
    echo "  - HTTP client (wget command)"
    echo "  - FTP client (ftp command)"
    echo "  - Ping (ping command)"
    echo ""
    echo "Starting QEMU..."
    echo ""
    
    # Check if tap scripts exist
    if [ ! -f "tap-up.sh" ] || [ ! -f "tap-down.sh" ]; then
        echo "Error: TAP scripts (tap-up.sh, tap-down.sh) not found!"
        echo "Please ensure all files from tester-bundle are present."
        exit 1
    fi
    
    # Make tap scripts executable
    chmod +x tap-up.sh tap-down.sh
    
    # Run QEMU with TAP networking (requires sudo)
    sudo qemu-system-i386 -cdrom "$ISO_FILE" -m 128M -boot d -serial stdio \
        -drive file="$DISK_IMG",format=raw,index=0,media=disk \
        -netdev tap,id=net0,script=tap-up.sh,downscript=tap-down.sh \
        -device e1000,netdev=net0,mac=52:54:00:12:34:56 | tee "$SERIAL_LOG"
    
elif [ "$NETWORK_MODE" = "none" ]; then
    echo "Running aOS without networking..."
    echo "Serial output will be saved to $SERIAL_LOG"
    echo ""
    echo "Features available:"
    echo "  - Disk I/O (ls, cat, mkdir, touch, rm, etc.)"
    echo "  - File system operations"
    echo "  - Memory management"
    echo "  - Process management (limited)"
    echo ""
    echo "Starting QEMU..."
    echo ""
    
    # Run QEMU without networking
    qemu-system-i386 -cdrom "$ISO_FILE" -m 128M -boot d -serial stdio \
        -drive file="$DISK_IMG",format=raw,index=0,media=disk | tee "$SERIAL_LOG"
fi

echo ""
echo "========================================"
echo "  aOS has exited                       "
echo "========================================"
echo ""
echo "Serial output saved to: $SERIAL_LOG"
echo ""
