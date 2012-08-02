#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "dlog.h"
#include "driver.h"
#include "util.h"

#define RESP_BUF_SIZE 4096

typedef enum {
  IC_STATE_START,
  IC_STATE_CONNECTING,
  IC_STATE_CONNECTED
} idle_conn_state_t;

typedef struct {
  ev_io              watcher;
  struct ev_loop    *evloop;
  int                fd;

  struct sockaddr    addr;

  idle_conn_state_t  state;
} idle_conn_t;

typedef enum {
  AC_STATE_START,
  AC_STATE_CONNECTING,
  AC_STATE_WRITE_REQUEST,
  AC_STATE_READ_RESPONSE,
  AC_STATE_DONE,
} active_conn_state_t;

typedef struct {
  ev_io                 watcher;
  struct ev_loop       *evloop;
  int                   fd;
  struct sockaddr       addr;

  active_conn_state_t   state;

  const char           *request;
  size_t                request_off;
  size_t                request_size;

  timing_t              ttc_timer;
  int64_t               tt_connect;
  timing_t              ttfb_timer;
  int64_t               tt_first_byte;
} active_conn_t;

static void idle_conn_run(idle_conn_t *conn);
static void active_conn_run(active_conn_t *conn);

static void active_conn_init(struct ev_loop *evloop, active_conn_t *conn,
                             const struct sockaddr *addr,
                             const char *request) {
  assert(NULL != evloop);
  assert(NULL != conn);

  conn->evloop = evloop;
  memcpy(&(conn->addr), addr, sizeof(*addr));

  conn->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (-1 == conn->fd) {
    perror("socket()");
    abort();
  }

  set_nonblocking(conn->fd);

  conn->state         = AC_STATE_START;
  conn->request_off   = 0;
  conn->request       = request;
  conn->request_size  = strlen(request);
  conn->tt_connect    = -1;
  conn->tt_first_byte = -1;
}

static void active_conn_cb(struct ev_loop *evloop, ev_io *w, int events) {
  active_conn_t *conn = NULL;

  assert(NULL != w);

  conn = (active_conn_t *) w;

  active_conn_run(conn);
}

static void active_conn_run(active_conn_t *conn) {
  int done = 0;
  uint8_t buf[RESP_BUF_SIZE], hup = 0;
  ssize_t nread = 0;

  while (!done) {
    switch (conn->state) {
      case AC_STATE_START:
        DLOG("fd=%d starting", conn->fd);

        TIME_START(&(conn->ttc_timer));
        TIME_START(&(conn->ttfb_timer));

        if (connect(conn->fd, &(conn->addr), sizeof(conn->addr)) < 0) {
          if (errno != EINPROGRESS) {
            perror("connect()");
            abort();
          }
        }

        /* Connect completes when socket is writable */
        ev_io_init(&(conn->watcher), active_conn_cb, conn->fd, EV_WRITE);
        ev_io_start(conn->evloop, &(conn->watcher));

        conn->state = AC_STATE_CONNECTING;
        done = 1;
        break;

      case AC_STATE_CONNECTING:
        TIME_STOP(&(conn->ttc_timer));
        conn->tt_connect = TIME_DELTA_NS(&(conn->ttc_timer));

        DLOG("fd=%d connected", conn->fd);

        conn->state = AC_STATE_WRITE_REQUEST;
        break;

      case AC_STATE_WRITE_REQUEST:
        conn->request_off += atomic_write(conn->fd, conn->request,
                                          conn->request_size - conn->request_off,
                                          &hup);
        DLOG("fd=%d writing, wrote %d", conn->fd, conn->request_off);

        if (hup) {
          DLOG("fd=%d premature hangup", conn->fd);

          conn->state = AC_STATE_DONE;
        } else if(conn->request_off == conn->request_size) {
          /* Go won't close the socket unless the write side is closed */
          shutdown(conn->fd, SHUT_WR);

          /* Finished writing the request, need to read the response */
          ev_io_stop(conn->evloop, &(conn->watcher));
          ev_io_set(&(conn->watcher), conn->fd, EV_READ);
          ev_io_start(conn->evloop, &(conn->watcher));

          conn->state = AC_STATE_READ_RESPONSE;

          done = 1;
        } else {
          done = 1;
        }
        break;

      case AC_STATE_READ_RESPONSE:
        DLOG("fd=%d reading", conn->fd);

        do {
          nread = atomic_read(conn->fd, buf, RESP_BUF_SIZE, &hup);

          if ((nread > 0) && (conn->tt_first_byte == -1)) {
            TIME_STOP(&(conn->ttfb_timer));
            conn->tt_first_byte = TIME_DELTA_NS(&(conn->ttfb_timer));
          }
        } while ((nread > 0) && !hup);

        if (hup) {
          conn->state = AC_STATE_DONE;
        } else {
          done = 1;
        }
        break;

      case AC_STATE_DONE:
        DLOG("fd=%d done", conn->fd);

        ev_io_stop(conn->evloop, &(conn->watcher));
        close(conn->fd);
        done = 1;
        break;

      default:
        /* NOTREACHED */
        abort();
    }
  }
}

static void idle_conn_init(struct ev_loop *evloop, idle_conn_t *conn,
                           const struct sockaddr *addr) {
  assert(NULL != evloop);
  assert(NULL != conn);

  conn->evloop = evloop;
  memcpy(&(conn->addr), addr, sizeof(*addr));

  conn->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (-1 == conn->fd) {
    perror("socket()");
    abort();
  }

  set_nonblocking(conn->fd);

  conn->state = IC_STATE_START;
}

static void idle_conn_cb(struct ev_loop *evloop, ev_io *w, int events) {
  idle_conn_t *conn = NULL;

  assert(NULL != w);

  conn = (idle_conn_t *) w;

  if (IC_STATE_CONNECTING == conn->state) {
    conn->state = IC_STATE_CONNECTED;
    idle_conn_run(conn);
  } else {
    /* We should be unregistered at this point. */
    abort();
  }
}

static void idle_conn_run(idle_conn_t *conn) {
  assert(NULL != conn);

  switch (conn->state) {
    case IC_STATE_START:
      if (connect(conn->fd, &(conn->addr), sizeof(conn->addr)) < 0) {
        if (errno != EINPROGRESS) {
          perror("connect()");
          abort();
        }
      }

      ev_io_init(&(conn->watcher), idle_conn_cb, conn->fd, EV_WRITE);
      ev_io_start(conn->evloop, &(conn->watcher));

      conn->state = IC_STATE_CONNECTING;
      break;

    case IC_STATE_CONNECTED:
      ev_io_stop(conn->evloop, &(conn->watcher));
      break;

    default:
      /* NOTREACHED */
      abort();
  }
}

void driver_init(driver_t *driver, struct ev_loop *evloop, unsigned int nactive,
                 unsigned int nidle, const struct sockaddr *addr) {
  assert(NULL != driver);
  assert(NULL != evloop);

  driver->evloop  = evloop;
  driver->nactive = nactive;
  driver->nidle   = nidle;

  memcpy(&(driver->addr), addr, sizeof(*addr));

  driver->results = calloc(nactive, sizeof(*driver->results));
  assert(NULL != driver->results);
}

void driver_destroy(driver_t *driver) {
  assert(NULL != driver);
  assert(NULL != driver->results);

  free(driver->results);
  driver->results = NULL;
}

void driver_run(driver_t *driver, const char *request) {
  timing_t timer;
  idle_conn_t *idle_conns = NULL;
  active_conn_t *active_conns = NULL;
  int ii = 0;

  if (driver->nidle > 0) {
    DLOG("Creating %d idle connections", driver->nidle);

    idle_conns = calloc(driver->nidle, sizeof(*idle_conns));
    assert(NULL != idle_conns);

    TIME_START(&timer);

    for (ii = 0; ii < driver->nidle; ++ii) {
      idle_conn_init(driver->evloop, &idle_conns[ii], &(driver->addr));
      idle_conn_run(&idle_conns[ii]);
    }

    DLOG("Done creating idle connections, waiting for connects.");

    /* Wait for idle conns to connect */
    ev_run(driver->evloop, 0);

    TIME_STOP(&timer);

    DLOG("All idle connections established. Took %lld nsecs.",
         TIME_DELTA_NS(&timer));
  }

  DLOG("Creating %d active connections", driver->nactive);

  active_conns = calloc(driver->nactive, sizeof(*active_conns));
  assert(NULL != active_conns);

  for (ii = 0; ii < driver->nactive; ++ii) {
    active_conn_init(driver->evloop, &active_conns[ii], &(driver->addr),
                     request);
    active_conn_run(&active_conns[ii]);
  }

  /* Wait for real requests to complete */
  ev_run(driver->evloop, 0);

  for (ii = 0; ii < driver->nactive; ++ii) {
    driver->results[ii].ttc = active_conns[ii].tt_connect;
    driver->results[ii].ttfb = active_conns[ii].tt_first_byte;
  }

  if (NULL != idle_conns) {
    for (ii = 0; ii < driver->nidle; ++ii) {
      close(idle_conns[ii].fd);
    }
    free(idle_conns);
  }

  if (NULL != active_conns) {
    free(active_conns);
  }
}
