#include "protocol.h"
#include "server.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/stat.h>

#define DISCOVERY_MCAST_ADDR "239.0.0.1"
#define DISCOVERY_PORT 5000
#define SERVER_PORT 5001

typedef enum {
    SET_OK = 0,
    SET_NOT_FOUND = 1,
    SET_BAD_REQUEST = 2
} set_result_t;

typedef struct {
    int client_fd;
} client_ctx_t;


int g_use_syslog = 0;
volatile sig_atomic_t g_running = 1;


static device_status_t g_devices[] = {
    { .device_id = 1, .temperature = 22.5, .battery = 85, .status = 1 },
    { .device_id = 2, .temperature = 19.0, .battery = 60, .status = 1 },
    { .device_id = 3, .temperature = 25.3, .battery = 40, .status = 0 },
    { .device_id = 4, .temperature = 30.1, .battery = 20, .status = 2 },
    { .device_id = 5, .temperature = 18.7, .battery = 90, .status = 1 },
};
static const size_t g_device_count = sizeof(g_devices) / sizeof(g_devices[0]);
static pthread_mutex_t g_devices_mutex = PTHREAD_MUTEX_INITIALIZER;

static device_status_t* find_device(uint32_t device_id);
static int handle_list(int fd);
static int handle_get(int fd, const uint8_t *payload, uint16_t len);
static int handle_set(int fd, const uint8_t *payload, uint16_t len);
static int dispatch_request(int fd, uint16_t type, const uint8_t *payload, uint16_t len);
static void devices_lock(void);
static void devices_unlock(void);
static void *client_thread(void *arg);
static void *discovery_thread(void *arg);

int server_run(void) {
    int listen_fd, status, opt;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_fd < 0) {
        LOGE("socket creation failed: %s", strerror(errno));
        return 1;
    }

    opt = 1;
    status = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if(status < 0) {
        LOGE("setsockopt SO_REUSEADDR failed: %s", strerror(errno));
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
        LOGE("bind failed: %s", strerror(errno));
        close(listen_fd);
        return 1;
    }

    status = listen(listen_fd, 5);
    if(status < 0) {
        LOGE("listen failed: %s", strerror(errno));
        close(listen_fd);
        return 1;
    }

    LOGI("listening on port %d...", SERVER_PORT);


    pthread_t disc_thread;
    pthread_create(&disc_thread, NULL, discovery_thread, NULL);
    pthread_detach(disc_thread);



    while(g_running) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if(client_fd < 0) {
            LOGE("accept failed: %s", strerror(errno));
            continue;
        }

        client_ctx_t *ctx = malloc(sizeof(*ctx));
        if(!ctx) {
            close(client_fd);
            continue;
        }
        ctx->client_fd = client_fd;

        pthread_t th;

        if(pthread_create(&th, NULL, client_thread, ctx) != 0) {
            LOGE("pthread_create failed: %s", strerror(errno));
            close(client_fd);
            free(ctx);
            continue;
        }

        pthread_detach(th);
    }

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
            LOGI("unknown request type 0x%04x", type);
            return 0; // ignore unknown types
    }
}

static int handle_list(int fd) {
    devices_lock();
    uint16_t payload_len = (uint16_t)(g_device_count * sizeof(device_status_t));
    int status = send_tlv(fd, TLV_TYPE_LIST_RESPONSE, g_devices, payload_len);
    devices_unlock();

    if(status < 0) {
        LOGE("send LIST_RESPONSE failed");
        return -1;
    }
    return 0;
}

static int handle_get(int fd, const uint8_t *payload, uint16_t len) {
    if(len != sizeof(uint32_t)) {
        LOGI("GET bad len=%u", len);
        return send_tlv(fd, TLV_TYPE_GET_RESPONSE, NULL, 0);
    }

    uint32_t id_net = 0;
    memcpy(&id_net, payload, sizeof(id_net));
    uint32_t device_id = ntohl(id_net);
    devices_lock();
    device_status_t *dev = find_device(device_id);
    if(dev == NULL) {
        devices_unlock();
        LOGE("device ID %u not found", device_id);
        return send_tlv(fd, TLV_TYPE_GET_RESPONSE, NULL, 0);
    }

    int rc = send_tlv(fd, TLV_TYPE_GET_RESPONSE, dev, sizeof(device_status_t));
    devices_unlock();

    return (rc < 0) ? -1 : 0;

}

static int handle_set(int fd, const uint8_t *payload, uint16_t len) {
    if(len != sizeof(uint32_t) * 2) {
        LOGI("SET bad len=%u", len);
        uint8_t code = SET_BAD_REQUEST;
        return send_tlv(fd, TLV_TYPE_SET_RESPONSE, &code, sizeof(code));
    }

    uint32_t id_net = 0, temp_bits_net = 0;
    memcpy(&id_net, payload, sizeof(id_net));
    memcpy(&temp_bits_net, payload + sizeof(id_net), sizeof(temp_bits_net));
    
    uint32_t device_id = ntohl(id_net);
    uint32_t temp_bits = ntohl(temp_bits_net);
    float temperature;
    memcpy(&temperature, &temp_bits, sizeof(temperature));

    devices_lock();
    device_status_t *dev = find_device(device_id);
    uint8_t code = SET_OK;
    if(dev == NULL) {
        LOGI("device ID %u not found for SET", device_id);
        code = SET_NOT_FOUND;
    } else {
        dev->temperature = temperature;
        LOGI("updated device ID %u temperature to %.2f", device_id, temperature);
    }
    devices_unlock();

    return send_tlv(fd, TLV_TYPE_SET_RESPONSE, &code, sizeof(code));
}

static void devices_lock(void) {
    pthread_mutex_lock(&g_devices_mutex);
}
static void devices_unlock(void) {
    pthread_mutex_unlock(&g_devices_mutex);
}

static void *client_thread(void *arg) {
    client_ctx_t *ctx = (client_ctx_t*)arg;
    int fd = ctx->client_fd;
    free(ctx);

    uint8_t buffer[1024];

    while(1) {
        uint16_t type = 0, len = 0;
        int rc = recv_tlv(fd, &type, buffer, sizeof(buffer), &len);

        if(rc == 1) break;
        if (rc < 0) break;

        if(dispatch_request(fd, type, buffer, len) < 0) {
            break;
        }

    }

    close(fd);
    return NULL;
}

static void *discovery_thread(void *arg) {
    (void)arg;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0) {
        LOGE("discovery socket creation failed: %s", strerror(errno));
        return NULL;
    }

    int reuse = 1;
    int status = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if(status < 0) {
        LOGE("discovery setsockopt SO_REUSEADDR failed: %s", strerror(errno));
        close(fd);
        return NULL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(DISCOVERY_PORT);

    status = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if(status < 0) {
        LOGE("discovery bind failed: %s", strerror(errno));
        close(fd);
        return NULL;
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(DISCOVERY_MCAST_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    status = setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    if(status < 0) {
        LOGE("discovery setsockopt IP_ADD_MEMBERSHIP failed: %s", strerror(errno));
        close(fd);
        return NULL;
    }
    LOGI("discovery thread listening on %s:%d...", DISCOVERY_MCAST_ADDR, DISCOVERY_PORT);

    uint8_t buffer[1024];

    while(g_running) {
        struct sockaddr_in src_addr;
        socklen_t src_addr_len = sizeof(src_addr);

        ssize_t n = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&src_addr, &src_addr_len);
        if(n < 0) {
            LOGE("discovery recvfrom failed: %s", strerror(errno));
            continue;
        }

        uint16_t type = 0, len = 0;
        int rc = tlv_decode_buf(buffer, (size_t)n, &type, NULL, &len);
        if(rc < 0) {
            LOGE("discovery tlv_decode_buf failed: %s", strerror(errno));
            continue;
        }

        if(type != TLV_TYPE_DISCOVER_REQUEST) {
            LOGI("discovery received unknown type 0x%04x", type);
            continue;
        }

        LOGI("discovery request received from %s", inet_ntoa(src_addr.sin_addr));

        uint16_t tcp_port_net = htons((uint16_t)SERVER_PORT);
        uint8_t tx[1024];
        size_t tx_len = 0;

        rc = tlv_encode_buf(tx, sizeof(tx), TLV_TYPE_DISCOVER_RESPONSE, &tcp_port_net, sizeof(tcp_port_net), &tx_len);
        if(rc < 0) {
            LOGE("discovery tlv_encode_buf failed: %s", strerror(errno));
            continue;
        }

        n = sendto(fd, tx, tx_len, 0, (struct sockaddr *)&src_addr, src_addr_len);
        if(n < 0) {
            LOGE("discovery sendto failed: %s", strerror(errno));
            continue;
        }
    }
    close(fd);
    return NULL;
}
