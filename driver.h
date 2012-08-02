#ifndef __DRIVER_H__
#define __DRIVER_H__

#include <arpa/inet.h>
#include <ev.h>
#include <netinet/in.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
  int64_t ttc;
  int64_t ttfb;
} timing_result_t;

typedef struct {
  struct ev_loop  *evloop;
  unsigned int     nactive;
  unsigned int     nidle;
  struct sockaddr  addr;
  timing_result_t *results;
} driver_t;

void driver_init(driver_t *driver, struct ev_loop *evloop, unsigned int nactive,
                 unsigned int nidle, const struct sockaddr *addr);

void driver_run(driver_t *driver, const char *request);

void driver_destroy(driver_t *driver);

#endif
