#include "protocol.h"

#include <asm-generic/errno-base.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>

static ssize_t read_all(int fd, void *buf, size_t count) {
    uint8_t *ptr = buf;
    size_t left = count;
    while (left > 0) {
        ssize_t n = read(fd, ptr, left);
        if (n == 0) {
            return (count == left) ? 0 : (ssize_t)(count - left);
        } else if(n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        ptr += n;
        left -= (size_t)n;
    }
 
    return (ssize_t)count;
}

static ssize_t write_all(int fd, const void *buf, size_t count) {
    const uint8_t *ptr = buf;
    size_t left = count;
    
    while(left > 0) {
        ssize_t n = write(fd, ptr, left);
        if (n < 0) {
            if(errno == EINTR) {
                continue;
            }
            return -1;
        }
        ptr += n;
        left -= (size_t)n;
    }

    return (ssize_t)count;
}

int send_tlv(int fd, uint16_t type, const void *value, uint16_t length) {
    tlv_header_t hdr;
    hdr.type = htons(type);
    hdr.length = htons(length);

    if(write_all(fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) {
        return -1;
    }

    if(length > 0 && value != NULL) {
        if(write_all(fd, value, length) != (ssize_t)length) {
            return -1;
        }
    }

    return 0;
}

int recv_tlv(int fd, uint16_t *type, void *buf, uint16_t bufsize, uint16_t *out_length) {
    tlv_header_t hdr;

    ssize_t n = read_all(fd, &hdr, sizeof(hdr));
    
    if(n == 0) {
        return 1;
    }
    if(n < 0 || n != (ssize_t)sizeof(hdr)) {
        return -1;
    }

    uint16_t payload_length = ntohs(hdr.length);
    if(payload_length > bufsize) {
        return -1;
    }

    if(payload_length > 0) {
        n = read_all(fd, buf, payload_length);
        if(n < 0 || n != (ssize_t)payload_length) {
            return -1;
        }
    }

    if(type) {
        *type = ntohs(hdr.type);
    }
    if(out_length) {
        *out_length = payload_length;
    }

    return 0;
}

int tlv_encode_buf(uint8_t *out, size_t out_size, uint16_t type, const void *value, uint16_t len, size_t *out_len) {
    if (out_size < sizeof(tlv_header_t) + len) {
        return -1;
    }

    tlv_header_t hdr;
    hdr.type = htons(type);
    hdr.length = htons(len);

    memcpy(out, &hdr, sizeof(hdr));
    if (len > 0 && value != NULL) {
        memcpy(out + sizeof(hdr), value, len);
    }

    if(out_len) *out_len = sizeof(hdr) + len;
    return 0;
}

int tlv_decode_buf(const uint8_t *in, size_t in_size, uint16_t *out_type, const uint8_t **out_value, uint16_t *out_len) {
    if (in_size < sizeof(tlv_header_t)) {
        return -1;
    }


    if (in_size < sizeof(tlv_header_t)) {
        return -1;
    }

    tlv_header_t hdr;
    memcpy(&hdr, in, sizeof(hdr));

    uint16_t type = ntohs(hdr.type);
    uint16_t len = ntohs(hdr.length);

    if(in_size < sizeof(hdr) + len) {
        return -1;
    }

    if(out_type) {
        *out_type = type;
    }

    if(out_len) {
        *out_len = len;
    }

    if(out_value) {
        *out_value = in + sizeof(hdr);
    }

    return 0;
}