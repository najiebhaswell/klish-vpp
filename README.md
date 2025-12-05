> ⚠️ **WARNING: This project is under active development and NOT recommended for production use.**
> Use at your own risk. APIs and features may change without notice.

# Klish-VPP: Cisco-like CLI for VPP

A Cisco IOS-style command-line interface for managing VPP (Vector Packet Processing) dataplane.

## Features

- **Interface Management**: Configure IP addresses, MTU, enable/disable interfaces
- **Show Commands**: View interfaces, routes, hardware, memory, errors, PCI devices
- **LCP Integration**: Linux Control Plane interface management
- **Configuration**: Save and restore VPP configuration
- **Tab Completion**: Auto-complete interface names

## Quick Installation

### From .deb Package (Recommended)

```bash
# Download and install
wget https://github.com/najiebhaswell/klish-vpp/raw/main/klish-vpp_1.0.5_amd64.deb
sudo dpkg -i klish-vpp_1.0.5_amd64.deb

# Start the CLI daemon
sudo systemctl start klishd

# Access CLI
sudo klish
```

### From Source

```bash
# Clone repository
git clone https://github.com/najiebhaswell/klish-vpp.git
cd klish-vpp

# Build dependencies (faux and klish3)
cd faux && ./configure && make && sudo make install && cd ..
cd klish3 && ./configure && make && sudo make install && cd ..
sudo ldconfig

# Build VPP plugin
cd vpp-klish-plugin && make && sudo make install && cd ..

# Install configuration
sudo mkdir -p /etc/klish /usr/local/share/klish/xml /var/run/klish
sudo cp vpp-cli.xml /usr/local/share/klish/xml/
sudo cp klishd.service /etc/systemd/system/
sudo cp klishd-wrapper /usr/local/bin/

# Create klishd.conf
cat << CONF | sudo tee /etc/klish/klishd.conf
UnixSocketPath=/var/run/klish/klish.sock
DBs=libxml2
DB.libxml2.XMLPath=/usr/local/share/klish/xml
CONF

# Start service
sudo systemctl daemon-reload
sudo systemctl enable klishd
sudo systemctl start klishd
```

## Available Commands

### Main Mode
| Command | Description |
|---------|-------------|
| `show-interfaces` | Show all interfaces with IP addresses |
| `show-hardware` | Show hardware interfaces with MAC |
| `show-version` | Show VPP version |
| `show-ip-route` | Show IP routing table |
| `show-lcp` | Show LCP interfaces |
| `show-running-config` | Show running configuration |
| `show-memory-heap` | Show main heap memory |
| `show-memory-map` | Show memory map |
| `show-buffers` | Show buffer pools |
| `show-trace` | Show packet trace |
| `show-error` | Show error counters |
| `show-pci` | Show PCI devices |
| `ping <ip>` | Ping target |
| `write-memory` | Save configuration |
| `configure` | Enter config mode |

### Config Mode
| Command | Description |
|---------|-------------|
| `interface <name>` | Configure interface (auto-creates loopback/VLAN) |
| `no interface <name>` | Delete interface |
| `lcp-create <if> <host>` | Create LCP interface |
| `lcp-delete <if>` | Delete LCP interface |
| `ip-route <net> <mask> <gw>` | Add IP route |

### Interface Mode
| Command | Description |
|---------|-------------|
| `ip <addr/prefix>` | Set IPv4 address |
| `ipv6 <addr/prefix>` | Set IPv6 address |
| `no-ip <addr/prefix>` | Remove IPv4 address |
| `no-ipv6 <addr/prefix>` | Remove IPv6 address |
| `mtu <value>` | Set MTU |
| `lcp <hostif>` | Create LCP for this interface |
| `no-lcp` | Remove LCP |
| `enable` | Enable interface |
| `disable` | Disable interface |

## Example Usage

```
debian@server:~$ sudo klish
server# show-interfaces
Interface        IP-Address           MTU    Status Protocol
BondEthernet0    10.10.10.1/24        9000   up     up
loop0            192.168.1.1/32       9000   up     up

server# configure
server(config)# interface loop100
server(config-if)# ip 10.100.0.1/32
server(config-if)# enable
server(config-if)# end

server# write-memory
Configuration saved to /etc/vpp/startup.vpp
```

## Requirements

- Debian 12 (Bookworm) or compatible
- VPP 24.x or later (running)
- libxml2

## License

MIT License

## Contributing

Contributions are welcome! If you want to participate in this project:

1. **Fork** this repository
2. **Clone** your fork locally
   ```bash
   git clone https://github.com/YOUR_USERNAME/klish-vpp.git
   ```
3. **Create a new branch** for your feature
   ```bash
   git checkout -b feature/your-feature-name
   ```
4. **Make your changes** and commit
   ```bash
   git add .
   git commit -m "Add: your feature description"
   ```
5. **Push** to your fork
   ```bash
   git push origin feature/your-feature-name
   ```
6. **Create a Pull Request** from your fork to this repository

### Ideas for Contribution

- Add more VPP commands (BGP, MPLS, NAT, etc.)
- Improve error handling
- Add unit tests
- Improve documentation
- Add support for other Linux distributions
- Create Docker image

Thank you for your interest in contributing! 🙏
