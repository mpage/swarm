#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "util.h"

#define ATOMIC_IO(func, fd, buf, count, hup) do {                       \
  ssize_t nbytes_tot = 0;                                               \
  ssize_t nbytes_cur = 0;                                               \
  uint8_t done = 0;                                                     \
                                                                        \
  while (!done) {                                                       \
    nbytes_cur = (func)((fd), (buf) + nbytes_tot, count - nbytes_tot);  \
    if (-1 == nbytes_cur) {                                             \
      switch (errno) {                                                  \
        case EAGAIN:                                                    \
          done = 1;                                                     \
          break;                                                        \
        case EINTR:                                                     \
          break;                                                        \
        case EPIPE:                                                     \
          if (NULL != hup) {                                            \
            *(hup) = 1;                                                 \
          }                                                             \
          done = 1;                                                     \
          break;                                                        \
        case ECONNRESET:                                                \
          if (NULL != hup) {                                            \
            *(hup) = 1;                                                 \
          }                                                             \
          done = 1;                                                     \
          break;                                                        \
        default:                                                        \
          perrorf("atomic_io(fd=%d)", fd);                              \
          abort();                                                      \
          break;                                                        \
      }                                                                 \
    } else if (0 == nbytes_cur) {                                       \
      if (NULL != hup) {                                                \
        *(hup) = 1;                                                     \
      }                                                                 \
      done = 1;                                                         \
    } else {                                                            \
      nbytes_tot += nbytes_cur;                                         \
      done = (nbytes_tot == count);                                     \
    }                                                                   \
  }                                                                     \
  return nbytes_tot;                                                    \
} while (0);

ssize_t atomic_read(int fd, void *buf, size_t count, uint8_t *hup) {
  ATOMIC_IO(read, fd, buf, count, hup);
}

ssize_t atomic_write(int fd, const void *buf, size_t count, uint8_t *hup) {
  ATOMIC_IO(write, fd, buf, count, hup);
}

void set_nonblocking(int fd) {
  int flags = 0;

  flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    perrorf("fcntl(fd=%d)", fd);
    abort();
  }

  if (-1 == fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
    perrorf("fcntl(fd=%d)", fd);
    abort();
  }
}

void perrorf(const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  fprintf(stderr, " %s\n", strerror(errno));
}

void checked_lock(pthread_mutex_t *lock) {
  assert(NULL != lock);

  if (-1 == pthread_mutex_lock(lock)) {
    perror("pthread_mutex_lock");
    abort();
  }
}

void checked_unlock(pthread_mutex_t *lock) {
  assert(NULL != lock);

  if (-1 == pthread_mutex_unlock(lock)) {
    perror("pthread_mutex_unlock");
    abort();
  }
}
