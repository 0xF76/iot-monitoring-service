#include "protocol.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#define SERVER_PORT "5001"

static void print_device(const device_status_t *dev)
{
    printf("  id=%u  temp=%.2f C  batt=%u%%  status=%u\n",
           dev->device_id,
           dev->temperature,
           dev->battery,
           dev->status);
}

static int handle_list(int fd)
{
    if (send_tlv(fd, TLV_TYPE_LIST_REQUEST, NULL, 0) < 0) {
        printf("[client] send_tlv LIST_REQUEST failed\n");
        return -1;
    }

    uint8_t buffer[1024];
    uint16_t type = 0;
    uint16_t length = 0;

    int rc = recv_tlv(fd, &type, buffer, sizeof(buffer), &length);
    if (rc == 1) {
        printf("[client] server closed connection (EOF)\n");
        return 1;
    } else if (rc < 0) {
        printf("[client] recv_tlv failed\n");
        return -1;
    }

    if (type != TLV_TYPE_LIST_RESPONSE) {
        printf("[client] unexpected response type=0x%04x\n", type);
        return -1;
    }

    if (length % sizeof(device_status_t) != 0) {
        printf("[client] invalid LIST_RESPONSE length=%u\n", length);
        return -1;
    }

    size_t count = length / sizeof(device_status_t);
    printf("[client] received %zu devices:\n", count);

    device_status_t *devs = (device_status_t *)buffer;
    for (size_t i = 0; i < count; ++i) {
        print_device(&devs[i]);
    }

    return 0;
}

static int handle_get(int fd, uint32_t id)
{
    uint32_t id_net = htonl(id);
    if (send_tlv(fd, TLV_TYPE_GET_REQUEST, &id_net, sizeof(id_net)) < 0) {
        printf("[client] send_tlv GET_REQUEST failed\n");
        return -1;
    }

    uint8_t buffer[1024];
    uint16_t type = 0;
    uint16_t length = 0;

    int rc = recv_tlv(fd, &type, buffer, sizeof(buffer), &length);
    if (rc == 1) {
        printf("[client] server closed connection (EOF)\n");
        return 1;
    } else if (rc < 0) {
        printf("[client] recv_tlv failed\n");
        return -1;
    }

    if (type != TLV_TYPE_GET_RESPONSE) {
        printf("[client] unexpected response type=0x%04x\n", type);
        return -1;
    }

    if (length == 0) {
        printf("[client] device %u not found\n", id);
        return 0;
    }

    if (length != sizeof(device_status_t)) {
        printf("[client] invalid GET_RESPONSE length=%u\n", length);
        return -1;
    }

    device_status_t dev;
    memcpy(&dev, buffer, sizeof(dev));

    printf("[client] device details:\n");
    print_device(&dev);
    return 0;
}

static int handle_set(int fd, uint32_t id, float temp) {
    uint32_t payload[2];
    int status;
    payload[0] = htonl(id);
    memcpy(&payload[1], &temp, sizeof(float));

    status = send_tlv(fd, TLV_TYPE_SET_REQUEST, payload, sizeof(payload));
    if (status < 0) {
        printf("[client] send_tlv SET_REQUEST failed\n");
        return -1;
    }

    uint8_t buffer[1024];
    uint16_t type = 0;
    uint16_t length = 0;
    status = recv_tlv(fd, &type, buffer, sizeof(buffer), &length);
    if (status == 1) {
        printf("[client] server closed connection (EOF)\n");
        return 1;
    } else if (status < 0) {
        printf("[client] recv_tlv failed\n");
        return -1;
    }

    if (type != TLV_TYPE_SET_RESPONSE) {
        printf("[client] unexpected response type=0x%04x\n", type);
        return -1;
    }

    if (length != 1) {
        printf("[client] invalid SET_RESPONSE length=%u\n", length);
        return -1;
    }

    uint8_t code = buffer[0];
    if (code == 0) {
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

static void print_help(void)
{
    printf("Available commands:\n");
    printf("  list             - show all devices\n");
    printf("  get <id>         - show details of selected device\n");
    printf("  set <id> <temp>  - set temperature of selected device\n");
    printf("  help             - show this help\n");
    printf("  exit / quit      - close connection and exit\n");
}

int client_run(void)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo("127.0.0.1", SERVER_PORT, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return 1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        perror("socket");
        freeaddrinfo(res);
        return 1;
    }

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("connect");
        close(fd);
        freeaddrinfo(res);
        return 1;
    }

    freeaddrinfo(res);
    printf("[client] connected to server\n");
    print_help();

    char line[256];

    for (;;) {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n[client] EOF on stdin, exiting\n");
            break;
        }

        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0') {
            continue;
        }

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            printf("[client] exiting on user request\n");
            break;
        } else if (strcmp(line, "help") == 0) {
            print_help();
        } else if (strcmp(line, "list") == 0) {
            int rc = handle_list(fd);
            if (rc != 0) {
                printf("[client] list failed, rc=%d\n", rc);
                break;
            }
        } else if (strncmp(line, "get ", 4) == 0) {
            char *arg = line + 4;
            while (*arg == ' ') arg++;
            if (*arg == '\0') {
                printf("usage: get <id>\n");
                continue;
            }

            char *endptr = NULL;
            unsigned long id_ul = strtoul(arg, &endptr, 10);
            if (*endptr != '\0') {
                printf("invalid id: %s\n", arg);
                continue;
            }

            uint32_t id = (uint32_t)id_ul;
            int rc = handle_get(fd, id);
            if (rc != 0) {
                printf("[client] get failed, rc=%d\n", rc);
                break;
            }
        } else if(strncmp(line, "set ", 4) == 0) {
            char *arg = line + 4;
            while (*arg == ' ') arg++;
            if (*arg == '\0') {
                printf("usage: set <id> <temp>\n");
                continue;
            }

            char *endptr = NULL;
            unsigned long id_ul = strtoul(arg, &endptr, 10);
            if (*endptr != ' ') {
                printf("invalid id: %s\n", arg);
                continue;
            }

            arg = endptr;
            while (*arg == ' ') arg++;
            if (*arg == '\0') {
                printf("usage: set <id> <temp>\n");
                continue;
            }

            float temp = strtof(arg, &endptr);
            if (*endptr != '\0') {
                printf("invalid temperature: %s\n", arg);
                continue;
            }

            uint32_t id = (uint32_t)id_ul;
            int rc = handle_set(fd, id, temp);
            if (rc != 0) {
                printf("[client] set failed, rc=%d\n", rc);
                break;
            }
        } else {
            printf("unknown command: '%s'\n", line);
            print_help();
        }
    }

    close(fd);
    return 0;
}
