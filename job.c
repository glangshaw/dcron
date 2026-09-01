//  JOB.C
//
//  Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
//  Copyright 2026 Gary Langshaw (gary.langshaw@gmail.com)
//  May be distributed under the GNU General Public License

#include "job.h"
#include "chuser.h"
#include "defs.h"
#include "subs.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

void RunJob(CronFile *file, CronLine *line)
{
  char mailFile[PATH_MAX];
  int mailFd;

  line->cl_Pid = 0;
  line->cl_MailFlag = 0;

  //  open mail file - owner root so nobody can screw with it.

  snprintf(mailFile, sizeof(mailFile), CRONMAIL "/cron.%s.%d",
           file->cf_UserName, (int)getpid());
  mailFd =
      open(mailFile, O_CREAT | O_TRUNC | O_WRONLY | O_EXCL | O_APPEND, 0600);

  if (mailFd >= 0)
  {
    line->cl_MailFlag = 1;
    fdprintf(mailFd, "To: %s\nSubject: cron: %s\n\n", file->cf_UserName,
             line->cl_Shell);
    line->cl_MailPos = lseek(mailFd, 0, 1);
  }

  //  Fork as the user in question and run program

  if ((line->cl_Pid = fork()) == 0)
  {
    //  CHILD, FORK OK

    // Create a new process group - parent will also do this.
    setpgid(0, 0);

    // The POSIX base specification requires a default environment be
    // setup for cron jobs containing at least, HOME, LOGNAME, SHELL
    // and PATH, where SHELL will be the path to 'sh'.
    // ChangeUser() will set the first three of these, only leaving
    // PATH to be set here.
    clearenv();
    setenv("PATH", DEFAULT_PATH, 1);

    //  Change running state to the user in question.
    if (ChangeUser(file->cf_UserName, 1) < 0)
    {
      syslog(LOG_ERR, "ChangeUser failed (%s): %s", file->cf_UserName,
             strerror(errno));
      exit(0);
    }

    syslog(LOG_INFO, "%s:%d  Starting job (pid: %d): %s", file->cf_FileName,
           line->cl_LineNum, getpid(), line->cl_Shell);

    //  Setup close-on-exec descriptor in case exec fails

    dup2(2, 8);
    fcntl(8, F_SETFD, 1);
    fclose(stderr);

    //  stdin is already /dev/null, setup stdout and stderr

    if (mailFd >= 0)
    {
      dup2(mailFd, 1);
      dup2(mailFd, 2);
      close(mailFd);
    }
    else
    {
      syslog(LOG_ERR,
             "%s:%d  unable to create mail file %s, output to /dev/null",
             file->cf_FileName, line->cl_LineNum, mailFile);
    }
    execl("/bin/sh", "/bin/sh", "-c", line->cl_Shell, NULL, NULL);
    syslog(LOG_ERR, "unable to exec, user %s cmd /bin/sh -c %s",
           file->cf_UserName, line->cl_Shell);
    exit(0);
  }
  else if (line->cl_Pid < 0)
  {
    //  PARENT, FORK FAILED

    syslog(LOG_ERR, "%s:%d  fork() failed!", file->cf_FileName,
           line->cl_LineNum);
    line->cl_Pid = 0;
    remove(mailFile);
  }
  else
  {
    //  PARENT, FORK SUCCESS

    char mailFile2[PATH_MAX];

    //  Put child in its own process group
    setpgid(line->cl_Pid, 0);

    //  rename mail-file based on pid of process
    snprintf(mailFile2, sizeof(mailFile2), CRONMAIL "/cron.%s.%d",
             file->cf_UserName, line->cl_Pid);
    rename(mailFile, mailFile2);
  }

  //  Close the mail file descriptor.. we can't just leave it open in
  //  a structure, closing it later, because we might run out of
  //  descriptors

  if (mailFd >= 0)
    close(mailFd);
}

void EndJob(CronFile *file, CronLine *line)
{
  //  EndJob() - called when job terminates and when mail terminates
  int mailFd;
  char mailFile[PATH_MAX];
  struct stat sbuf;

  if (line->cl_Pid <= 0) /* no job */
  {
    line->cl_Pid = 0;
    return;
  }

  //  End of job and no mail file
  //  End of sendmail job

  snprintf(mailFile, sizeof(mailFile), CRONMAIL "/cron.%s.%d",
           file->cf_UserName, line->cl_Pid);
  line->cl_Pid = 0;

  if (line->cl_MailFlag != 1)
    return;

  line->cl_MailFlag = 0;

  //  End of primary job - check for mail file.  If size has increased
  //  and the file is still valid, we sendmail it.

  mailFd = open(mailFile, O_RDONLY);
  remove(mailFile);
  if (mailFd < 0)
  {
    return;
  }
  if (fstat(mailFd, &sbuf) < 0 || sbuf.st_uid != DaemonUid ||
      sbuf.st_nlink != 0 || sbuf.st_size == line->cl_MailPos ||
      !S_ISREG(sbuf.st_mode))
  {
    close(mailFd);
    return;
  }

  if ((line->cl_Pid = fork()) == 0)
  {
    //  CHILD, FORK OK

    //  change user id - no way in hell security can be compromised by
    //  the mailing and we already verified the mail file.

    if (ChangeUser(file->cf_UserName, 1) < 0)
    {
      syslog(LOG_ERR, "%s:%d  ChangeUser failed, unable to send mail!",
             file->cf_FileName, line->cl_LineNum);
      exit(0);
    }

    //  create close-on-exec log descriptor in case exec fails

    dup2(2, 8);
    fcntl(8, F_SETFD, 1);

    fclose(stderr);

    //  run sendmail with mail file as standard input, only if mail
    //  file exists!

    dup2(mailFd, 0);
    dup2(1, 2);
    close(mailFd);

    execl(SENDMAIL, SENDMAIL, SENDMAIL_ARGS, NULL, NULL);
    syslog(LOG_ERR, "unable to exec %s, user %s, output to sink null", SENDMAIL,
           file->cf_UserName);
    exit(0);
  }
  else if (line->cl_Pid < 0)
  {
    //  PARENT, FORK FAILED

    syslog(LOG_ERR, "%s:%d  fork() failed!", file->cf_FileName,
           line->cl_LineNum);
    line->cl_Pid = 0;
  }
  else
  {
    // PARENT, FORK OK
  }
  close(mailFd);
}
