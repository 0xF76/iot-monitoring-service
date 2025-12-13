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

static device_status_t g_devices[] = {
    { .device_id = 1, .temperature = 22.5, .battery = 85, .status = 1 },
    { .device_id = 2, .temperature = 19.0, .battery = 60, .status = 1 },
    { .device_id = 3, .temperature = 25.3, .battery = 40, .status = 0 },
    { .device_id = 4, .temperature = 30.1, .battery = 20, .status = 2 },
    { .device_id = 5, .temperature = 18.7, .battery = 90, .status = 1 },
};
static const size_t g_device_count = sizeof(g_devices) / sizeof(g_devices[0]);

static device_status_t* find_device_by_id(uint32_t device_id) {
    for(size_t i = 0; i < g_device_count; i++) {
        if(g_devices[i].device_id == device_id) {
            return &g_devices[i];
        }
    }
    return NULL;
}

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
        uint16_t tlv_type = 0;
        uint16_t tlv_length = 0;

        int status = recv_tlv(client_fd, &tlv_type, buffer, sizeof(buffer), &tlv_length);
        if(status == 1) {
            printf("[server] client disconnected\n");
            break;
        } else if(status < 0) {
            perror("recv_tlv");
            break;
        }

        printf("[server] received TLV type=0x%04x length=%d\n", tlv_type, tlv_length);

        if(tlv_type == TLV_TYPE_LIST_REQUEST) {
            uint16_t payload_len = (uint16_t)(g_device_count * sizeof(device_status_t));
            status = send_tlv(client_fd, TLV_TYPE_LIST_RESPONSE, g_devices, payload_len);
            if(status < 0) {
                perror("send_tlv LIST_RESPONSE gailed");
                break;
            }
            printf("[server] sent LIST_RESPONSE with %zu devices\n", g_device_count);
        } else if(tlv_type == TLV_TYPE_GET_REQUEST) {
            if(tlv_length != sizeof(uint32_t)) {
                printf("[server] invalid GET_REQUEST length %d\n", tlv_length);
                continue;
            }

            uint32_t id_net = 0;
            memcpy(&id_net, buffer, sizeof(id_net));
            uint32_t device_id = ntohl(id_net);
            device_status_t *dev = find_device_by_id(device_id);
            if(dev == NULL) {
                printf("[server] device ID %u not found\n", device_id);
                status = send_tlv(client_fd, TLV_TYPE_GET_RESPONSE, NULL, 0);
                if(status < 0) {
                    perror("send_tlv GET_RESPONSE failed");
                    break;
                }
            } else {
                status = send_tlv(client_fd, TLV_TYPE_GET_RESPONSE, dev, sizeof(device_status_t));
                if(status < 0) {
                    perror("send_tlv GET_RESPONSE failed");
                    break;
                }
                printf("[server] sent GET_RESPONSE for device ID %u\n", device_id);
            }

        } else if(tlv_type == TLV_TYPE_SET_REQUEST) {
            if(tlv_length != 2 * sizeof(uint32_t)) {
                printf("[server] invalid SET_REQUEST length %d\n", tlv_length);
                uint8_t code =2;
                status = send_tlv(client_fd, TLV_TYPE_SET_RESPONSE, &code, sizeof(code));
                if(status < 0) {
                    perror("send_tlv SET_RESPONSE failed");
                    break;
                }
                continue;
            }
            uint32_t id_net = 0, temp_bits_net = 0;
            memcpy(&id_net, buffer, sizeof(id_net));
            memcpy(&temp_bits_net, buffer + sizeof(id_net), sizeof(temp_bits_net));
            uint32_t device_id = ntohl(id_net);
            float temperature;
            memcpy(&temperature, &temp_bits_net, sizeof(temperature));

            device_status_t *dev = find_device_by_id(device_id);
            uint8_t code = 0;

            if(dev == NULL) {
                printf("[server] device ID %u not found for SET_REQUEST\n", device_id);
                code = 1;
            } else {
                dev->temperature = temperature;
                printf("[server] updated device ID %u temperature to %.2f\n", device_id, temperature);
            }

            status = send_tlv(client_fd, TLV_TYPE_SET_RESPONSE, &code, sizeof(code));
            if(status < 0) {
                perror("send_tlv SET_RESPONSE failed");
                break;
            }
        } else {
            printf("[server] unknown TLV type 0x%04x\n", tlv_type);
        }   
    }

    close(client_fd);
    close(listen_fd);
    return 0;
}