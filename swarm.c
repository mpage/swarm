#include <assert.h>
#include <ev.h>
#include <getopt.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "driver.h"

#define REQUEST_FMT "GET %s HTTP/1.1\r\n"       \
                    "Host: %s:%s\r\n"           \
                    "Connection: close\r\n"     \
                    "\r\n"

static char request[1024];

static void *run_driver(void *data) {
  assert(NULL != data);

  driver_run((driver_t *) data, request);

  return NULL;
}

static void usage(void) {
  fprintf(stderr,
          "Usage: swarm [opts] <nactive> <host> <port> <url>\n"
          "\n"
          "Options:\n"
          "  -i <nidle>\n"
          "     Specifies the number of idle connections to create. Default is 0.\n"
          "\n"
          "  -t <nthreads>\n"
          "     Specifies the number of threads to use. By default, this\n"
          "     creates one thread per core. Note that each thread has its own\n"
          "     event loop.\n");
}

int main(int argc, char *argv[]) {
  struct addrinfo hints, *ai;
  int nactive = 0, nidle = 0, exit_status = 0, ii = 0, jj = 0, opt = 0;
  long nthreads = 0;
  driver_t *drivers = NULL;
  struct ev_loop **evloops = NULL;
  pthread_t *driver_threads = NULL;

  while ((opt = getopt(argc, argv, "i:t:")) != -1) {
    switch (opt) {
      case 't':
        nthreads = atoi(optarg);
        break;

      case 'i':
        nidle = atoi(optarg);
        break;
    }
  }

  if ((argc - optind) != 4) {
    usage();
    return 1;
  }

  nactive = atoi(argv[optind]);
  if (nactive < 0) {
    fprintf(stderr, "Error: nactive must be > 0\n");
    exit_status = 1;
    goto cleanup;
  }

  if (nidle < 0) {
    fprintf(stderr, "Error: nidle must be > 0\n");
    exit_status = 1;
    goto cleanup;
  }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(argv[optind + 1], argv[optind + 2], &hints, &ai) != 0) {
    perror("getaddrinfo()");
    exit_status = 1;
    goto cleanup;
  }

  memset(request, 0, sizeof(request));
  snprintf(request, sizeof(request) - 1,
           REQUEST_FMT, argv[optind + 3], argv[optind + 1], argv[optind + 2]);

  if (0 == nthreads) {
    nthreads = sysconf(_SC_NPROCESSORS_ONLN);
  }

  if (nthreads <= 0) {
    fprintf(stderr, "Error: nthreads must be > 0\n");
    exit_status = 1;
    goto cleanup;
  }

  evloops = calloc(nthreads, sizeof(*evloops));
  assert(NULL != evloops);

  drivers = calloc(nthreads, sizeof(*drivers));
  assert(NULL != drivers);

  driver_threads = calloc(nthreads, sizeof(*driver_threads));
  assert(NULL != driver_threads);

  for (ii = 0; ii < nthreads; ++ii) {
    evloops[ii] = ev_loop_new(EVFLAG_AUTO);
    assert(NULL != evloops[ii]);

    /* XXX - Handle remainder */
    driver_init(&drivers[ii], evloops[ii],
                (unsigned int) (nactive / nthreads), (unsigned int) (nidle / nthreads),
                ai->ai_addr);

    if (pthread_create(&driver_threads[ii], NULL, run_driver, &drivers[ii])) {
      perror("Failed creating driver thread");
      exit_status = 1;
      goto cleanup;
    }
  }

  for (ii = 0; ii < nthreads; ++ii) {
    pthread_join(driver_threads[ii], NULL);

    for (jj = 0; jj < drivers[ii].nactive; ++jj) {
      printf("%ld %ld\n",
             drivers[ii].results[jj].ttc,
             drivers[ii].results[jj].ttfb);
    }
  }

cleanup:

  if (drivers != NULL) {
    for (ii = 0; ii < nthreads; ++ii) {
      driver_destroy(&(drivers[ii]));
    }
    free(drivers);
  }

  if (evloops != NULL) {
    for (ii = 0; ii < nthreads; ++ii) {
      ev_loop_destroy(evloops[ii]);
    }
    free(evloops);
  }

  if (driver_threads != NULL) {
    free(driver_threads);
  }

  if (ai != NULL) {
    freeaddrinfo(ai);
    ai = NULL;
  }

  return exit_status;
}
