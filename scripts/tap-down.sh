#!/bin/bash
# TAP interface teardown script for aOS networking
# This script is called by QEMU to clean up the TAP interface

TAP_INTERFACE=$1
BRIDGE_INTERFACE="br0"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Error: TAP teardown requires root privileges"
    exit 1
fi

echo "Tearing down TAP interface: $TAP_INTERFACE"

# Stop dnsmasq if running
if [ -f "/var/run/dnsmasq-$BRIDGE_INTERFACE.pid" ]; then
    kill $(cat /var/run/dnsmasq-$BRIDGE_INTERFACE.pid) 2>/dev/null
    rm /var/run/dnsmasq-$BRIDGE_INTERFACE.pid
    echo "DHCP server stopped"
fi

# Remove TAP interface from bridge
if ip link show $TAP_INTERFACE &>/dev/null; then
    ip link set $TAP_INTERFACE nomaster
    ip link set $TAP_INTERFACE down
    echo "TAP interface $TAP_INTERFACE removed from bridge"
fi

# Check if bridge has any other interfaces
if ip link show $BRIDGE_INTERFACE &>/dev/null; then
    BRIDGE_PORTS=$(ls /sys/class/net/$BRIDGE_INTERFACE/brif/ 2>/dev/null | wc -l)
    
    if [ "$BRIDGE_PORTS" -eq 0 ]; then
        echo "No more TAP interfaces, removing bridge $BRIDGE_INTERFACE"
        ip link set $BRIDGE_INTERFACE down
        ip link delete $BRIDGE_INTERFACE type bridge
    else
        echo "Bridge $BRIDGE_INTERFACE still has active interfaces, keeping it up"
    fi
fi

echo "TAP teardown complete"
exit 0
