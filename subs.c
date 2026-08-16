//  SUBS.C
//
//  Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
//  Copyright 2026 Gary Langshaw (gary.langshaw@gmail.com)
//  May be distributed under the GNU General Public License

#include "subs.h"

#include "defs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

static void vlog(int level, int fd, const char *ctl, va_list va);
static int slog(char *buf, const char *ctl, int nmax, va_list va, int useDate);

void logn(int level, const char *ctl, ...)
{
  va_list va;

  va_start(va, ctl);
  vlog(level, 2, ctl, va);
  va_end(va);
}

void logfd(int fd, const char *ctl, ...)
{
  va_list va;

  va_start(va, ctl);
  vlog(0, fd, ctl, va);
  va_end(va);
}

void fdprintf(int fd, const char *ctl, ...)
{
  va_list va;
  char buf[2048];

  va_start(va, ctl);
  vsnprintf(buf, sizeof(buf), ctl, va);
  write(fd, buf, strlen(buf));
  va_end(va);
}

static void vlog(int level, int fd, const char *ctl, va_list va)
{
  char buf[2048];
  int n;
  static int useDate = 1;

  if (level <= LogLevel)
  {
    write(fd, buf, n = slog(buf, ctl, sizeof(buf), va, useDate));
    useDate = (n && buf[n - 1] == '\n');
  }
}

int slog(char *buf, const char *ctl, int nmax, va_list va, int useDate)
{
  time_t t;
  struct tm *tp;
  size_t dateStrLen = 0;

  buf[0] = 0;
  if (useDate)
  {
    t = time(NULL);
    tp = localtime(&t);
    dateStrLen = strftime(buf, 128, "%d-%b-%Y %H:%M  ", tp);
  }
  vsnprintf(buf + dateStrLen, nmax, ctl, va);
  return (strlen(buf));
}
