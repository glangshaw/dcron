
/*
 * SUBS.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2026 Gary Langshaw (gary.langshaw@gmail.com)
 * May be distributed under the GNU General Public License
 */

#include "subs.h"
#include "defs.h"
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void vlog(int level, int fd, const char *ctl, va_list va);
static int slog(char *buf, const char *ctl, int nmax, va_list va, short useDate);


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
  vlog(9, fd, ctl, va);
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
  short n;
  static short useDate = 1;

  if (level <= LogLevel)
  {
    write(fd, buf, n = slog(buf, ctl, sizeof(buf), va, useDate));
    useDate = (n && buf[n - 1] == '\n');
  }
}

int slog(char *buf, const char *ctl, int nmax, va_list va, short useDate)
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

int ChangeUser(const char *user, short dochdir)
{
  struct passwd *pas;

  /*
   * Obtain password entry and change privilages
   */

  if ((pas = getpwnam(user)) == 0)
  {
    logn(3, "failed to get uid for %s", user);
    return (-1);
  }
  setenv("USER", pas->pw_name, 1);
  setenv("HOME", pas->pw_dir, 1);
  setenv("SHELL", "/bin/sh", 1);

  /*
   * Change running state to the user in question
   */

  if (initgroups(user, pas->pw_gid) < 0)
  {
    logn(3, "initgroups failed: %s %s", user, strerror(errno));
    return (-1);
  }
  if (setregid(pas->pw_gid, pas->pw_gid) < 0)
  {
    logn(3, "setregid failed: %s %d", user, pas->pw_gid);
    return (-1);
  }
  if (setreuid(pas->pw_uid, pas->pw_uid) < 0)
  {
    logn(3, "setreuid failed: %s %d", user, pas->pw_uid);
    return (-1);
  }
  if (dochdir)
  {
    if (chdir(pas->pw_dir) < 0)
    {
      logn(3, "chdir failed: %s %s", user, pas->pw_dir);
      if (chdir(TMPDIR) < 0)
      {
        logn(3, "chdir failed: %s %s", user, pas->pw_dir);
        logn(3, "chdir failed: %s " TMPDIR, user);
        return (-1);
      }
    }
  }
  return (pas->pw_uid);
}
