//  crond.c
//
//  run as root, but NOT setuid root
//
//  Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
//  Copyright 2026 Gary Langshaw (gary.langshaw@gmail.com)
//  May be distributed under the GNU General Public License

#include "database.h"
#include "defs.h"
#include "job.h"
#include "subs.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define ONE_HOUR 3600
#define ONE_MINUTE 60

#ifndef RESCAN_INTERVAL
#define RESCAN_INTERVAL ONE_HOUR
#endif

#ifndef WAKEUP_INTERVAL
#define WAKEUP_INTERVAL ONE_MINUTE
#endif

#ifndef CHECKJOBS_INTERVAL
#define CHECKJOBS_INTERVAL 10
#endif

int LogLevel = 5;
int BackgroundOpt = 0;
const char *CDir = CRONTABS;
const char *SCDir = SCRONTABS;
uid_t DaemonUid;
int InSyncFileRoot;

volatile int sig_chld = 0;

void SigHandler(int sig)
{
  switch (sig)
  {
  case SIGCHLD:
    sig_chld = 1;
    break;
  }
}

void RunMainLoop()
{
  time_t t1;
  time_t t2;
  time_t rescan; /* time of last rescan */
  unsigned int stime;

  t1 = time(NULL);
  t1 = t1 - t1 % WAKEUP_INTERVAL;
  rescan = t1 - t1 % RESCAN_INTERVAL;

  for (;;)
  {
    //  synchronize to 1 second after the minute, minimum sleep of 1 second.

    stime = sleep(WAKEUP_INTERVAL + 1 - time(NULL) % WAKEUP_INTERVAL);
    t2 = time(NULL);

    logn(7, "Wakeup(%s): %s", ((stime > 0) ? "interrupted" : "scheduled"),
         ctime(&t2));

    //  Check for disparity: disparities over an hour either way
    //  result in resynchronization.  A reverse-indexed disparity less
    //  then an hour causes us to effectively sleep until we match the
    //  original time (i.e. no re-execution of jobs that have just
    //  been run).  A forward-indexed disparity less then an hour
    //  causes intermediate jobs to be run, but only once in the worst
    //  case.

    if (t2 < t1 - ONE_HOUR || t2 > t1 + ONE_HOUR)
    {
      rescan = t2 - t2 % RESCAN_INTERVAL;
      t1 = t2 - t2 % WAKEUP_INTERVAL;
      logn(5, "time disparity greater than one hour detected.\n");
    }

    if (stime == 0 && t2 >= t1)
    {
      if (rescan + RESCAN_INTERVAL <= t2)
      {
        //  The directory is rescanned once an hour to deal with any
        //  screwups.
        rescan = t2 - t2 % RESCAN_INTERVAL;
        SynchronizeDir(CDir, NULL, 0);
        SynchronizeDir(SCDir, "root", 0);
      }
      else
      {
        //  The file 'cron.update' is checked to determine new cron
        //  jobs.
        CheckUpdates(CDir, NULL);
        CheckUpdates(SCDir, "root");
      }

      //  when running jobs, the inequality used is greater but
      //  not equal to t1, and less then or equal to t2.
      TestJobs(t1, t2);
      RunJobs();
      t1 = t2;
    }
    CheckJobs();
  }
}

int main(int argc, char **argv)
{
  extern char *optarg;
  int i;
  int opt;
  struct sigaction sa;

  //  parse options

  DaemonUid = getuid();

  while ((opt = getopt(argc, argv, "bc:l:s:")) != -1)
  {
    switch (opt)
    {
    case 'b':
      BackgroundOpt = 1;
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

  //  close stdin, stdout, and unused descriptors.
  //  (stderr normally redirected by caller).

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

  if (BackgroundOpt == 1)
  {
    int pid;

    pid = fork();

    if (pid < 0)
    {
      perror("fork");
      exit(1);
    }
    if (pid > 0)
      exit(0);

    setsid();
  }

  logn(5, "%s " VERSION " dillon, started\n", argv[0]);

  //  establish a signal handler for SIGCHLD.

  sa.sa_handler = SigHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  if (sigaction(SIGCHLD, &sa, NULL) == -1)
    logn(3, "failed to establish sig_handler");

  SynchronizeDir(CDir, NULL, 1);
  SynchronizeDir(SCDir, "root", 1);

  RunMainLoop(); /* does not return */

  return 1;
}
