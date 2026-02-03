#!/bin/bash
# TAP interface setup script for aOS networking
# This script is called by QEMU to bring up the TAP interface

TAP_INTERFACE=$1
BRIDGE_INTERFACE="${BRIDGE_INTERFACE:-br0}"

# Auto-detect available network (prefer private ranges)
# Try to find an unused network in 10.0.0.0/8, 172.16.0.0/12, or 192.168.0.0/16
find_available_network() {
    # Check if 10.0.2.0/24 is available
    if ! ip addr show | grep -q "10.0.2."; then
        echo "10.0.2"
        return
    fi
    
    # Try other 10.0.x.0/24 networks
    for i in {3..254}; do
        if ! ip addr show | grep -q "10.0.$i."; then
            echo "10.0.$i"
            return
        fi
    done
    
    # Fallback to 192.168.x.0/24
    for i in {100..200}; do
        if ! ip addr show | grep -q "192.168.$i."; then
            echo "192.168.$i"
            return
        fi
    done
    
    # Last resort
    echo "172.16.0"
}

NETWORK_BASE=$(find_available_network)
TAP_IP="${NETWORK_BASE}.1"
TAP_NETWORK="${NETWORK_BASE}.0/24"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Error: TAP setup requires root privileges"
    exit 1
fi

echo "Setting up TAP interface: $TAP_INTERFACE"
echo "Using network: $TAP_NETWORK (Bridge IP: $TAP_IP)"

# Create bridge if it doesn't exist
if ! ip link show $BRIDGE_INTERFACE &>/dev/null; then
    echo "Creating bridge: $BRIDGE_INTERFACE"
    ip link add name $BRIDGE_INTERFACE type bridge
    ip addr add $TAP_IP/24 dev $BRIDGE_INTERFACE
    ip link set $BRIDGE_INTERFACE up
fi

# Bring up the TAP interface
ip link set $TAP_INTERFACE up promisc on

# Add TAP interface to bridge
ip link set $TAP_INTERFACE master $BRIDGE_INTERFACE

echo "TAP interface $TAP_INTERFACE added to bridge $BRIDGE_INTERFACE"

# Enable IP forwarding
echo 1 > /proc/sys/net/ipv4/ip_forward

# Set up NAT (masquerading) for internet access
# Find the default internet interface
DEFAULT_IF=$(ip route | grep default | awk '{print $5}' | head -n1)

if [ -n "$DEFAULT_IF" ]; then
    echo "Setting up NAT via $DEFAULT_IF"
    
    # Clear any existing rules for this bridge
    iptables -t nat -D POSTROUTING -s $TAP_NETWORK ! -d $TAP_NETWORK -j MASQUERADE 2>/dev/null
    
    # Add NAT rule
    iptables -t nat -A POSTROUTING -s $TAP_NETWORK ! -d $TAP_NETWORK -j MASQUERADE
    
    # Allow forwarding
    iptables -D FORWARD -i $BRIDGE_INTERFACE -o $DEFAULT_IF -j ACCEPT 2>/dev/null
    iptables -D FORWARD -i $DEFAULT_IF -o $BRIDGE_INTERFACE -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null
    
    iptables -A FORWARD -i $BRIDGE_INTERFACE -o $DEFAULT_IF -j ACCEPT
    iptables -A FORWARD -i $DEFAULT_IF -o $BRIDGE_INTERFACE -m state --state RELATED,ESTABLISHED -j ACCEPT
    
    echo "NAT configured for internet access"
else
    echo "Warning: Could not find default network interface for NAT"
fi

# Start dnsmasq for DHCP (if available)
if command -v dnsmasq &>/dev/null; then
    # Kill any existing dnsmasq on this bridge
    pkill -f "dnsmasq.*$BRIDGE_INTERFACE" 2>/dev/null
    
    # Start dnsmasq for DHCP and DNS
    DHCP_START="${NETWORK_BASE}.100"
    DHCP_END="${NETWORK_BASE}.200"
    
    dnsmasq \
        --interface=$BRIDGE_INTERFACE \
        --bind-interfaces \
        --dhcp-range=$DHCP_START,$DHCP_END,12h \
        --dhcp-option=option:router,$TAP_IP \
        --dhcp-option=option:dns-server,8.8.8.8,8.8.4.4 \
        --pid-file=/var/run/dnsmasq-$BRIDGE_INTERFACE.pid \
        --log-facility=/var/log/dnsmasq-$BRIDGE_INTERFACE.log \
        2>/dev/null
    
    if [ $? -eq 0 ]; then
        echo "DHCP server (dnsmasq) started on $BRIDGE_INTERFACE"
        echo "  DHCP range: $DHCP_START - $DHCP_END"
        echo "  Gateway: $TAP_IP"
        echo "  DNS: 8.8.8.8, 8.8.4.4"
    fi
else
    echo "Warning: dnsmasq not found, DHCP server not started"
    echo "  Install with: sudo apt-get install dnsmasq"
fi

echo "TAP setup complete"
exit 0
