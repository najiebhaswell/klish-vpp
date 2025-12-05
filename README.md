# VPP Klish CLI Plugin

A Cisco-style CLI interface for VPP (Vector Packet Processing) using Klish3 framework.

## Features

- **Cisco-like CLI** - Familiar command structure (configure, interface)
- **Interface Configuration** - IP addresses, MTU, enable/disable
- **VLAN Subinterfaces** - Auto-create with 
- **LCP (Linux Control Plane)** - Create tap interfaces for Linux integration
- **Tab Completion** - Dynamic interface name completion
- **Config Persistence** - Save and auto-load configuration
- **Show Running Config** - Display current configuration in Cisco format

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

### 5. Configure VPP Startup Config

Add to `/etc/vpp/startup.conf` in the `unix` section:

```
unix {
  startup-config /etc/vpp/klish-startup.conf
  ...
}
```

### 6. Install Systemd Service

```bash
sudo cp klishd.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable klishd.service
sudo systemctl start klishd.service
```

## Usage

### Connect to CLI

```bash
sudo klish
```

### Basic Commands

```
hostname# show-interfaces
hostname# show-running-config
hostname# show-version
hostname# configure
hostname(config)# interface BondEthernet0.100    # Auto-creates VLAN subinterface
hostname(config-if)# ip 10.0.0.1/24
hostname(config-if)# ipv6 2001:db8::1/64
hostname(config-if)# mtu 1500
hostname(config-if)# enable
hostname(config-if)# lcp bond0.100               # Create LCP tap
hostname(config-if)# exit
hostname(config)# end
hostname# write-memory                           # Save configuration
```

### Available Commands

| Mode | Command | Description |
|------|---------|-------------|
| Main | `show-interfaces` | Show all interfaces |
| Main | `show-running-config` | Show running configuration |
| Main | `show-version` | Show VPP version |
| Main | `show-lcp` | Show LCP mappings |
| Main | `ping <target>` | Ping target |
| Main | `write-memory` | Save configuration |
| Main | `configure` | Enter config mode |
| Config | `interface <name>` | Configure interface (Tab for completion) |
| Config | `create-loopback` | Create loopback interface |
| Interface | `ip <addr/prefix>` | Set IPv4 address |
| Interface | `ipv6 <addr/prefix>` | Set IPv6 address |
| Interface | `mtu <value>` | Set MTU |
| Interface | `enable` / `disable` | Bring interface up/down |
| Interface | `lcp <host-if>` | Create LCP tap interface |

## Configuration Persistence

```bash
# Save running configuration
hostname# write-memory
```

Configuration is saved to `/etc/vpp/klish-startup.conf` and automatically loaded by VPP on startup (via native startup-config).

## Files

| Path | Description |
|------|-------------|
| `/usr/local/bin/klishd` | Klish daemon |
| `/usr/local/bin/klish` | Klish client |
| `/usr/local/lib/libklish-plugin-vpp.so` | VPP plugin |
| `/usr/local/share/klish/xml/vpp-cli.xml` | CLI definition |
| `/etc/vpp/klish-startup.conf` | Saved configuration |
| `/etc/vpp/startup.conf` | VPP startup config |

## Troubleshooting

### Check service status
```bash
sudo systemctl status klishd
sudo systemctl status vpp
```

### View logs
```bash
sudo journalctl -u klishd -f
sudo journalctl -u vpp -f
```

### Test VPP connection
```bash
vppctl show version
```

## License

MIT License
