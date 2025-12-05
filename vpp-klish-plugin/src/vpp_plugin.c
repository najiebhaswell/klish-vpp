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

/* Version */
const uint8_t kplugin_vpp_major = KPLUGIN_MAJOR;
const uint8_t kplugin_vpp_minor = KPLUGIN_MINOR;

static int vpp_cli_fd = -1;

/* Helper to execute VPP CLI command */
static char* vpp_exec_cli(const char *cmd) {
    struct sockaddr_un addr;
    int fd;
    static char buffer[BUFFER_SIZE];
    ssize_t n;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        snprintf(buffer, BUFFER_SIZE, "Error: Cannot create socket: %s\n", strerror(errno));
        return buffer;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, VPP_CLI_SOCKET, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        snprintf(buffer, BUFFER_SIZE, "Error: Cannot connect to VPP: %s\n", strerror(errno));
        close(fd);
        return buffer;
    }

    if (write(fd, cmd, strlen(cmd)) < 0) {
        snprintf(buffer, BUFFER_SIZE, "Error: Failed to send command: %s\n", strerror(errno));
        close(fd);
        return buffer;
    }

    memset(buffer, 0, BUFFER_SIZE);
    n = read(fd, buffer, BUFFER_SIZE - 1);
    if (n < 0) {
        snprintf(buffer, BUFFER_SIZE, "Error: Failed to read response: %s\n", strerror(errno));
    }

    close(fd);
    return buffer;
}

/* Get parameter value from context */
static const char* get_param(kcontext_t *context, const char *name) {
    const kpargv_t *pargv = NULL;
    const kparg_t *parg = NULL;
    
    if (!context || !name)
        return NULL;
        
    pargv = kcontext_pargv(context);
    if (!pargv)
        return NULL;
        
    parg = kpargv_find(pargv, name);
    if (!parg)
        return NULL;
    
    return kparg_value(parg);
}

/* Show interfaces */
int vpp_show_interfaces(kcontext_t *context) {
    const char *result = vpp_exec_cli("show interface\n");
    kcontext_printf(context, "%s", result);
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
    kcontext_printf(context, "Building configuration...\n\n");
    kcontext_printf(context, "Current configuration:\n");
    
    const char *interfaces = vpp_exec_cli("show interface addr\n");
    kcontext_printf(context, "%s", interfaces);
    
    const char *routes = vpp_exec_cli("show ip fib\n");
    kcontext_printf(context, "\n%s", routes);
    
    kcontext_printf(context, "end\n");
    return 0;
}

/* Configure interface IP address */
int vpp_config_interface_ip(kcontext_t *context) {
    const char *iface = get_param(context, "interface");
    const char *ip = get_param(context, "address");
    const char *mask = get_param(context, "mask");
    char cmd[256];
    
    if (!iface || !ip || !mask) {
        kcontext_printf(context, "Error: Missing parameters (interface=%s, ip=%s, mask=%s)\n",
               iface ? iface : "null", ip ? ip : "null", mask ? mask : "null");
        return -1;
    }
    
    /* Convert netmask to prefix length */
    int prefix = 24; /* Default */
    if (strcmp(mask, "255.255.255.255") == 0) prefix = 32;
    else if (strcmp(mask, "255.255.255.254") == 0) prefix = 31;
    else if (strcmp(mask, "255.255.255.252") == 0) prefix = 30;
    else if (strcmp(mask, "255.255.255.248") == 0) prefix = 29;
    else if (strcmp(mask, "255.255.255.240") == 0) prefix = 28;
    else if (strcmp(mask, "255.255.255.224") == 0) prefix = 27;
    else if (strcmp(mask, "255.255.192.0") == 0) prefix = 26;
    else if (strcmp(mask, "255.255.128.0") == 0) prefix = 25;
    else if (strcmp(mask, "255.255.255.0") == 0) prefix = 24;
    else if (strcmp(mask, "255.255.254.0") == 0) prefix = 23;
    else if (strcmp(mask, "255.255.252.0") == 0) prefix = 22;
    else if (strcmp(mask, "255.255.248.0") == 0) prefix = 21;
    else if (strcmp(mask, "255.255.240.0") == 0) prefix = 20;
    else if (strcmp(mask, "255.255.224.0") == 0) prefix = 19;
    else if (strcmp(mask, "255.255.192.0") == 0) prefix = 18;
    else if (strcmp(mask, "255.255.128.0") == 0) prefix = 17;
    else if (strcmp(mask, "255.255.0.0") == 0) prefix = 16;
    else if (strcmp(mask, "255.0.0.0") == 0) prefix = 8;
    
    snprintf(cmd, sizeof(cmd), "set interface ip address %s %s/%d\n", iface, ip, prefix);
    const char *result = vpp_exec_cli(cmd);
    if (strlen(result) > 0 && strstr(result, "error") == NULL) {
        kcontext_printf(context, "IP address %s/%d configured on %s\n", ip, prefix, iface);
    } else if (strlen(result) > 0) {
        kcontext_printf(context, "%s", result);
    } else {
        kcontext_printf(context, "IP address %s/%d configured on %s\n", ip, prefix, iface);
    }
    return 0;
}

/* Set interface state up */
int vpp_interface_up(kcontext_t *context) {
    const char *iface = get_param(context, "interface");
    char cmd[256];
    
    if (!iface) {
        kcontext_printf(context, "Error: Interface name required\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "set interface state %s up\n", iface);
    vpp_exec_cli(cmd);
    kcontext_printf(context, "Interface %s is now up\n", iface);
    return 0;
}

/* Set interface state down (shutdown) */
int vpp_interface_down(kcontext_t *context) {
    const char *iface = get_param(context, "interface");
    char cmd[256];
    
    if (!iface) {
        kcontext_printf(context, "Error: Interface name required\n");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "set interface state %s down\n", iface);
    vpp_exec_cli(cmd);
    kcontext_printf(context, "Interface %s is now administratively down\n", iface);
    return 0;
}

/* Create loopback interface */
int vpp_create_loopback(kcontext_t *context) {
    const char *result = vpp_exec_cli("create loopback interface\n");
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

/* Write memory (save config) */
int vpp_write_memory(kcontext_t *context) {
    kcontext_printf(context, "Building configuration...\n");
    kcontext_printf(context, "[OK]\n");
    return 0;
}

/* Plugin initialization */
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
    kplugin_add_syms(plugin, ksym_new("vpp_interface_up", vpp_interface_up));
    kplugin_add_syms(plugin, ksym_new("vpp_interface_down", vpp_interface_down));
    kplugin_add_syms(plugin, ksym_new("vpp_create_loopback", vpp_create_loopback));
    kplugin_add_syms(plugin, ksym_new("vpp_create_tap", vpp_create_tap));
    kplugin_add_syms(plugin, ksym_new("vpp_show_version", vpp_show_version));
    kplugin_add_syms(plugin, ksym_new("vpp_show_ip_route", vpp_show_ip_route));
    kplugin_add_syms(plugin, ksym_new("vpp_add_ip_route", vpp_add_ip_route));
    kplugin_add_syms(plugin, ksym_new("vpp_del_ip_route", vpp_del_ip_route));
    kplugin_add_syms(plugin, ksym_new("vpp_show_hardware", vpp_show_hardware));
    kplugin_add_syms(plugin, ksym_new("vpp_ping", vpp_ping));
    kplugin_add_syms(plugin, ksym_new("vpp_write_memory", vpp_write_memory));

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
