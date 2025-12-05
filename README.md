# VPP Klish CLI Plugin

A Cisco-style CLI interface for VPP (Vector Packet Processing) using Klish3 framework.

## Features

- **Cisco-like CLI** - Familiar command structure (enable, configure, interface)
- **Interface Configuration** - IP addresses, MTU, enable/disable
- **LCP (Linux Control Plane)** - Create tap interfaces for Linux integration
- **Tab Completion** - Dynamic interface name completion
- **Config Persistence** - Save and restore configuration across restarts
- **Systemd Integration** - Auto-start and config loading

## Requirements

- Debian 12 (Bookworm) or compatible
- VPP 25.x with LCP plugin
- Klish3 framework
- GCC, Make, pkg-config

## Installation

### 1. Install Dependencies

```bash
apt-get update
apt-get install -y git build-essential pkg-config libexpat1-dev liblua5.3-dev
```

### 2. Install Klish3

```bash
git clone https://github.com/klish-project/klish.git klish3
cd klish3
./configure --prefix=/usr/local
make -j$(nproc)
sudo make install
sudo ldconfig
```

### 3. Build VPP Plugin

```bash
cd /path/to/klish-vpp/vpp-klish-plugin
make
sudo cp libklish-plugin-vpp.so /usr/local/lib/
```

### 4. Install XML Configuration

```bash
sudo mkdir -p /usr/local/share/klish/xml
sudo cp /path/to/klish-vpp/vpp-cli.xml /usr/local/share/klish/xml/
```

### 5. Install Systemd Service

```bash
sudo cat > /etc/systemd/system/klishd.service << 'SVCEOF'
[Unit]
Description=Klish CLI Daemon for VPP
After=vpp.service network.target
Requires=vpp.service

[Service]
Type=simple
ExecStartPre=/bin/sleep 3
ExecStart=/usr/local/bin/klishd
ExecStartPost=/bin/bash -c 'sleep 2 && [ -f /etc/vpp/klish-startup.conf ] && vppctl exec /etc/vpp/klish-startup.conf || true'
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
SVCEOF

sudo systemctl daemon-reload
sudo systemctl enable klishd.service
```

### 6. Start Service

```bash
sudo systemctl start klishd
```

## Usage

### Connect to CLI

```bash
sudo klish
```

### Basic Commands

```
hostname> show-interfaces          # Show all interfaces with IP/MTU
hostname> show-version             # Show VPP version
hostname> enable                   # Enter privileged mode
hostname# configure                # Enter config mode
hostname(config)# interface loop0  # Configure interface
hostname(config-if)# ip 10.0.0.1/24
hostname(config-if)# ipv6 2001:db8::1/64
hostname(config-if)# mtu 1500
hostname(config-if)# enable
hostname(config-if)# lcp lo0       # Create LCP tap
hostname(config-if)# exit
hostname(config)# end
hostname# write-memory             # Save configuration
```

### Interface View Commands

| Command | Description |
|---------|-------------|
| `ip <addr/prefix>` | Set IPv4 address |
| `ipv6 <addr/prefix>` | Set IPv6 address |
| `no-ip <addr/prefix>` | Remove IPv4 address |
| `no-ipv6 <addr/prefix>` | Remove IPv6 address |
| `mtu <value>` | Set MTU |
| `enable` | Bring interface up |
| `disable` | Bring interface down |
| `lcp <host-if>` | Create LCP tap interface |
| `no-lcp` | Remove LCP tap |
| `exit` | Back to config mode |
| `end` | Back to enable mode |

### Config Mode Commands

| Command | Description |
|---------|-------------|
| `interface <name>` | Configure interface (Tab for completion) |
| `create-loopback` | Create loopback interface |
| `lcp-create <if> <host-if>` | Create LCP for interface |
| `create-subinterface <if> <subid> <vlan>` | Create VLAN subinterface |

## Configuration Persistence

Save running configuration:
```
hostname# write-memory
```

Configuration is saved to `/etc/vpp/klish-startup.conf` and automatically loaded when klishd service starts.

## Files

| Path | Description |
|------|-------------|
| `/usr/local/bin/klishd` | Klish daemon |
| `/usr/local/bin/klish` | Klish client |
| `/usr/local/lib/libklish-plugin-vpp.so` | VPP plugin |
| `/usr/local/share/klish/xml/vpp-cli.xml` | CLI definition |
| `/etc/vpp/klish-startup.conf` | Saved configuration |
| `/etc/systemd/system/klishd.service` | Systemd service |

## Troubleshooting

### Check service status
```bash
sudo systemctl status klishd
```

### View logs
```bash
sudo journalctl -u klishd -f
```

### Test VPP connection
```bash
vppctl show version
```

## License

MIT License
