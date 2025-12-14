#pragma once

#include <stdio.h>
#include <syslog.h>
#include <signal.h>


#define LOGI(fmt, ...) do { \
  if (g_use_syslog) syslog(LOG_INFO, fmt __VA_OPT__(,) __VA_ARGS__); \
  else printf("[server] " fmt "\n" __VA_OPT__(,) __VA_ARGS__); \
} while(0)

#define LOGE(fmt, ...) do { \
  if (g_use_syslog) syslog(LOG_ERR, fmt __VA_OPT__(,) __VA_ARGS__); \
  else fprintf(stderr, "[server] " fmt "\n" __VA_OPT__(,) __VA_ARGS__); \
} while(0)

extern int g_use_syslog;
extern volatile sig_atomic_t g_running;


int server_run(void);