#include <stdio.h>

int client_run(void);

int main(void) {
    printf("[client] Starting client...\n");
    int rc = client_run();
    printf("[client] Exiting with code %d\n", rc);
    return rc;
}