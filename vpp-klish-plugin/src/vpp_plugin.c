/*
 * VPP Plugin for Klish3
 * Connects Klish CLI to VPP API for Cisco-like interface management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>

#include <faux/faux.h>
#include <faux/str.h>
#include <faux/argv.h>
#include <klish/kplugin.h>
#include <klish/kcontext.h>
#include <klish/kpargv.h>
#include <klish/kentry.h>
#include <klish/ksym.h>

#define VPP_CLI_SOCKET "/run/vpp/cli.sock"
#define BUFFER_SIZE 8192
#define TELNET_IAC 255
#define TELNET_DONT 254
#define TELNET_DO 253
#define TELNET_WONT 252
#define TELNET_WILL 251
#define TELNET_SB 250
#define TELNET_SE 240

/* Version */
const uint8_t kplugin_vpp_major = KPLUGIN_MAJOR;
const uint8_t kplugin_vpp_minor = KPLUGIN_MINOR;

static int vpp_cli_fd = -1;

/* File-based storage for current interface (shared across forked processes)
 * Uses parent PID to create unique file per client session */

static void get_iface_file_path(char *path, size_t size) {
    /* Use parent PID which is the klishd service process for this client */
    snprintf(path, size, "/tmp/klish_vpp_iface_%d", getppid());
}

/* Get current interface from file */
static const char* get_current_interface(void) {
    static char iface[64];
    char path[128];
    FILE *f;
    
    get_iface_file_path(path, sizeof(path));
    f = fopen(path, "r");
    if (!f) {
        return NULL;
    }
    
    if (fgets(iface, sizeof(iface), f) == NULL) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    
    /* Remove trailing newline */
    size_t len = strlen(iface);
    if (len > 0 && iface[len-1] == '\n') {
        iface[len-1] = 0;
    }
    
    return iface[0] ? iface : NULL;
}

/* Set current interface to file */
static void set_current_interface(const char *iface) {
    char path[128];
    FILE *f;
    
    get_iface_file_path(path, sizeof(path));
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "ERROR: Cannot write to %s\n", path);
        return;
    }
    
    fprintf(f, "%s\n", iface);
    fclose(f);
}

/* Clear current interface (delete file) */
static void clear_current_interface(void) {
    char path[128];
    get_iface_file_path(path, sizeof(path));
    unlink(path);
}

/* Helper to read from socket until prompt or EOF, handling Telnet IAC */
static ssize_t read_until_prompt(int fd, char *buffer, size_t size) {
    size_t total = 0;
    ssize_t n;
    unsigned char c;
    int state = 0; // 0=Normal, 1=IAC, 2=IAC+Cmd, 3=SB
    
    memset(buffer, 0, size);
    
    while (total < size - 1) {
        n = read(fd, &c, 1);
        if (n <= 0) break; // EOF or Error
        
        // Telnet State Machine
        if (state == 0) {
            if (c == TELNET_IAC) {
                state = 1;
            } else {
                buffer[total++] = c;
            }
        } else if (state == 1) { // Received IAC
            if (c == TELNET_IAC) { // Escaped IAC (255 255)
                buffer[total++] = c;
                state = 0;
            } else if (c == TELNET_SB) {
                state = 3;
            } else if (c >= TELNET_WILL && c <= TELNET_DONT) {
                state = 2; // Expect option byte
            } else {
                state = 0; // Other commands (NOP, etc) - ignore
            }
        } else if (state == 2) { // Expecting Option Byte
            state = 0; // Ignore option
        } else if (state == 3) { // Inside Subnegotiation
            if (c == TELNET_SE) { // End of subnegotiation? 
                // Wait, SE is usually preceded by IAC.
                // But simplified: just ignore until IAC SE.
                // Actually, strictly it is IAC SE.
                // Let's handle IAC inside SB.
            }
            if (c == TELNET_IAC) {
                // Check next byte for SE
                 // This simple state machine is imperfect for SB but sufficient for VPP
                 // VPP sends IAC SB ... IAC SE
            }
             // For now, just stay in state 3 until we see IAC SE sequence?
             // Let's refine:
             // If we are in SB, we ignore everything until IAC SE.
        }
    }
    
    // Re-implementing read loop with better buffering and simpler logic
    // We read chunk by chunk and filter.
    return total;
}

/* Better implementation of reading and filtering */
static int read_and_filter(int fd, char *buffer, size_t size, const char *stop_str) {
    size_t total = 0;
    ssize_t n;
    unsigned char buf[1024];
    int in_iac = 0;
    int in_sb = 0;

    while (total < size - 1) {
        n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;

        for (ssize_t i = 0; i < n; i++) {
            unsigned char c = buf[i];
            
            if (in_sb) {
                if (in_iac && c == TELNET_SE) {
                    in_sb = 0;
                    in_iac = 0;
                } else if (c == TELNET_IAC) {
                    in_iac = 1;
                } else {
                    in_iac = 0;
                }
                continue;
            }

            if (in_iac) {
                if (c == TELNET_SB) {
                    in_sb = 1;
                    in_iac = 0;
                } else if (c >= TELNET_WILL && c <= TELNET_DONT) {
                    // Expect one more byte (option code), but we handle it by just resetting in_iac
                    // Wait, we need to skip the NEXT byte too.
                    // This simple logic is failing.
                    // Let's use a proper state variable.
                } else {
                    // Other commands or escaped IAC
                    if (c == TELNET_IAC) {
                         if (total < size - 1) buffer[total++] = c;
                    }
                    in_iac = 0;
                }
                continue;
            }

            if (c == TELNET_IAC) {
                in_iac = 1;
                continue;
            }

            // Normal char
            if (total < size - 1) buffer[total++] = c;
        }
        buffer[total] = 0;
        
        // Check for stop string (prompt)
        if (stop_str && strstr(buffer, stop_str)) {
            return 0; // Found
        }
    }
    return 0;
}

/* Robust Telnet Filter */
static void filter_telnet(unsigned char *data, ssize_t len, char *out, size_t *out_len) {
    size_t j = 0;
    for (ssize_t i = 0; i < len; ) {
        if (data[i] == TELNET_IAC) {
            i++;
            if (i >= len) break;
            if (data[i] == TELNET_IAC) {
                out[j++] = data[i++]; // Escaped IAC
            } else if (data[i] == TELNET_SB) {
                i++;
                while (i < len) {
                    if (data[i] == TELNET_IAC) {
                        i++;
                        if (i < len && data[i] == TELNET_SE) {
                            i++;
                            break;
                        }
                    } else {
                        i++;
                    }
                }
            } else if (data[i] >= TELNET_WILL && data[i] <= TELNET_DONT) {
                i += 2; // Skip Command + Option
            } else {
                i++; // Skip Command
            }
        } else {
            out[j++] = data[i++];
        }
    }
    *out_len = j;
    out[j] = 0;
}

static char* vpp_exec_cli(const char *cmd) {
    static char buffer[BUFFER_SIZE];
    char vppctl_cmd[512];
    FILE *fp;
    size_t total = 0;
    
    /* Build vppctl command - remove trailing newline from cmd if present */
    char clean_cmd[256];
    strncpy(clean_cmd, cmd, sizeof(clean_cmd) - 1);
    clean_cmd[sizeof(clean_cmd) - 1] = 0;
    size_t len = strlen(clean_cmd);
    if (len > 0 && clean_cmd[len-1] == '\n') {
        clean_cmd[len-1] = 0;
    }
    
    /* Use vppctl which is much faster than raw socket */
    snprintf(vppctl_cmd, sizeof(vppctl_cmd), 
             "vppctl -s %s '%s' 2>/dev/null", VPP_CLI_SOCKET, clean_cmd);
    
    fp = popen(vppctl_cmd, "r");
    if (!fp) {
        snprintf(buffer, BUFFER_SIZE, "Error: Cannot execute vppctl: %s\n", strerror(errno));
        return buffer;
    }
    
    /* Read output */
    buffer[0] = 0;
    while (fgets(buffer + total, BUFFER_SIZE - total, fp) != NULL) {
        total = strlen(buffer);
        if (total >= BUFFER_SIZE - 1) break;
    }
    
    pclose(fp);
    return buffer;
}

/* Get parameter value from context - returns LAST matching entry */
static const char* get_param(kcontext_t *context, const char *name) {
    const kpargv_t *pargv = NULL;
    const kparg_t *result_parg = NULL;
    
    if (!context || !name)
        return NULL;
        
    pargv = kcontext_pargv(context);
    if (!pargv)
        return NULL;
    
    /* Iterate all params and find the LAST one with matching name */
    kpargv_pargs_node_t *iter = kpargv_pargs_iter(pargv);
    kparg_t *p;
    while ((p = kpargv_pargs_each(&iter)) != NULL) {
        const kentry_t *entry = kparg_entry(p);
        const char *entry_name = entry ? kentry_name(entry) : NULL;
        if (entry_name && strcmp(entry_name, name) == 0) {
            result_parg = p;  /* Keep updating to get the last one */
        }
    }
    
    if (!result_parg)
        return NULL;
    
    return kparg_value(result_parg);
}

/* Show interfaces with IP addresses - Cisco style with MTU and multi-IP */
int vpp_show_interfaces(kcontext_t *context) {
    char iface_buf[BUFFER_SIZE];
    char addr_buf[BUFFER_SIZE];
    
    /* Get interface list */
    const char *ifaces = vpp_exec_cli("show interface\n");
    strncpy(iface_buf, ifaces, BUFFER_SIZE - 1);
    iface_buf[BUFFER_SIZE - 1] = 0;
    
    /* Get IP addresses */
    const char *addrs = vpp_exec_cli("show interface addr\n");
    strncpy(addr_buf, addrs, BUFFER_SIZE - 1);
    addr_buf[BUFFER_SIZE - 1] = 0;
    
    /* Interface data structure - supports multiple IPs */
    struct {
        char name[64];
        char ips[8][48];  /* Max 8 IPs per interface */
        int ip_count;
        int mtu;
        int is_up;
    } interfaces[128];
    int iface_count = 0;
    
    /* Parse interface names, status, and MTU from "show interface" output */
    /* Format: Name  Idx  State  MTU (L3/IP4/IP6/MPLS) */
    char *line = strtok(iface_buf, "\n");
    while (line && iface_count < 128) {
        /* Skip header line and empty lines */
        if (strstr(line, "Name") || strlen(line) < 5) {
            line = strtok(NULL, "\n");
            continue;
        }
        
        /* Skip counter lines (indented lines with rx/tx/drops) */
        if (line[0] == ' ') {
            line = strtok(NULL, "\n");
            continue;
        }
        
        /* Parse: "local0  0  down  0/0/0/0" or "loop0  1  up  9000/0/0/0" */
        char name[64] = {0};
        int idx;
        char state[16] = {0};
        int mtu_l3 = 9000;
        
        if (sscanf(line, "%63s %d %15s %d", name, &idx, state, &mtu_l3) >= 3) {
            strncpy(interfaces[iface_count].name, name, 63);
            interfaces[iface_count].is_up = (strstr(state, "up") != NULL);
            interfaces[iface_count].mtu = mtu_l3;
            interfaces[iface_count].ip_count = 0;
            iface_count++;
        }
        
        line = strtok(NULL, "\n");
    }
    
    /* Parse IP addresses from "show interface addr" output */
    char *addr_line = strtok(addr_buf, "\n");
    char current_iface[64] = {0};
    int current_idx = -1;
    
    while (addr_line) {
        /* Lines like "loop0 (up):" indicate interface name */
        /* Lines like "  L3 127.0.0.2/8" indicate IP address */
        
        if (addr_line[0] != ' ' && strchr(addr_line, '(')) {
            /* This is an interface line: "loop0 (up):" */
            sscanf(addr_line, "%63s", current_iface);
            /* Find matching interface index */
            current_idx = -1;
            for (int i = 0; i < iface_count; i++) {
                if (strcmp(interfaces[i].name, current_iface) == 0) {
                    current_idx = i;
                    break;
                }
            }
        } else if (strstr(addr_line, "L3 ") && current_idx >= 0) {
            /* This is an IP line: "  L3 192.168.1.1/24" */
            char *ip_start = strstr(addr_line, "L3 ");
            if (ip_start && interfaces[current_idx].ip_count < 8) {
                ip_start += 3; /* Skip "L3 " */
                /* Keep full IP with prefix */
                char ip[48] = {0};
                sscanf(ip_start, "%47s", ip);
                strncpy(interfaces[current_idx].ips[interfaces[current_idx].ip_count], ip, 47);
                interfaces[current_idx].ip_count++;
            }
        }
        
        addr_line = strtok(NULL, "\n");
    }
    
    /* Print header */
    kcontext_printf(context, "%-16s %-20s %-6s %-6s %s\n",
        "Interface", "IP-Address", "MTU", "Status", "Protocol");
    
    /* Print formatted table */
    for (int i = 0; i < iface_count; i++) {
        if (interfaces[i].ip_count == 0) {
            /* No IP assigned */
            kcontext_printf(context, "%-16s %-20s %-6d %-6s %s\n",
                interfaces[i].name,
                "unassigned",
                interfaces[i].mtu,
                interfaces[i].is_up ? "up" : "down",
                interfaces[i].is_up ? "up" : "down");
        } else {
            /* First IP with interface name */
            kcontext_printf(context, "%-16s %-20s %-6d %-6s %s\n",
                interfaces[i].name,
                interfaces[i].ips[0],
                interfaces[i].mtu,
                interfaces[i].is_up ? "up" : "down",
                interfaces[i].is_up ? "up" : "down");
            
            /* Additional IPs on separate lines with blank interface name */
            for (int j = 1; j < interfaces[i].ip_count; j++) {
                kcontext_printf(context, "%-16s %-20s\n",
                    "",
                    interfaces[i].ips[j]);
            }
        }
    }
    
    return 0;
}

/* Show interface details */
int vpp_show_interface_detail(kcontext_t *context) {
    const char *result = vpp_exec_cli("show interface addr\n");
    kcontext_printf(context, "%s", result);
    return 0;
}

/* Show IP interface brief */
int vpp_show_ip_interface_brief(kcontext_t *context) {
    const char *result = vpp_exec_cli("show int addr\n");
    kcontext_printf(context, "%s", result);
    return 0;
}

/* Show running config (stub) */
int vpp_show_running_config(kcontext_t *context) {
    kcontext_printf(context, "!\n! VPP Running Configuration\n!\n");
    
    /* Show loopback interfaces */
    const char *iface_list = vpp_exec_cli("show interface");
    char iface_buf[BUFFER_SIZE];
    strncpy(iface_buf, iface_list, BUFFER_SIZE - 1);
    iface_buf[BUFFER_SIZE - 1] = 0;
    
    char *iline = strtok(iface_buf, "\n");
    while (iline) {
        if (iline[0] != ' ' && strncmp(iline, "loop", 4) == 0) {
            kcontext_printf(context, "create loopback interface\n");
        }
        iline = strtok(NULL, "\n");
    }
    
    /* Show bond interfaces */
    const char *bond_info = vpp_exec_cli("show bond");
    char bond_buf[BUFFER_SIZE];
    strncpy(bond_buf, bond_info, BUFFER_SIZE - 1);
    bond_buf[BUFFER_SIZE - 1] = 0;
    
    char *bline = strtok(bond_buf, "\n");
    while (bline) {
        if (strstr(bline, "BondEthernet")) {
            kcontext_printf(context, "create bond mode lacp load-balance l34\n");
        }
        bline = strtok(NULL, "\n");
    }
    
    /* Show VLAN subinterfaces */
    const char *sub_list = vpp_exec_cli("show interface");
    char sub_buf[BUFFER_SIZE];
    strncpy(sub_buf, sub_list, BUFFER_SIZE - 1);
    sub_buf[BUFFER_SIZE - 1] = 0;
    
    char *sline = strtok(sub_buf, "\n");
    while (sline) {
        if (sline[0] != ' ') {
            char name[64] = {0};
            if (sscanf(sline, "%63s", name) == 1) {
                char *dot = strchr(name, '.');
                if (dot && strncmp(name, "tap", 3) != 0) {
                    char parent[64] = {0};
                    size_t plen = dot - name;
                    if (plen >= sizeof(parent)) plen = sizeof(parent) - 1;
                    strncpy(parent, name, plen);
                    int vlan_id = atoi(dot + 1);
                    if (vlan_id > 0 && vlan_id < 4096) {
                        kcontext_printf(context, "create sub %s %d\n", parent, vlan_id);
                    }
                }
            }
        }
        sline = strtok(NULL, "\n");
    }
    
    kcontext_printf(context, "!\n");
    
    /* Show interface configuration */
    const char *addrs = vpp_exec_cli("show interface addr");
    char addr_buf[BUFFER_SIZE];
    strncpy(addr_buf, addrs, BUFFER_SIZE - 1);
    addr_buf[BUFFER_SIZE - 1] = 0;
    
    char *line = strtok(addr_buf, "\n");
    char current_iface[64] = {0};
    int skip_iface = 0;
    while (line) {
        if (line[0] != ' ' && strchr(line, '(')) {
            sscanf(line, "%63s", current_iface);
            skip_iface = (strncmp(current_iface, "tap", 3) == 0 ||
                         strcmp(current_iface, "local0") == 0);
            
            if (!skip_iface && strstr(line, "(up)") && current_iface[0]) {
                kcontext_printf(context, "!\ninterface %s\n", current_iface);
                kcontext_printf(context, " no shutdown\n");
            } else if (!skip_iface && current_iface[0]) {
                kcontext_printf(context, "!\ninterface %s\n", current_iface);
                kcontext_printf(context, " shutdown\n");
            }
        } else if (strstr(line, "L3 ") && current_iface[0] && !skip_iface) {
            char *ip_start = strstr(line, "L3 ");
            if (ip_start) {
                ip_start += 3;
                char ip[48] = {0};
                sscanf(ip_start, "%47s", ip);
                kcontext_printf(context, " ip address %s\n", ip);
            }
        }
        line = strtok(NULL, "\n");
    }
    
    /* Show LCP */
    kcontext_printf(context, "!\n");
    const char *lcp = vpp_exec_cli("show lcp");
    char lcp_buf[BUFFER_SIZE];
    strncpy(lcp_buf, lcp, BUFFER_SIZE - 1);
    lcp_buf[BUFFER_SIZE - 1] = 0;
    
    line = strtok(lcp_buf, "\n");
    while (line) {
        if (strstr(line, "itf-pair:")) {
            int idx;
            char vpp_if[64], tap_if[64], host_if[32];
            if (sscanf(line, "itf-pair: [%d] %63s %63s %31s", &idx, vpp_if, tap_if, host_if) >= 4) {
                kcontext_printf(context, "lcp create %s host-if %s\n", vpp_if, host_if);
            }
        }
        line = strtok(NULL, "\n");
    }
    
    kcontext_printf(context, "!\nend\n");
    return 0;
}

/* Configure interface IP address - format: ip address X.X.X.X/Y */
int vpp_config_interface_ip(kcontext_t *context) {
    const char *ip_prefix = get_param(context, "address");
    const char *iface = get_current_interface();
    char cmd[256];
    
    fprintf(stderr, "DEBUG vpp_config_interface_ip: iface='%s'\n", iface ? iface : "NULL");
    
    if (!iface || iface[0] == 0) {
        kcontext_printf(context, "Error: Not in interface configuration mode\n");
        return -1;
    }
    
    if (!ip_prefix) {
        kcontext_printf(context, "Error: IP address required (format: X.X.X.X/Y)\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "set interface ip address %s %s", iface, ip_prefix);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0 && (strstr(result, "error") != NULL || strstr(result, "failed") != NULL || strstr(result, "conflict") != NULL)) {
        kcontext_printf(context, "%s", result);
        return -1;
    } else {
        kcontext_printf(context, "IP address %s configured on %s\n", ip_prefix, iface);
    }
    return 0;
}

/* Remove interface IP address - format: no ip address X.X.X.X/Y */
int vpp_no_interface_ip(kcontext_t *context) {
    const char *ip_prefix = get_param(context, "address");
    const char *iface = get_current_interface();
    char cmd[256];
    
    if (!iface || iface[0] == 0) {
        kcontext_printf(context, "Error: Not in interface configuration mode\n");
        return -1;
    }
    
    if (!ip_prefix) {
        kcontext_printf(context, "Error: IP address required (format: X.X.X.X/Y)\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "set interface ip address del %s %s\n", iface, ip_prefix);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0 && strstr(result, "error") != NULL) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "IP address %s removed from %s\n", ip_prefix, iface);
    }
    return 0;
}

/* Configure interface IPv6 address - format: ipv6 address X:X:X::X/Y */
int vpp_config_interface_ipv6(kcontext_t *context) {
    const char *ip_prefix = get_param(context, "address");
    const char *iface = get_current_interface();
    char cmd[256];
    
    if (!iface || iface[0] == 0) {
        kcontext_printf(context, "Error: Not in interface configuration mode\n");
        return -1;
    }
    
    if (!ip_prefix) {
        kcontext_printf(context, "Error: IPv6 address required (format: X:X:X::X/Y)\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "set interface ip address %s %s\n", iface, ip_prefix);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0 && strstr(result, "error") != NULL) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "IPv6 address %s configured on %s\n", ip_prefix, iface);
    }
    return 0;
}

/* Remove interface IPv6 address */
int vpp_no_interface_ipv6(kcontext_t *context) {
    const char *ip_prefix = get_param(context, "address");
    const char *iface = get_current_interface();
    char cmd[256];
    
    if (!iface || iface[0] == 0) {
        kcontext_printf(context, "Error: Not in interface configuration mode\n");
        return -1;
    }
    
    if (!ip_prefix) {
        kcontext_printf(context, "Error: IPv6 address required\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "set interface ip address del %s %s\n", iface, ip_prefix);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0 && strstr(result, "error") != NULL) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "IPv6 address %s removed from %s\n", ip_prefix, iface);
    }
    return 0;
}

/* Set interface state up */
int vpp_interface_up(kcontext_t *context) {
    const char *iface = get_current_interface();
    char cmd[256];
    
    if (!iface || iface[0] == 0) {
        kcontext_printf(context, "Error: Not in interface configuration mode\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "set interface state %s up\n", iface);
    vpp_exec_cli(cmd);
    kcontext_printf(context, "Interface %s is now up\n", iface);
    return 0;
}

/* Set interface state down */
int vpp_interface_down(kcontext_t *context) {
    const char *iface = get_current_interface();
    char cmd[256];
    
    if (!iface || iface[0] == 0) {
        kcontext_printf(context, "Error: Not in interface configuration mode\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "set interface state %s down\n", iface);
    vpp_exec_cli(cmd);
    kcontext_printf(context, "Interface %s is now administratively down\n", iface);
    return 0;
}

/* Enter interface configuration mode - stores interface name */
int vpp_enter_interface(kcontext_t *context) {
    const char *iface = get_param(context, "interface");
    char cmd[256];
    
    fprintf(stderr, "DEBUG: vpp_enter_interface called, iface=%s\n", iface ? iface : "NULL");
    
    if (!iface) {
        kcontext_printf(context, "Error: Interface name required\n");
        return -1;
    }
    
    /* Check if it's a loopback interface (starts with 'loop') */
    if (strncmp(iface, "loop", 4) == 0) {
        int instance = 0;
        if (sscanf(iface, "loop%d", &instance) == 1) {
            /* Create loopback with specific instance number */
            snprintf(cmd, sizeof(cmd), "create loopback interface instance %d", instance);
            const char *result = vpp_exec_cli(cmd);
            
            /* Check if created or already exists */
            if (strstr(result, iface) || strlen(result) == 0) {
                kcontext_printf(context, "Loopback interface %s created\n", iface);
            } else if (strstr(result, "already exists") || strstr(result, "is in use")) {
                /* Already exists - OK */
            } else if (strlen(result) > 0) {
                kcontext_printf(context, "%s", result);
            }
        }
    }
    /* Check if it's a VLAN subinterface (contains a dot) */
    else if (strchr(iface, '.') != NULL) {
        const char *dot = strchr(iface, '.');
        /* Parse parent interface and VLAN ID */
        char parent[64] = {0};
        int vlan_id = 0;
        
        size_t parent_len = dot - iface;
        if (parent_len >= sizeof(parent)) parent_len = sizeof(parent) - 1;
        strncpy(parent, iface, parent_len);
        parent[parent_len] = 0;
        
        vlan_id = atoi(dot + 1);
        
        if (vlan_id > 0 && vlan_id < 4096) {
            /* Create subinterface: create sub <parent> <vlan_id> */
            snprintf(cmd, sizeof(cmd), "create sub %s %d", parent, vlan_id);
            const char *result = vpp_exec_cli(cmd);
            
            /* Check if created or already exists */
            if (strstr(result, iface) || strlen(result) == 0 || strstr(result, "already exists")) {
                kcontext_printf(context, "VLAN subinterface %s created\n", iface);
            } else if (strlen(result) > 0) {
                kcontext_printf(context, "%s", result);
            }
        }
    }
    
    set_current_interface(iface);
    fprintf(stderr, "DEBUG: current_interface set to '%s'\n", iface);
    return 0;
}

/* Exit interface configuration mode - clears interface name */
int vpp_exit_interface(kcontext_t *context) {
    clear_current_interface();
    return 0;
}

/* Set MTU for current interface */
int vpp_set_mtu(kcontext_t *context) {
    const char *mtu = get_param(context, "mtu");
    const char *iface = get_current_interface();
    char cmd[256];
    
    if (!iface || iface[0] == 0) {
        kcontext_printf(context, "Error: Not in interface configuration mode\n");
        return -1;
    }
    
    if (!mtu) {
        kcontext_printf(context, "Error: MTU value required\n");
        return -1;
    }
    
    /* VPP command: set interface mtu packet <value> <interface> */
    snprintf(cmd, sizeof(cmd), "set interface mtu packet %s %s\n", mtu, iface);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "MTU set to %s on %s\n", mtu, iface);
    }
    return 0;
}

/* Create LCP for current interface */
int vpp_lcp_create_current(kcontext_t *context) {
    const char *hostif = get_param(context, "hostif");
    const char *iface = get_current_interface();
    char cmd[256];
    
    if (!iface || iface[0] == 0) {
        kcontext_printf(context, "Error: Not in interface configuration mode\n");
        return -1;
    }
    
    if (!hostif) {
        kcontext_printf(context, "Error: Linux host interface name required\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "lcp create %s host-if %s\n", iface, hostif);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "LCP created: %s -> %s\n", iface, hostif);
    }
    return 0;
}

/* Delete LCP for current interface */
int vpp_lcp_delete_current(kcontext_t *context) {
    const char *iface = get_current_interface();
    char cmd[256];
    
    if (!iface || iface[0] == 0) {
        kcontext_printf(context, "Error: Not in interface configuration mode\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "lcp delete %s\n", iface);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "LCP deleted: %s\n", iface);
    }
    return 0;
}

/* Create loopback interface */
int vpp_create_loopback(kcontext_t *context) {
    const char *instance = get_param(context, "instance");
    char cmd[256];
    
    if (instance && strlen(instance) > 0) {
        snprintf(cmd, sizeof(cmd), "create loopback interface instance %s", instance);
    } else {
        snprintf(cmd, sizeof(cmd), "create loopback interface");
    }
    const char *result = vpp_exec_cli(cmd);
    kcontext_printf(context, "%s", result);
    return 0;
}

/* Create tap interface */
int vpp_create_tap(kcontext_t *context) {
    const char *name = get_param(context, "name");
    char cmd[256];
    
    if (name) {
        snprintf(cmd, sizeof(cmd), "create tap id 0 host-if-name %s\n", name);
    } else {
        snprintf(cmd, sizeof(cmd), "create tap id 0\n");
    }
    const char *result = vpp_exec_cli(cmd);
    kcontext_printf(context, "%s", result);
    return 0;
}

/* Show VPP version */
int vpp_show_version(kcontext_t *context) {
    const char *result = vpp_exec_cli("show version\n");
    kcontext_printf(context, "%s", result);
    return 0;
}

/* Show IP routes */
int vpp_show_ip_route(kcontext_t *context) {
    const char *result = vpp_exec_cli("show ip fib\n");
    kcontext_printf(context, "%s", result);
    return 0;
}

/* Add IP route */
int vpp_add_ip_route(kcontext_t *context) {
    const char *network = get_param(context, "network");
    const char *mask = get_param(context, "mask");
    const char *gateway = get_param(context, "gateway");
    char cmd[256];
    
    if (!network || !gateway) {
        kcontext_printf(context, "Error: Missing parameters\n");
        return -1;
    }
    
    /* Convert mask to prefix */
    int prefix = 24;
    if (mask) {
        if (strcmp(mask, "255.255.255.0") == 0) prefix = 24;
        else if (strcmp(mask, "255.255.0.0") == 0) prefix = 16;
        else if (strcmp(mask, "255.0.0.0") == 0) prefix = 8;
        else if (strcmp(mask, "0.0.0.0") == 0) prefix = 0;
    }
    
    snprintf(cmd, sizeof(cmd), "ip route add %s/%d via %s\n", network, prefix, gateway);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "Route added: %s/%d via %s\n", network, prefix, gateway);
    }
    return 0;
}

/* Delete IP route */
int vpp_del_ip_route(kcontext_t *context) {
    const char *network = get_param(context, "network");
    const char *mask = get_param(context, "mask");
    const char *gateway = get_param(context, "gateway");
    char cmd[256];
    
    if (!network || !gateway) {
        kcontext_printf(context, "Error: Missing parameters\n");
        return -1;
    }
    
    int prefix = 24;
    if (mask) {
        if (strcmp(mask, "255.255.255.0") == 0) prefix = 24;
        else if (strcmp(mask, "255.255.0.0") == 0) prefix = 16;
        else if (strcmp(mask, "255.0.0.0") == 0) prefix = 8;
    }
    
    snprintf(cmd, sizeof(cmd), "ip route del %s/%d via %s\n", network, prefix, gateway);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "Route deleted: %s/%d via %s\n", network, prefix, gateway);
    }
    return 0;
}

/* Show hardware info */
int vpp_show_hardware(kcontext_t *context) {
    const char *result = vpp_exec_cli("show hardware-interfaces\n");
    kcontext_printf(context, "%s", result);
    return 0;
}

/* Ping */
int vpp_ping(kcontext_t *context) {
    const char *target = get_param(context, "target");
    char cmd[256];
    
    if (!target) {
        kcontext_printf(context, "Error: Target IP required\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "ping %s repeat 5\n", target);
    const char *result = vpp_exec_cli(cmd);
    kcontext_printf(context, "%s", result);
    return 0;
}

/* Write memory (save config) - saves VPP running config to file */
#define CONFIG_FILE "/etc/vpp/klish-startup.conf"

int vpp_write_memory(kcontext_t *context) {
    FILE *fp;
    
    kcontext_printf(context, "Building configuration...\n");
    
    fp = fopen(CONFIG_FILE, "w");
    if (!fp) {
        kcontext_printf(context, "Error: Cannot write to %s: %s\n", CONFIG_FILE, strerror(errno));
        return -1;
    }
    
    fprintf(fp, "# VPP Klish Configuration - Auto-generated\n");
    fprintf(fp, "# Generated at startup\n\n");
    
    /* First: Create loopback interfaces */
    fprintf(fp, "# Loopback interfaces\n");
    const char *iface_list = vpp_exec_cli("show interface");
    char iface_buf[BUFFER_SIZE];
    strncpy(iface_buf, iface_list, BUFFER_SIZE - 1);
    iface_buf[BUFFER_SIZE - 1] = 0;
    
    int loop_count = 0;
    char *iline = strtok(iface_buf, "\n");
    while (iline) {
        if (iline[0] != ' ' && strncmp(iline, "loop", 4) == 0) {
            char name[64];
            if (sscanf(iline, "%63s", name) == 1) {
                /* Extract instance number from loop name (e.g., loop100 -> 100) */
                int instance = 0;
                if (sscanf(name, "loop%d", &instance) == 1) {
                    fprintf(fp, "create loopback interface instance %d\n", instance);
                } else {
                    fprintf(fp, "create loopback interface\n");
                }
                loop_count++;
            }
        }
        iline = strtok(NULL, "\n");
    }
    
    /* Second: Create bond interfaces */
    fprintf(fp, "\n# Bond interfaces\n");
    const char *bond_info = vpp_exec_cli("show bond");
    char bond_buf[BUFFER_SIZE];
    strncpy(bond_buf, bond_info, BUFFER_SIZE - 1);
    bond_buf[BUFFER_SIZE - 1] = 0;
    
    char *bline = strtok(bond_buf, "\n");
    while (bline) {
        /* Parse: "BondEthernet0" or similar bond interface info */
        if (strstr(bline, "BondEthernet")) {
            int bond_id = -1;
            if (sscanf(bline, "BondEthernet%d", &bond_id) == 1 || 
                sscanf(bline, " BondEthernet%d", &bond_id) == 1) {
                fprintf(fp, "create bond mode lacp load-balance l34\n");
            }
        }
        bline = strtok(NULL, "\n");
    }
    
    /* Third: Create VLAN subinterfaces */
    fprintf(fp, "\n# VLAN subinterfaces\n");
    const char *sub_list = vpp_exec_cli("show interface");
    char sub_buf[BUFFER_SIZE];
    strncpy(sub_buf, sub_list, BUFFER_SIZE - 1);
    sub_buf[BUFFER_SIZE - 1] = 0;
    
    char *sline = strtok(sub_buf, "\n");
    while (sline) {
        if (sline[0] != ' ') {
            char name[64] = {0};
            if (sscanf(sline, "%63s", name) == 1) {
                /* Check if it's a subinterface (contains dot) and not tap */
                char *dot = strchr(name, '.');
                if (dot && strncmp(name, "tap", 3) != 0) {
                    char parent[64] = {0};
                    int vlan_id = 0;
                    size_t plen = dot - name;
                    if (plen >= sizeof(parent)) plen = sizeof(parent) - 1;
                    strncpy(parent, name, plen);
                    vlan_id = atoi(dot + 1);
                    if (vlan_id > 0 && vlan_id < 4096) {
                        fprintf(fp, "create sub %s %d\n", parent, vlan_id);
                    }
                }
            }
        }
        sline = strtok(NULL, "\n");
    }
    
    fprintf(fp, "\n# Interface configuration\n");
    
    /* Get and save interface addresses */
    const char *addrs = vpp_exec_cli("show interface addr");
    char addr_buf[BUFFER_SIZE];
    strncpy(addr_buf, addrs, BUFFER_SIZE - 1);
    addr_buf[BUFFER_SIZE - 1] = 0;
    
    char *line = strtok(addr_buf, "\n");
    char current_iface[64] = {0};
    int skip_iface = 0;
    while (line) {
        if (line[0] != ' ' && strchr(line, '(')) {
            /* Interface line: "loop0 (up):" */
            sscanf(line, "%63s", current_iface);
            
            /* Skip tap interfaces (auto-created by LCP) and system interfaces */
            skip_iface = (strncmp(current_iface, "tap", 3) == 0 ||
                         strcmp(current_iface, "local0") == 0 ||
                         strcmp(current_iface, "drops") == 0 ||
                         strcmp(current_iface, "ip6") == 0);
            
            /* Save state only for non-skipped interfaces that are up */
            if (!skip_iface && strstr(line, "(up)") && current_iface[0]) {
                fprintf(fp, "set interface state %s up\n", current_iface);
            }
        } else if (strstr(line, "L3 ") && current_iface[0] && !skip_iface) {
            /* IP line: "  L3 192.168.1.1/24" - only for non-skipped interfaces */
            char *ip_start = strstr(line, "L3 ");
            if (ip_start) {
                ip_start += 3;
                char ip[48] = {0};
                sscanf(ip_start, "%47s", ip);
                fprintf(fp, "set interface ip address %s %s\n", current_iface, ip);
            }
        }
        line = strtok(NULL, "\n");
    }
    
    fprintf(fp, "\n");
    
    /* Get and save LCP configs */
    const char *lcp = vpp_exec_cli("show lcp");
    char lcp_buf[BUFFER_SIZE];
    strncpy(lcp_buf, lcp, BUFFER_SIZE - 1);
    lcp_buf[BUFFER_SIZE - 1] = 0;
    
    line = strtok(lcp_buf, "\n");
    while (line) {
        /* Parse: "itf-pair: [0] BondEthernet0 tap4096 bond0 2 type tap netns dataplane" */
        if (strstr(line, "itf-pair:")) {
            int idx;
            char vpp_if[64], tap_if[64], host_if[32];
            if (sscanf(line, "itf-pair: [%d] %63s %63s %31s", &idx, vpp_if, tap_if, host_if) >= 4) {
                fprintf(fp, "lcp create %s host-if %s\n", vpp_if, host_if);
            }
        }
        line = strtok(NULL, "\n");
    }
    
    fclose(fp);
    kcontext_printf(context, "[OK]\n");
    kcontext_printf(context, "Configuration saved to %s\n", CONFIG_FILE);
    return 0;
}

/* Create LCP (Linux Control Plane) interface */
int vpp_lcp_create(kcontext_t *context) {
    const char *iface = get_param(context, "interface");
    const char *hostif = get_param(context, "hostif");
    char cmd[256];
    
    if (!iface || !hostif) {
        kcontext_printf(context, "Error: Interface and host-if name required\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "lcp create %s host-if %s\n", iface, hostif);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "LCP created: %s -> %s\n", iface, hostif);
    }
    return 0;
}

/* Delete LCP interface */
int vpp_lcp_delete(kcontext_t *context) {
    const char *iface = get_param(context, "interface");
    char cmd[256];
    
    if (!iface) {
        kcontext_printf(context, "Error: Interface name required\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "lcp delete %s\n", iface);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "LCP deleted: %s\n", iface);
    }
    return 0;
}

/* Show LCP interfaces */
int vpp_show_lcp(kcontext_t *context) {
    const char *result = vpp_exec_cli("show lcp\n");
    kcontext_printf(context, "%s", result);
    return 0;
}

/* Create VLAN subinterface */
int vpp_create_subinterface(kcontext_t *context) {
    const char *iface = get_param(context, "interface");
    const char *subid = get_param(context, "subid");
    const char *vlanid = get_param(context, "vlanid");
    char cmd[256];
    
    if (!iface || !subid || !vlanid) {
        kcontext_printf(context, "Error: Interface, sub-id and vlan-id required\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "create sub %s %s dot1q %s exact-match\n", iface, subid, vlanid);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "Subinterface created: %s.%s (VLAN %s)\n", iface, subid, vlanid);
    }
    return 0;
}

/* Delete subinterface */
int vpp_delete_subinterface(kcontext_t *context) {
    const char *iface = get_param(context, "interface");
    char cmd[256];
    
    if (!iface) {
        kcontext_printf(context, "Error: Subinterface name required\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "delete sub %s", iface);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "Subinterface deleted: %s\n", iface);
    }
    return 0;
}

/* Delete loopback interface */
int vpp_delete_loopback(kcontext_t *context) {
    const char *iface = get_param(context, "interface");
    char cmd[256];
    
    if (!iface) {
        kcontext_printf(context, "Error: Loopback interface name required\n");
        return -1;
    }
    
    /* Check if it's a loopback interface */
    if (strncmp(iface, "loop", 4) != 0) {
        kcontext_printf(context, "Error: %s is not a loopback interface\n", iface);
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "delete loopback interface intfc %s", iface);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "Loopback deleted: %s\n", iface);
    }
    return 0;
}

/* Delete any interface (auto-detect type) */
int vpp_no_interface(kcontext_t *context) {
    const char *iface = get_param(context, "interface");
    char cmd[256];
    
    if (!iface) {
        kcontext_printf(context, "Error: Interface name required\n");
        return -1;
    }
    
    /* Determine interface type and delete accordingly */
    if (strncmp(iface, "loop", 4) == 0) {
        /* Loopback interface */
        snprintf(cmd, sizeof(cmd), "delete loopback interface intfc %s", iface);
    } else if (strchr(iface, '.') != NULL) {
        /* VLAN subinterface */
        snprintf(cmd, sizeof(cmd), "delete sub %s", iface);
    } else {
        kcontext_printf(context, "Error: Cannot delete %s - only loopback and VLAN subinterfaces can be deleted\n", iface);
        return -1;
    }
    
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "Interface deleted: %s\n", iface);
    }
    return 0;
}

/* Tab completion for interface names */
int vpp_complete_interface(kcontext_t *context) {
    char iface_buf[BUFFER_SIZE];
    
    /* Get interface list from VPP */
    const char *ifaces = vpp_exec_cli("show interface\n");
    strncpy(iface_buf, ifaces, BUFFER_SIZE - 1);
    iface_buf[BUFFER_SIZE - 1] = 0;
    
    /* Parse interface names and add to completion */
    char *line = strtok(iface_buf, "\n");
    while (line) {
        /* Skip header and indented counter lines */
        if (strstr(line, "Name") || line[0] == ' ' || strlen(line) < 3) {
            line = strtok(NULL, "\n");
            continue;
        }
        
        /* Extract interface name (first word) */
        char name[64] = {0};
        if (sscanf(line, "%63s", name) == 1) {
            /* Add to completion list */
            kcontext_printf(context, "%s\n", name);
        }
        
        line = strtok(NULL, "\n");
    }
    
    return 0;
}


/* Plugin initialization */

/* Show memory main-heap */
int vpp_show_memory_heap(kcontext_t *context) {
    const char *result = vpp_exec_cli("show memory main-heap\n");
    kcontext_printf(context, "%s", result);
    return 0;
}

/* Show memory map */
int vpp_show_memory_map(kcontext_t *context) {
    const char *result = vpp_exec_cli("show memory map\n");
    kcontext_printf(context, "%s", result);
    return 0;
}

/* Show buffers */
int vpp_show_buffers(kcontext_t *context) {
    const char *result = vpp_exec_cli("show buffers\n");
    kcontext_printf(context, "%s", result);
    return 0;
}

/* Show trace */
int vpp_show_trace(kcontext_t *context) {
    const char *result = vpp_exec_cli("show trace\n");
    kcontext_printf(context, "%s", result);
    return 0;
}

/* Show error */
int vpp_show_error(kcontext_t *context) {
    const char *result = vpp_exec_cli("show error\n");
    kcontext_printf(context, "%s", result);
    return 0;
}

/* Show PCI devices */
int vpp_show_pci(kcontext_t *context) {
    const char *result = vpp_exec_cli("show pci\n");
    kcontext_printf(context, "%s", result);
    return 0;
}

int kplugin_vpp_init(kcontext_t *context) {
    kplugin_t *plugin = NULL;

    if (!context)
        return -1;
    plugin = kcontext_plugin(context);
    if (!plugin)
        return -1;

    /* Register symbols */
    kplugin_add_syms(plugin, ksym_new("vpp_show_interfaces", vpp_show_interfaces));
    kplugin_add_syms(plugin, ksym_new("vpp_show_interface_detail", vpp_show_interface_detail));
    kplugin_add_syms(plugin, ksym_new("vpp_show_ip_interface_brief", vpp_show_ip_interface_brief));
    kplugin_add_syms(plugin, ksym_new("vpp_show_running_config", vpp_show_running_config));
    kplugin_add_syms(plugin, ksym_new("vpp_config_interface_ip", vpp_config_interface_ip));
    kplugin_add_syms(plugin, ksym_new("vpp_no_interface_ip", vpp_no_interface_ip));
    kplugin_add_syms(plugin, ksym_new("vpp_config_interface_ipv6", vpp_config_interface_ipv6));
    kplugin_add_syms(plugin, ksym_new("vpp_no_interface_ipv6", vpp_no_interface_ipv6));
    kplugin_add_syms(plugin, ksym_new("vpp_interface_up", vpp_interface_up));
    kplugin_add_syms(plugin, ksym_new("vpp_interface_down", vpp_interface_down));
    kplugin_add_syms(plugin, ksym_new("vpp_enter_interface", vpp_enter_interface));
    kplugin_add_syms(plugin, ksym_new("vpp_exit_interface", vpp_exit_interface));
    kplugin_add_syms(plugin, ksym_new("vpp_set_mtu", vpp_set_mtu));
    kplugin_add_syms(plugin, ksym_new("vpp_lcp_create_current", vpp_lcp_create_current));
    kplugin_add_syms(plugin, ksym_new("vpp_lcp_delete_current", vpp_lcp_delete_current));
    kplugin_add_syms(plugin, ksym_new("vpp_create_loopback", vpp_create_loopback));
    kplugin_add_syms(plugin, ksym_new("vpp_create_tap", vpp_create_tap));
    kplugin_add_syms(plugin, ksym_new("vpp_show_version", vpp_show_version));
    kplugin_add_syms(plugin, ksym_new("vpp_show_ip_route", vpp_show_ip_route));
    kplugin_add_syms(plugin, ksym_new("vpp_add_ip_route", vpp_add_ip_route));
    kplugin_add_syms(plugin, ksym_new("vpp_del_ip_route", vpp_del_ip_route));
    kplugin_add_syms(plugin, ksym_new("vpp_show_hardware", vpp_show_hardware));
    kplugin_add_syms(plugin, ksym_new("vpp_ping", vpp_ping));
    kplugin_add_syms(plugin, ksym_new("vpp_write_memory", vpp_write_memory));
    kplugin_add_syms(plugin, ksym_new("vpp_lcp_create", vpp_lcp_create));
    kplugin_add_syms(plugin, ksym_new("vpp_lcp_delete", vpp_lcp_delete));
    kplugin_add_syms(plugin, ksym_new("vpp_show_lcp", vpp_show_lcp));
    kplugin_add_syms(plugin, ksym_new("vpp_create_subinterface", vpp_create_subinterface));
    kplugin_add_syms(plugin, ksym_new("vpp_delete_subinterface", vpp_delete_subinterface));
    kplugin_add_syms(plugin, ksym_new("vpp_delete_loopback", vpp_delete_loopback));
    kplugin_add_syms(plugin, ksym_new("vpp_no_interface", vpp_no_interface));
    kplugin_add_syms(plugin, ksym_new("vpp_complete_interface", vpp_complete_interface));
    kplugin_add_syms(plugin, ksym_new("vpp_show_memory_heap", vpp_show_memory_heap));
    kplugin_add_syms(plugin, ksym_new("vpp_show_memory_map", vpp_show_memory_map));
    kplugin_add_syms(plugin, ksym_new("vpp_show_buffers", vpp_show_buffers));
    kplugin_add_syms(plugin, ksym_new("vpp_show_trace", vpp_show_trace));
    kplugin_add_syms(plugin, ksym_new("vpp_show_error", vpp_show_error));
    kplugin_add_syms(plugin, ksym_new("vpp_show_pci", vpp_show_pci));

    /* Check if VPP is running */
    if (access(VPP_CLI_SOCKET, F_OK) != 0) {
        fprintf(stderr, "Warning: VPP CLI socket not found. VPP may not be running.\n");
    }
    return 0;
}

/* Plugin finalization */  
int kplugin_vpp_fini(kcontext_t *context) {
    (void)context;
    if (vpp_cli_fd >= 0) {
        close(vpp_cli_fd);
        vpp_cli_fd = -1;
    }
    return 0;
}
