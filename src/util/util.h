#ifndef UTIL_UTIL_H
#define UTIL_UTIL_H

#include <stdint.h>
#include <time.h>

// Number of elements in an array (doesn't work for pointers).
#define COUNT(a) ((int)(sizeof(a) / sizeof(a)[0]))

static inline int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#endif
