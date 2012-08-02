#ifndef UTIL_H
#define UTIL_H 1

#include <assert.h>
#include <linux/limits.h>
#include <pthread.h>
#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef struct {
  struct timespec tsA, tsB;
  int64_t dt;
} timing_t;

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define MUXER_READABLE 1
#define MUXER_STOP 2


#define TIME_START(_ptr) do {                                     \
    int _time_rv = clock_gettime(CLOCK_REALTIME, &((_ptr)->tsA)); \
    assert(_time_rv == 0);                                        \
  } while(0)

#define TIME_STOP(_ptr) do {                                      \
    int _time_rv = clock_gettime(CLOCK_REALTIME, &((_ptr)->tsB)); \
    assert(_time_rv == 0);                                        \
    (_ptr)->dt = (_ptr)->tsB.tv_sec - (_ptr)->tsA.tv_sec;         \
    (_ptr)->dt *= 1000000000;                                     \
    (_ptr)->dt += (_ptr)->tsB.tv_nsec - (_ptr)->tsA.tv_nsec;      \
  } while(0)

#define TIME_DELTA_NS(_ptr) (_ptr)->dt

/**
 * Reads until _count_ bytes have been read or _fd_ would block.
 */
ssize_t atomic_read(int fd, void *buf, size_t count, uint8_t *hup);

/**
 * Writes until _count_ bytes have been written or _fd_ would block.
 */
ssize_t atomic_write(int fd, const void *buf, size_t count, uint8_t *hup);

void set_nonblocking(int fd);

void perrorf(const char *fmt, ...);

void checked_lock(pthread_mutex_t *lock);

void checked_unlock(pthread_mutex_t *lock);

#endif
