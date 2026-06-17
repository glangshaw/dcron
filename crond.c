/*
 * crond.c
 *
 * dcron -d[#] -c <crondir> [ -f | -b ]
 *
 * run as root, but NOT setuid root
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * May be distributed under the GNU General Public License
 */

#include "database.h"
#include "defs.h"
#include "job.h"
#include "subs.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define ONE_HOUR 3600

#ifndef RESCAN_INTERVAL
#define RESCAN_INTERVAL ONE_HOUR
#endif

short DebugOpt;
short LogLevel = 8;
short ForegroundOpt;
const char *CDir = CRONTABS;
const char *SCDir = SCRONTABS;
uid_t DaemonUid;
int InSyncFileRoot;

void RunMainLoop()
{
  time_t t1;
  time_t t2;
  time_t rescan; /* time of last rescan */
  short stime = 60;

  t1 = time(NULL);
  rescan = t1 - t1 % RESCAN_INTERVAL;

  for (;;)
  {
    /* synchronize to 1 second after the minute, minimum sleep of 1 second. */
    sleep((stime + 1) - (short)(time(NULL) % stime));

    t2 = time(NULL);

    if (DebugOpt)
      logn(5, "Wakeup: %s", ctime(&t2));

    /*
     * The file 'cron.update' is checked to determine new cron
     * jobs.  The directory is rescanned once an hour to deal
     * with any screwups.
     *
     * check for disparity.  Disparities over an hour either way
     * result in resynchronization.  A reverse-indexed disparity
     * less then an hour causes us to effectively sleep until we
     * match the original time (i.e. no re-execution of jobs that
     * have just been run).  A forward-indexed disparity less then
     * an hour causes intermediate jobs to be run, but only once
     * in the worst case.
     *
     * when running jobs, the inequality used is greater but not
     * equal to t1, and less then or equal to t2.
     */

    if (rescan + RESCAN_INTERVAL <= t2)
    {
      rescan = t2 - t2 % RESCAN_INTERVAL;
      SynchronizeDir(CDir, NULL, 0);
      SynchronizeDir(SCDir, "root", 0);
    }
    CheckUpdates(CDir, NULL);
    CheckUpdates(SCDir, "root");
    if (t2 < t1 - ONE_HOUR || t2 > t1 + ONE_HOUR)
    {
      rescan = t2 - t2 % RESCAN_INTERVAL;
      t1 = t2;
      logn(9, "large time disparity detected.\n");
    }
    else if (t2 > t1)
    {
      TestJobs(t1, t2);
      RunJobs();
      sleep(5);
      if (CheckJobs() > 0)
        stime = 10;
      else
        stime = 60;
      t1 = t2;
    }
  }
}

int main(int argc, char **argv)
{
  extern char *optarg;
  int i;
  int opt;

  /*
   * parse options
   */

  DaemonUid = getuid();

  while ((opt = getopt(argc, argv, "bc:dfl:s:")) != -1)
  {
    switch (opt)
    {
    case 'b':
      ForegroundOpt = 0;
      break;
    case 'd':
      DebugOpt = 1;
      LogLevel = 0;
      /* intentional fall-through */
    case 'f':
      ForegroundOpt = 1;
      break;
    case 'c':
      CDir = optarg;
      break;
    case 's':
      SCDir = optarg;
      break;
    case 'l':
      LogLevel = strtol(optarg, NULL, 10);
      break;
    default:
      break;
    }
  }

  /*
   * close stdin and stdout (stderr normally redirected by caller).
   * close unused descriptors
   * optional detach from controlling terminal
   */

  fclose(stdin);
  fclose(stdout);

  i = open("/dev/null", O_RDWR);
  if (i < 0)
  {
    perror("open: /dev/null:");
    exit(1);
  }
  dup2(i, 0);
  dup2(i, 1);

  for (i = 3; i < OPEN_MAX; ++i)
  {
    close(i);
  }

  if (ForegroundOpt == 0)
  {
    int fd;
    int pid;

    if ((fd = open("/dev/tty", O_RDWR)) >= 0)
    {
      ioctl(fd, TIOCNOTTY, 0);
      close(fd);
    }

    pid = fork();

    if (pid < 0)
    {
      perror("fork");
      exit(1);
    }
    if (pid > 0)
      exit(0);
  }

  logn(9, "%s " VERSION " dillon, started\n", argv[0]);

  SynchronizeDir(CDir, NULL, 1);
  SynchronizeDir(SCDir, "root", 1);

  RunMainLoop(); /* does not return */

  return 1;
}
