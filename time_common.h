#ifndef TIME_COMMON_H
#define TIME_COMMON_H
/*
 * tvsub --
 *  Subtract 2 timeval structs:  out = out - in.  Out is assumed to
 * be >= in.
 *
 * Borrowed from ping.h
 */
static inline void tvsub(struct timeval *out, struct timeval *in)
{
  if ((out->tv_usec -= in->tv_usec) < 0) {
    --out->tv_sec;
    out->tv_usec += 1000000;
  }
  out->tv_sec -= in->tv_sec;
}

// out = out + in
static inline void tvadd(struct timeval *out, struct timeval *in)
{
  if ((out->tv_usec += in->tv_usec) > 1000000) {
    ++out->tv_sec;
    out->tv_usec -= 1000000;
  }
  out->tv_sec += in->tv_sec;
}

#endif
