#include <stdio.h>

int server_run(void);

int main(void) {
    printf("[server] Starting server...\n");
    int rc = server_run();
    printf("[server] Exiting with code %d\n", rc);
    return rc;
}