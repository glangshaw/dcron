#ifndef DEFS_H
#define DEFS_H

//  DEFS.H
//
//  Copyright 1994-1998 Matthew Dillon (dillon@backplane.com)
//  Copyright 2026 Gary Langshaw (gary.langshaw@gmail.com)
//  May be distributed under the GNU General Public License

#ifndef CRONTABS
#define CRONTABS "/var/lib/cron/crontabs"
#endif
#ifndef CRONMAIL
#define CRONMAIL "/var/spool/cron"
#endif
#ifndef SCRONTABS
#define SCRONTABS "/etc/cron.d"
#endif
#ifndef TMPDIR
#define TMPDIR "/tmp"
#endif
#ifndef OPEN_MAX
#define OPEN_MAX 256
#endif

#ifndef DEFAULT_PATH
#define DEFAULT_PATH                                                           \
  "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
#endif

#ifndef SENDMAIL
#define SENDMAIL "/usr/sbin/sendmail"
#endif
#ifndef SENDMAIL_ARGS
#define SENDMAIL_ARGS "-t", "-oem", "-i"
#endif
#ifndef CRONUPDATE
#define CRONUPDATE "cron.update"
#endif
#ifndef MAXLINES
#define MAXLINES 256 /* max lines in non-root crontabs */
#endif
#ifndef PATH_VI
#define PATH_VI "/usr/bin/vi" /* location of vi */
#endif

#define VERSION "V3.3-dev"

#include <stdint.h>

typedef struct CronFile
{
  struct CronFile *cf_Next;
  struct CronLine *cf_LineBase;
  char *cf_DPath;    /* Directory path to cronfile */
  char *cf_FileName; /* Name of cronfile */
  char *cf_UserName; /* username to execute jobs as */
  int cf_Ready;      /* bool: one or more jobs ready */
  int cf_Running;    /* bool: one or more jobs running */
  int cf_Deleted;    /* marked for deletion, ignore */
} CronFile;

typedef struct CronLine
{
  struct CronLine *cl_Next;
  unsigned int cl_LineNum; /* Line Number of the crontab file that contains this entry */
  char *cl_Shell;          /* shell command */
  int cl_Pid;              /* running pid, 0, or armed (-1) */
  int cl_MailFlag;         /* running pid is for mail */
  int cl_MailPos;          /* 'empty file' size */
  uint64_t cl_Minutes;     /* bitmask for minutes 2^n where n = 0..59 */
  uint32_t cl_Hours;       /* bitmask for hours 2^n where n = 0..23 */
  uint32_t cl_DayOfMonth;  /* bitmask for days 2^(n-1) where n = 1..31, i.e. bit
                              0 is day 1 */
  uint16_t cl_Month;       /* bitmask for minutes 2^n where n = 0..11 */
  uint8_t cl_DayOfWeek;    /* bitmask for days of week 2^n where n = 0..6,
                             beginning Sunday */
} CronLine;

#define RUN_RANOUT 1
#define RUN_RUNNING 2
#define RUN_FAILED 3

extern int LogLevel;

#include <sys/types.h>
extern uid_t DaemonUid;

#endif /* DEFS_H */
