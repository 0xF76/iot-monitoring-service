#include "protocol.h"

#include <bits/types/struct_timeval.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>


#define DISCOVERY_MCAST_ADDR "239.0.0.1"
#define DISCOVERY_PORT 5000
#define RX_BUFF_SIZE 1024
#define LINE_BUFF_SIZE 256


typedef enum {
    CMD_NONE = 0,
    CMD_HELP,
    CMD_LIST,
    CMD_GET,
    CMD_SET,
    CMD_EXIT
} command_type_t;


typedef struct {
    command_type_t type;
    uint32_t id;
    float temp;
} command_t;


static void trim_newline(char *str);
static char *skip_spaces(char *str);
static char *next_token(char **strp);


static void print_device(const device_status_t *dev);
static void print_help(void);

static int parse_command(char *line, command_t *cmd);

static int cmd_list(int fd);
static int cmd_get(int fd, uint32_t id);
static int cmd_set(int fd, uint32_t id, float temp);

static int recv_expect(int fd, uint16_t expected_type, void *buf, uint16_t bufsize, uint16_t *out_len);

static int discover_server(char *out_ip, size_t ip_size, uint16_t *out_port);

static int connect_to_server(const char *ip, const char *port);

int client_run(void) {

    char ip[INET_ADDRSTRLEN];
    uint16_t port = 0;

    if(discover_server(ip, sizeof(ip), &port) < 0) {
        printf("[client] server discovery failed\n");
        return 1;
    }

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", port);

    int fd = connect_to_server(ip, port_str);
    if(fd < 0) {
        return 1;
    }

    printf("[client] connected to server\n");
    print_help();

    char line[LINE_BUFF_SIZE];

    while(1) {
        printf("> ");
        fflush(stdout);

        if(!fgets(line, sizeof(line), stdin)) {
            printf("\n[client] EOF on stdin, exiting\n");
            break;
        }
        trim_newline(line);

        command_t cmd;
        int prc = parse_command(line, &cmd);

        if(prc == 0 && cmd.type == CMD_NONE) {
            continue;
        } else if(prc == -1) {
            printf("usage:\n");
            print_help();
            continue;
        } else if(prc == -2) {
            printf("unknown command: '%s'\n", line);
            print_help();
            continue;
        }

        int rc = 0;

        switch(cmd.type) {
            case CMD_HELP:
                print_help();
                break;
            case CMD_LIST:
                rc = cmd_list(fd);
                break;
            case CMD_GET:
                rc = cmd_get(fd, cmd.id);
                break;
            case CMD_SET:
                rc = cmd_set(fd, cmd.id, cmd.temp);
                break;
            case CMD_EXIT:
                printf("[client] exiting on user request\n");
                close(fd);
                return 0;
                break;
            default:
                break;
        }

        if(rc == 1) {
            break;
        } else if(rc < 0) {
            printf("[client] command failed\n");
            break;
        }
    }
    close(fd);
    return 0;
}


static void trim_newline(char *str) {
    str[strcspn(str, "\r\n")] = '\0';
}

static char *skip_spaces(char *str) {
    while (*str == ' ') str++;

    return str;
}

static char *next_token(char **strp) {
    char *s = skip_spaces(*strp);
    if (*s == '\0') {
        *strp = s;
        return NULL;
    }

    char *start = s;
    while(*s != '\0' && *s != ' ' && *s != '\t') s++;

    if (*s != '\0') {
        *s = '\0';
        s++;
    }
    *strp = s;
    return start;
}

static void print_device(const device_status_t *dev) {
    printf("  id=%u  temp=%.2f C  batt=%u%%  status=%u\n",
           dev->device_id,
           dev->temperature,
           dev->battery,
           dev->status);
}

static void print_help(void) {
    printf("Available commands:\n");
    printf("  list             - show all devices\n");
    printf("  get <id>         - show details of selected device\n");
    printf("  set <id> <temp>  - set temperature of selected device\n");
    printf("  help             - show this help\n");
    printf("  exit / quit      - close connection and exit\n");
}

static int parse_command(char *line, command_t *cmd) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = CMD_NONE;

    char *p = line;
    char *token = next_token(&p);
    if(!token) return 0;

    if(strcmp(token, "help") == 0) {
        cmd->type = CMD_HELP;
        return 0;
    }
    if(strcmp(token, "list") == 0) {
        cmd->type = CMD_LIST;
        return 0;
    }
    if(strcmp(token, "exit") == 0 || strcmp(token, "quit") == 0) {
        cmd->type = CMD_EXIT;
        return 0;
    }
    if(strcmp(token, "get") == 0) {
        char *id_str = next_token(&p);
        if(!id_str) {
            return -1;
        }
        uint32_t id = (uint32_t)strtoul(id_str, NULL, 10);
        cmd->type = CMD_GET;
        cmd->id = id;
        return 0;
    }
    if(strcmp(token, "set") == 0) {
        char *id_str = next_token(&p);
        char *temp_str = next_token(&p);
        if(!id_str || !temp_str) {
            return -1;
        }

        uint32_t id = (uint32_t)strtoul(id_str, NULL, 10);
        float temp = strtof(temp_str, NULL);
        cmd->type = CMD_SET;
        cmd->id = id;
        cmd->temp = temp;
        return 0;
    }

    return -2; // unknown command
}

static int cmd_list(int fd) {
    int status = send_tlv(fd, TLV_TYPE_LIST_REQUEST, NULL, 0);
    if (status < 0) {
        printf("[client] send_tlv LIST_REQUEST failed\n");
        return -1;
    }

    uint8_t rx[RX_BUFF_SIZE];
    uint16_t len = 0;

    status = recv_expect(fd, TLV_TYPE_LIST_RESPONSE, rx, sizeof(rx), &len);
    if (status != 0) return status;

    if(len % sizeof(device_status_t) != 0) {
        printf("[client] invalid LIST_RESPONSE length=%u\n", len);
        return -1;
    }

    size_t count = len / sizeof(device_status_t);
    printf("[client] received %zu devices:\n", count);

    device_status_t *devs = (device_status_t *)rx;
    for(size_t i = 0; i < count; ++i) {
        print_device(&devs[i]);
    }
    return 0;
}

static int cmd_get(int fd, uint32_t id) {
    uint32_t id_net = htonl(id);
    int status = send_tlv(fd, TLV_TYPE_GET_REQUEST, &id_net, sizeof(id_net));
    if(status < 0) {
        printf("[client] send_tlv GET_REQUEST failed\n");
        return -1;
    }

    uint8_t rx[RX_BUFF_SIZE];
    uint16_t len = 0;

    status = recv_expect(fd, TLV_TYPE_GET_RESPONSE, rx, sizeof(rx), &len);
    if (status != 0) return status;

    if(len == 0) {
        printf("[client] device %u not found\n", id);
        return 0;
    }

    if(len != sizeof(device_status_t)) {
        printf("[client] invalid GET_RESPONSE length=%u\n", len);
        return -1;
    }

    device_status_t dev;
    memcpy(&dev, rx, sizeof(dev));
    printf("[client] device details:\n");
    print_device(&dev);
    return 0;
}

static int cmd_set(int fd, uint32_t id, float temp) {
    uint32_t payload[2];
    payload[0] = htonl(id);
    uint32_t temp_bits;
    memcpy(&temp_bits, &temp, sizeof(float));
    payload[1] = htonl(temp_bits);

    int status = send_tlv(fd, TLV_TYPE_SET_REQUEST, payload, sizeof(payload));
    if(status < 0) {
        printf("[client] send_tlv SET_REQUEST failed\n");
        return -1;
    }

    uint8_t rx[RX_BUFF_SIZE];
    uint16_t len = 0;

    status = recv_expect(fd, TLV_TYPE_SET_RESPONSE, rx, sizeof(rx), &len);
    if (status != 0) return status;

    if(len != 1) {
        printf("[client] invalid SET_RESPONSE length=%u\n", len);
        return -1;
    }

    uint8_t code = rx[0];
    if(code == 0) {
        printf("[client] SET successful for device %u\n", id);
    } else if(code == 1) {
        printf("[client] SET failed: device %u not found\n", id);
    } else if(code == 2) {
        printf("[client] SET failed: bad request\n");
    } else {
        printf("[client] SET failed: unknown error code %u\n", code);
    }

    return 0;

}

static int recv_expect(int fd, uint16_t expected_type, void *buf, uint16_t bufsize, uint16_t *out_len) {
    uint16_t type = 0, len = 0;
    int rc = recv_tlv(fd, &type, buf, bufsize, &len);
    if (rc == 1) {
        printf("[client] server closed connection (EOF)\n");
        return 1;
    } else if (rc < 0) {
        printf("[client] recv_tlv failed\n");
        return -1;
    }
    if (type != expected_type) {
        printf("[client] unexpected response type=0x%04x\n", type);
        return -1;
    }
    
    if (out_len) *out_len = len;
    return 0;
}

static int discover_server(char *out_ip, size_t ip_size, uint16_t *out_port) {
    printf("Searching for server...\n");
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0) {
        perror("discovery socket");
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    struct sockaddr_in mcast_addr;
    memset(&mcast_addr, 0, sizeof(mcast_addr));
    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_port = htons(DISCOVERY_PORT);
    inet_pton(AF_INET, DISCOVERY_MCAST_ADDR, &mcast_addr.sin_addr);

    uint8_t tx[1024];
    size_t tx_len = 0;
    int rc = tlv_encode_buf(tx, sizeof(tx), TLV_TYPE_DISCOVER_REQUEST, NULL, 0, &tx_len);
    if(rc < 0) {
        printf("[client] tlv_encode_buf DISCOVER_REQUEST failed\n");
        close(fd);
        return -1;
    }

    ssize_t n = sendto(fd, tx, tx_len, 0, (struct sockaddr *)&mcast_addr, sizeof(mcast_addr));
    if(n < 0) {
        perror("discovery sendto");
        close(fd);
        return -1;
    }

    uint8_t rx[1024];
    struct sockaddr_in src_addr;
    socklen_t src_addr_len = sizeof(src_addr);
    n = recvfrom(fd, rx, sizeof(rx), 0, (struct sockaddr *)&src_addr, &src_addr_len);
    if(n < 0) {
        perror("discovery recvfrom");
        close(fd);
        return -1;
    }

    uint16_t type = 0, len = 0;
    const uint8_t *val = NULL;
    rc = tlv_decode_buf(rx, (size_t)n, &type, &val, &len);
    if(rc < 0) {
        printf("[client] discovery tlv_decode_buf failed\n");
        close(fd);
        return -1;
    }

    if(type != TLV_TYPE_DISCOVER_RESPONSE) {
        printf("[client] unexpected discovery response type=0x%04x\n", type);
        close(fd);
        return -1;
    }

    uint16_t port_net;
    memcpy(&port_net, val, sizeof(port_net));
    *out_port = ntohs(port_net);

    inet_ntop(AF_INET, &src_addr.sin_addr, out_ip, ip_size);
    close(fd);
    printf("Found server at %s:%u\n", out_ip, *out_port);
    return 0;
}

static int connect_to_server(const char *ip, const char *port) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(ip, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        perror("socket");
        freeaddrinfo(res);
        return -1;
    }

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("connect");
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return fd;
}