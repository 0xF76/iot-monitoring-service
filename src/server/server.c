#include "protocol.h"

#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define SERVER_PORT 5001


typedef enum {
    SET_OK = 0,
    SET_NOT_FOUND = 1,
    SET_BAD_REQUEST = 2
} set_result_t;

static device_status_t g_devices[] = {
    { .device_id = 1, .temperature = 22.5, .battery = 85, .status = 1 },
    { .device_id = 2, .temperature = 19.0, .battery = 60, .status = 1 },
    { .device_id = 3, .temperature = 25.3, .battery = 40, .status = 0 },
    { .device_id = 4, .temperature = 30.1, .battery = 20, .status = 2 },
    { .device_id = 5, .temperature = 18.7, .battery = 90, .status = 1 },
};
static const size_t g_device_count = sizeof(g_devices) / sizeof(g_devices[0]);

static device_status_t* find_device(uint32_t device_id);


static int handle_list(int fd);
static int handle_get(int fd, const uint8_t *payload, uint16_t len);
static int handle_set(int fd, const uint8_t *payload, uint16_t len);

static int dispatch_request(int fd, uint16_t type, const uint8_t *payload, uint16_t len);

int server_run(void) {
    int listen_fd, status, opt;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_fd < 0) {
        perror("socket");
        return 1;
    }

    opt = 1;
    status = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if(status < 0) {
        perror("setsockopt");
        close(listen_fd);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERVER_PORT);

    status = bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
    if(status < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    status = listen(listen_fd, 5);
    if(status < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("[server] listening on port %d...\n", SERVER_PORT);

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if(client_fd < 0) {
        perror("accept");
        close(listen_fd);
        return 1;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    printf("[server] client connected from %s:%d\n", client_ip, ntohs(client_addr.sin_port));


    uint8_t buffer[1024];
    while(1) {
        uint16_t type = 0, len = 0;

        int status = recv_tlv(client_fd, &type, buffer, sizeof(buffer), &len);
        if(status == 1) {
            printf("[server] client disconnected\n");
            break;
        } else if(status < 0) {
            perror("recv_tlv");
            break;
        }        

        printf("[server] received TLV type=0x%04x length=%d\n", type, len);

        if(dispatch_request(client_fd, type, buffer, len)) {
            printf("[server] handler error\n");
            break;
        }
    }

    close(client_fd);
    close(listen_fd);
    return 0;
}


static device_status_t* find_device(uint32_t device_id) {
    for(size_t i = 0; i < g_device_count; i++) {
        if(g_devices[i].device_id == device_id) {
            return &g_devices[i];
        }
    }
    return NULL;
}

static int dispatch_request(int fd, uint16_t type, const uint8_t *payload, uint16_t len) {
    switch(type) {
        case TLV_TYPE_LIST_REQUEST:
            return handle_list(fd);
        case TLV_TYPE_GET_REQUEST:
            return handle_get(fd, payload, len);
        case TLV_TYPE_SET_REQUEST:
            return handle_set(fd, payload, len);
        default:
            printf("[server] unknown request type 0x%04x\n", type);
            return 0; // ignore unknown types
    }
}

static int handle_list(int fd) {
    uint16_t payload_len = (uint16_t)(g_device_count * sizeof(device_status_t));
    int status = send_tlv(fd, TLV_TYPE_LIST_RESPONSE, g_devices, payload_len);
    if(status < 0) {
        printf("[server] send LIST_RESPONSE failed\n");
        return -1;
    }
    return 0;
}

static int handle_get(int fd, const uint8_t *payload, uint16_t len) {
    if(len != sizeof(uint32_t)) {
        printf("[server] GET bad len=%u\n", len);
        return send_tlv(fd, TLV_TYPE_GET_RESPONSE, NULL, 0);
    }

    uint32_t id_net = 0;
    memcpy(&id_net, payload, sizeof(id_net));
    uint32_t device_id = ntohl(id_net);
    device_status_t *dev = find_device(device_id);
    if(dev == NULL) {
        printf("[server] device ID %u not found\n", device_id);
        return send_tlv(fd, TLV_TYPE_GET_RESPONSE, NULL, 0);
    }

    return send_tlv(fd, TLV_TYPE_GET_RESPONSE, dev, sizeof(device_status_t));

}

static int handle_set(int fd, const uint8_t *payload, uint16_t len) {
    if(len != sizeof(uint32_t) * 2) {
        printf("[server] SET bad len=%u\n", len);
        uint8_t code = SET_BAD_REQUEST;
        return send_tlv(fd, TLV_TYPE_SET_RESPONSE, &code, sizeof(code));
    }

    uint32_t id_net = 0, temp_bits_net = 0;
    memcpy(&id_net, payload, sizeof(id_net));
    memcpy(&temp_bits_net, payload + sizeof(id_net), sizeof(temp_bits_net));
    
    uint32_t device_id = ntohl(id_net);
    float temperature;
    memcpy(&temperature, &temp_bits_net, sizeof(temperature));

    device_status_t *dev = find_device(device_id);
    uint8_t code = SET_OK;
    if(dev == NULL) {
        printf("[server] device ID %u not found for SET\n", device_id);
        code = SET_NOT_FOUND;
    } else {
        dev->temperature = temperature;
        printf("[server] updated device ID %u temperature to %.2f\n", device_id, temperature);
    }

    return send_tlv(fd, TLV_TYPE_SET_RESPONSE, &code, sizeof(code));
}
