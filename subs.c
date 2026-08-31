//  SUBS.C
//
//  Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
//  Copyright 2026 Gary Langshaw (gary.langshaw@gmail.com)
//  May be distributed under the GNU General Public License

#include "subs.h"

#include "defs.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

void fdprintf(int fd, const char *ctl, ...)
{
  va_list va;
  char buf[2048];

  va_start(va, ctl);
  vsnprintf(buf, sizeof(buf), ctl, va);
  write(fd, buf, strlen(buf));
  va_end(va);
}
