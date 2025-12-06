> ⚠️ **WARNING: This project is under active development and NOT recommended for production use.**
> Use at your own risk. APIs and features may change without notice.

# Klish-VPP: Cisco-like CLI for VPP

A Cisco IOS-style command-line interface for managing VPP (Vector Packet Processing) dataplane.

## Features

- **Interface Management**: Configure IP addresses, MTU, enable/disable interfaces
- **Bonding Support**: Create bonds, add/remove members, set mode and load-balance
- **Static Routing**: Add/remove IP routes with next-hop
- **Show Commands**: View interfaces, routes, hardware, memory, errors, PCI devices, bonds
- **LCP Integration**: Linux Control Plane interface management
- **Configuration**: Save and restore VPP configuration
- **Tab Completion**: Auto-complete interface names
- **System Banner**: Display system info on login

## Quick Installation

### From .deb Package (Recommended)

```bash
# Download and install (v1.0.6 - latest)
wget https://github.com/najiebhaswell/klish-vpp/raw/main/klish-vpp_1.0.6_amd64.deb
sudo dpkg -i klish-vpp_1.0.6_amd64.deb

# Start the CLI daemon
sudo systemctl enable --now klishd

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
cd klish3 && ./configure --with-libxml2 && make && sudo make install && cd ..
sudo ldconfig

# Build VPP plugin
cd vpp-klish-plugin && make && sudo make install && cd ..

# Install configuration
sudo mkdir -p /etc/klish /usr/local/share/klish/xml /var/run/klish
sudo cp vpp-cli.xml /usr/local/share/klish/xml/
sudo cp klishd.service /etc/systemd/system/
sudo cp klishd-wrapper /usr/local/bin/

# Start service
sudo systemctl daemon-reload
sudo systemctl enable --now klishd
```

## Available Commands

### Main Mode (Privileged EXEC)

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
| `show-bond` | Show bond interfaces and members |
| `show-banner` | Show system info banner |
| `ping <ip>` | Ping target |
| `write-memory` | Save configuration |
| `configure` | Enter config mode |
| `exit` | Exit CLI |

### Config Mode

| Command | Description |
|---------|-------------|
| `interface <name>` | Configure interface (auto-creates loopback/VLAN/Bond) |
| `no interface <name>` | Delete interface (loopback/VLAN only) |
| `ip route <network> next-hop <gateway>` | Add static IP route |
| `end` | Exit config mode |
| `exit` | Exit config mode |

### Interface Mode

| Command | Description |
|---------|-------------|
| `ip address <addr/prefix>` | Set IPv4 address |
| `ipv6 address <addr/prefix>` | Set IPv6 address |
| `no ip address <addr/prefix>` | Remove IPv4 address |
| `no ipv6 address <addr/prefix>` | Remove IPv6 address |
| `mtu <value>` | Set MTU |
| `lcp <hostif>` | Create LCP for this interface |
| `no lcp` | Remove LCP |
| `enable` | Enable interface (admin up) |
| `disable` | Disable interface (admin down) |
| `mode <mode>` | Set bond mode (lacp, xor, round-robin, active-backup, broadcast) |
| `load-balance <lb>` | Set load-balance algorithm (l2, l23, l34) |
| `member <iface>` | Add member to bond |
| `no member <iface>` | Remove member from bond |
| `exit` | Back to config mode |
| `end` | Back to main mode |

## Command Examples

### Basic Usage

```
$ sudo klish

========================================================================
------------------------INFORMASI ROUTER--------------------------------
========================================================================
Device Name             : router1
Distro                  : Ubuntu 22.04.5 LTS
Kernel                  : 5.15.0-161-generic
Memory Usage            : 5.4Gi used / 125.3Gi total
CPU Usage               : 1.9%
========================================================================

router1# show-version
vpp v25.10-release built by root on server at 2025-10-29T10:56:45

router1# show-interfaces
Interface              IP-Address           MTU    Status Protocol
BondEthernet0          10.10.10.1/24        9000   up     up
loop0                  192.168.1.1/32       9000   up     up
HundredGigabitEthernet8a/0/0  unassigned   9000   up     up
```

### Creating and Configuring Loopback Interface

```
router1# configure
router1(config)# interface loop100
Loopback interface loop100 created
router1(config-if)# ip address 10.100.0.1/32
IP address 10.100.0.1/32 configured on loop100
router1(config-if)# enable
Interface loop100 is now up
router1(config-if)# end
router1# 
```

### Configuring Bond Interface

```
router1# configure
router1(config)# interface BondEthernet0
router1(config-if)# mode lacp
Bond mode set to lacp
router1(config-if)# load-balance l34
Load balance set to l34
router1(config-if)# member HundredGigabitEthernet8a/0/0
Member HundredGigabitEthernet8a/0/0 added to bond
router1(config-if)# member HundredGigabitEthernet8a/0/1
Member HundredGigabitEthernet8a/0/1 added to bond
router1(config-if)# ip address 10.10.10.1/24
IP address 10.10.10.1/24 configured on BondEthernet0
router1(config-if)# enable
Interface BondEthernet0 is now up
router1(config-if)# end
router1#
```

### Viewing Bond Details

```
router1# show-bond
BondEthernet0
  mode: lacp
  load balance: l34
  number of active members: 2
  number of members: 2
    HundredGigabitEthernet8a/0/0
    HundredGigabitEthernet8a/0/1
  device instance: 0
  interface id: 0
```

### Adding Static Route

```
router1# configure
router1(config)# ip route 192.168.100.0/24 next-hop 10.10.10.254
Route added: 192.168.100.0/24 via 10.10.10.254
router1(config)# ip route 0.0.0.0/0 next-hop 10.10.10.1
Route added: 0.0.0.0/0 via 10.10.10.1
router1(config)# end
router1#
```

### Creating LCP (Linux Control Plane) Interface

```
router1# configure
router1(config)# interface BondEthernet0
router1(config-if)# lcp bond0
LCP created: BondEthernet0 -> bond0
router1(config-if)# end
router1# show-lcp
itf-pair: [0] BondEthernet0 tap4096 bond0 2 type tap netns dataplane
```

### Creating VLAN Subinterface

```
router1# configure
router1(config)# interface BondEthernet0.100
VLAN subinterface BondEthernet0.100 created
router1(config-if)# ip address 192.168.100.1/24
IP address 192.168.100.1/24 configured on BondEthernet0.100
router1(config-if)# enable
Interface BondEthernet0.100 is now up
router1(config-if)# end
router1#
```

### Removing Configuration

```
router1# configure
router1(config)# interface loop100
router1(config-if)# no ip address 10.100.0.1/32
IP address 10.100.0.1/32 removed from loop100
router1(config-if)# no lcp
LCP deleted: loop100
router1(config-if)# exit
router1(config)# no interface loop100
Interface loop100 deleted
router1(config)# end
router1#
```

### Saving Configuration

```
router1# write-memory
Building configuration...
[OK]
Configuration saved to /etc/vpp/klish-startup.conf
```

### Viewing Running Configuration

```
router1# show-running-config
!
! VPP Running Configuration
!
create loopback interface instance 0
create bond mode lacp load-balance l34
!
bond add BondEthernet0 HundredGigabitEthernet8a/0/0
bond add BondEthernet0 HundredGigabitEthernet8a/0/1
!
set interface state BondEthernet0 up
set interface ip address BondEthernet0 10.10.10.1/24
set interface state loop0 up
set interface ip address loop0 192.168.1.1/32
!
lcp create BondEthernet0 host-if bond0
!
end
```

## Requirements

- Ubuntu 22.04 / Debian 12 or compatible
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
