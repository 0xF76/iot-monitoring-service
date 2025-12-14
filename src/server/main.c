#include "server.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <errno.h>

static int daemonize_process(void);
static void handle_sigterm(int sig);
static int install_signal_handlers(void);

int main(int argc, char *argv[]) {

    int daemon_mode = (argc >= 2 && strcmp(argv[1], "--daemon") == 0);

    if(daemon_mode){
        openlog("iot-monitor-server", LOG_PID | LOG_NDELAY, LOG_DAEMON);
        g_use_syslog = 1;
        
        if(daemonize_process() < 0) {
            LOGE("Failed to damonize process");
            closelog();
            return 1;
        }
        LOGI("Daemon started");
    } else {
        g_use_syslog = 0;
    }

    if(install_signal_handlers() < 0) {
        LOGE("Failed to install signal handlers");
        if(g_use_syslog) closelog();
        return 1;
    }

    LOGI("Starting server%s", daemon_mode ? " in daemon mode" : "");

    int rc = server_run();

    if(daemon_mode){
        LOGI("Daemon stopping");
        closelog();
    }

    return rc;
}

static int daemonize_process(void) {
    pid_t pid = fork();
    if(pid < 0) {
        LOGE("First fork failed: %s", strerror(errno));
        return -1;
    }
    if(pid > 0) {
        // Parent process
        exit(0);
    }

    if(setsid() < 0) {
        LOGE("setsid failed: %s", strerror(errno));
        return -1;
    }

    //second fork to prevent reacquiring a terminal
    pid = fork();
    if(pid < 0) {
        LOGE("Second fork failed: %s", strerror(errno));
        return -1;
    }
    if(pid > 0) {
        // Parent process
        exit(0);
    }

    umask(027);
    if(chdir("/") < 0) {
        LOGE("chdr failed: %s", strerror(errno));
        return -1;
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int fd = open("/dev/null", O_RDWR);
    if(fd < 0) {
        LOGE("open /dev/null failed: %s", strerror(errno));
        return -1;
    }

    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);

    if(fd > STDERR_FILENO) {
        close(fd);
    }

    return 0;   
}

static void handle_sigterm(int sig) {
    (void)sig;
    g_running = 0;
}

static int install_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigterm;
    sigemptyset(&sa.sa_mask);

    if(sigaction(SIGTERM, &sa, NULL) < 0) return -1;
    if(sigaction(SIGINT, &sa, NULL) < 0) return -1;
    return 0;
}