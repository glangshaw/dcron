//  database.c
//
//  Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
//  Copyright 2026 Gary Langshaw (gary.langshaw@gmail.com)
//  May be distributed under the GNU General Public License

#include "database.h"

#include "bitset.h"
#include "defs.h"
#include "job.h"
#include "subs.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>

void SynchronizeFile(const char *dpath, const char *fname, const char *uname);
void DeleteFile(CronFile **pfile);
uint64_t ParseField(char *user, int wrap, int off, const char **names,
                    char **pptr);

CronFile *FileBase;

const char *DowAry[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", NULL};

const char *MonAry[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul",
                        "Aug", "Sep", "Oct", "Nov", "Dec", NULL};

void CheckUpdates(const char *dpath, const char *user_override)
{
  //  Check the cron.update file in the specified directory.  If
  //  user_override is NULL then the files in the directory belong to
  //  the user whos name is the file, otherwise they belong to the
  //  user_override user.

  FILE *fi;
  char *lineBuf = NULL;
  size_t lineBufSize;
  ssize_t lineLength;
  char *ptr;
  char *path;

  syslog(LOG_DEBUG, "CheckUpdates on %s/%s", dpath, CRONUPDATE);

  asprintf(&path, "%s/%s", dpath, CRONUPDATE);
  if ((fi = fopen(path, "r")) != NULL)
  {
    remove(path);
    while ((lineLength = getline(&lineBuf, &lineBufSize, fi)) != -1)
    {
      ptr = strtok(lineBuf, " \t\r\n");
      if (user_override)
        SynchronizeFile(dpath, ptr, user_override);
      else if (getpwnam(ptr))
        SynchronizeFile(dpath, ptr, ptr);
      else
        syslog(LOG_INFO, "ignoring %s/%s (non-existant user)", dpath, ptr);
    }
    if (ferror(fi))
      syslog(LOG_ERR, "getline() error reading %s: %s", CRONUPDATE,
             strerror(errno));

    fclose(fi);
    free(lineBuf);
  }
  free(path);
}

void SynchronizeDir(const char *dpath, const char *user_override,
                    int initial_scan)
{
  CronFile **pfile;
  CronFile *file;
  struct dirent *den;
  DIR *dir;
  char *path;

  //  Delete all database files for this directory.  DeleteFile() will
  //  free *pfile and relink the *pfile pointer, or in the alternative
  //  will mark it as deleted.

  pfile = &FileBase;
  while ((file = *pfile) != NULL)
  {
    if (file->cf_Deleted == 0 && strcmp(file->cf_DPath, dpath) == 0)
    {
      DeleteFile(pfile);
    }
    else
    {
      pfile = &file->cf_Next;
    }
  }

  //  Since we are resynchronizing the entire directory, remove the
  //  the CRONUPDATE file.

  asprintf(&path, "%s/%s", dpath, CRONUPDATE);
  remove(path);
  free(path);

  // Scan the specified directory

  if ((dir = opendir(dpath)) != NULL)
  {
    while ((den = readdir(dir)) != NULL)
    {
      if (den->d_name[0] == '.')
        continue;
      if (strcmp(den->d_name, CRONUPDATE) == 0)
        continue;
      if (!user_override && !getpwnam(den->d_name))
      {
        syslog(LOG_INFO, "ignoring %s/%s (non-existant user)", dpath,
               den->d_name);
        continue;
      }
      SynchronizeFile(dpath, den->d_name,
                      (user_override) ? user_override : den->d_name);
    }
    closedir(dir);
  }
  else if (initial_scan) /* softerror, do not exit the program */
    syslog(LOG_ERR, "Unable to scan directory %s", dpath);
}

void SynchronizeFile(const char *dpath, const char *fileName,
                     const char *userName)
{
  CronFile **pfile;
  CronFile *file;
  int maxEntries;
  int maxLines;
  char *lineBuf = NULL;
  size_t lineBufSize = 0;
  ssize_t lineLength;
  char *path;
  FILE *fi;

  // Limit entries

  if (strcmp(userName, "root") == 0)
    maxEntries = 65535;
  else
    maxEntries = MAXLINES;
  maxLines = maxEntries * 10;

  //  Delete any existing copy of this file

  pfile = &FileBase;
  while ((file = *pfile) != NULL)
  {
    if (file->cf_Deleted == 0 && strcmp(file->cf_DPath, dpath) == 0 &&
        strcmp(file->cf_FileName, fileName) == 0)
    {
      DeleteFile(pfile);
    }
    else
    {
      pfile = &file->cf_Next;
    }
  }

  asprintf(&path, "%s/%s", dpath, fileName);
  if ((fi = fopen(path, "r")) != NULL)
  {
    struct stat sbuf;

    if (fstat(fileno(fi), &sbuf) == 0 && sbuf.st_uid == DaemonUid)
    {
      CronFile *file = calloc(1, sizeof(CronFile));
      CronLine **pline;

      file->cf_UserName = strdup(userName);
      file->cf_FileName = strdup(fileName);
      file->cf_DPath = strdup(dpath);
      pline = &file->cf_LineBase;

      while ((lineLength = getline(&lineBuf, &lineBufSize, fi)) != -1 &&
             --maxLines)
      {
        CronLine line;
        char *ptr = lineBuf;

        if (lineLength > 0 && lineBuf[lineLength - 1] == '\n')
          lineBuf[--lineLength] = '\0';

        ptr += strspn(ptr, " \t\n");

        if (*ptr == '\0' || *ptr == '#')
          continue;

        if (--maxEntries == 0)
          break;

        memset(&line, 0, sizeof(line));

        syslog(LOG_DEBUG, "User:  %s", userName);
        syslog(LOG_DEBUG, "Entry:  %s", lineBuf);

        //  parse date ranges

        line.cl_Minutes = ParseField(file->cf_UserName, 60, 0, NULL, &ptr);
        line.cl_Hours =
            (uint32_t)ParseField(file->cf_UserName, 24, 0, NULL, &ptr);
        line.cl_DayOfMonth =
            (uint32_t)ParseField(file->cf_UserName, 31, -1, NULL, &ptr);
        line.cl_Month =
            (uint16_t)ParseField(file->cf_UserName, 12, -1, MonAry, &ptr);
        line.cl_DayOfWeek =
            (uint8_t)ParseField(file->cf_UserName, 7, 0, DowAry, &ptr);

        //  check failure

        if (ptr == NULL)
          continue;

        // Handle unspecified fields (those marked as '*')
        //
        //  We can't let ParseField() set the bits for the unspecified
        //  fields individually owing to the interactions between some
        //  of them.

        // When Month is specified and DayOfMonths is not,
        // set all days of month:

        if (line.cl_Month && !line.cl_DayOfMonth)
          line.cl_DayOfMonth = ~UINT32_C(0);

        //  If both day fields are '*' then we need to set at least
        //  one of them.  DayOfWeek is the first test condition in
        //  TestJobs() and will short-circuit the Month and DayOfMonth
        //  checks, however we'll set both day fields for cosmetic
        //  reasons:

        if (!line.cl_DayOfWeek && !line.cl_DayOfMonth)
        {
          line.cl_DayOfWeek = ~UINT8_C(0);
          line.cl_DayOfMonth = ~UINT32_C(0);
        }

        // When Month is not specified, set all months:

        if (!line.cl_Month)
          line.cl_Month = ~UINT16_C(0);

        // When Minutes is unspecified, set all minutes:
        if (!line.cl_Minutes)
          line.cl_Minutes = ~UINT64_C(0);

        // When Hours is unspecified, set all hours:
        if (!line.cl_Hours)
          line.cl_Hours = ~UINT32_C(0);

        syslog(LOG_DEBUG,
               "Bitset:  Mins %016" PRIX64 ", Hours %08" PRIX32
               ", Days %08" PRIX32 ", Months %04" PRIX16 ", DayOfWeek %02" PRIX8,
               line.cl_Minutes, line.cl_Hours, line.cl_DayOfMonth,
               line.cl_Month, line.cl_DayOfWeek);

        *pline = calloc(1, sizeof(CronLine));
        **pline = line;

        //  copy command

        (*pline)->cl_Shell = strdup(ptr);

        syslog(LOG_DEBUG, "Command:  %s", ptr);

        pline = &((*pline)->cl_Next);
      }
      free(lineBuf);

      *pline = NULL;

      file->cf_Next = FileBase;
      FileBase = file;

      if (maxLines == 0 || maxEntries == 0)
        syslog(LOG_INFO, "Maximum number of lines reached for user %s",
               userName);
    }
    fclose(fi);
  }
  free(path);
}

uint64_t ParseField(char *user, int wrap, int off, const char **names,
                    char **pptr)
{
  int n1 = -1;
  int n2 = -1;
  uint64_t bits = 0;

  char *ptr;

  if (pptr == NULL || *pptr == NULL)
    return 0;

  ptr = *pptr;

  while (*ptr != ' ' && *ptr != '\t' && *ptr != '\n')
  {
    int step = 0;

    //  Handle numeric digit or symbol or '*'

    if (*ptr == '*')
    {
      ++ptr;

      if (*ptr == '\t' || *ptr == ' ' || *ptr == ',')
      {
        // unspecified field

        // advance pointer over any remaining subfields (which will
        // be redundant) and interfield whitespace, and then return 0:
        ptr += strcspn(ptr, " \t");
        *pptr = ptr + strspn(ptr, " \t");
        return UINT64_C(0);
      }
      else
      {
        n1 = 0;
        n2 = wrap - 1;
        step = 1;
      }
    }
    else if (*ptr >= '0' && *ptr <= '9')
    {
      if (n1 < 0)
        n1 = strtol(ptr, &ptr, 10) + off;
      else
        n2 = strtol(ptr, &ptr, 10) + off;
      step = 1;
    }
    else if (names)
    {
      int i;

      for (i = 0; names[i]; ++i)
      {
        if (strncasecmp(ptr, names[i], strlen(names[i])) == 0)
        {
          break;
        }
      }
      if (names[i])
      {
        ptr += strlen(names[i]);
        if (n1 < 0)
          n1 = i;
        else
          n2 = i;
        step = 1;
      }
    }

    //  handle optional range '-'

    if (step == 0)
    {
      syslog(LOG_NOTICE, "failed user %s parsing %s", user, *pptr);
      *pptr = NULL;
      return UINT64_C(0);
    }
    if (*ptr == '-' && n2 < 0)
    {
      ++ptr;
      continue;
    }

    //  collapse single-value ranges, handle stepmark, and fill in the
    //  bitset appropriately.

    if (n2 < 0)
      n2 = n1;

    if (*ptr == '/')
      step = strtol(ptr + 1, &ptr, 10);

    // Set apprropriate bits for range:

    if (step > 0)
      bits = setbits64(bits, wrap, n1, n2, step);

    if (*ptr != ',')
      break;
    ++ptr;
    n1 = -1;
    n2 = -1;
  }
  if (*ptr != ' ' && *ptr != '\t' && *ptr != '\n')
  {
    syslog(LOG_NOTICE, "failed user %s parsing %s", user, *pptr);
    *pptr = NULL;
    return UINT64_C(0);
  }

  ptr += strspn(ptr, " \t");

  *pptr = ptr;
  return bits;
}

void DeleteFile(CronFile **pfile)
{
  //  DeleteFile() - destroy a CronFile.
  //
  //  The CronFile (*pfile) is destroyed if possible, and marked
  //  cf_Deleted if there are still active processes running on it.
  //  *pfile is relinked on success.

  CronFile *file = *pfile;
  CronLine **pline = &file->cf_LineBase;
  CronLine *line;

  file->cf_Running = 0;
  file->cf_Deleted = 1;

  while ((line = *pline) != NULL)
  {
    if (line->cl_Pid > 0)
    {
      file->cf_Running = 1;
      pline = &line->cl_Next;
    }
    else
    {
      *pline = line->cl_Next;
      free(line->cl_Shell);
      free(line);
    }
  }
  if (file->cf_Running == 0)
  {
    *pfile = file->cf_Next;
    free(file->cf_DPath);
    free(file->cf_FileName);
    free(file->cf_UserName);
    free(file);
  }
}

int TestJobs(time_t t1, time_t t2)
{
  //  TestJobs()
  //
  //  determine which jobs need to be run.  Under normal conditions,
  //  the period is about a minute (one scan).  Worst case it will be
  //  one hour (60 scans).

  int nJobs = 0;
  time_t t;

  //  Find jobs > t1 and <= t2

  for (t = t1 - t1 % 60; t <= t2; t += 60)
  {
    if (t > t1)
    {
      struct tm *tp = localtime(&t);
      CronFile *file;
      CronLine *line;

      uint64_t minMask = UINT64_C(1) << tp->tm_min;
      uint32_t hrsMask = UINT32_C(1) << tp->tm_hour;
      uint32_t dayMask = UINT32_C(1) << (tp->tm_mday - 1);
      uint16_t monMask = UINT16_C(1) << tp->tm_mon;
      uint8_t dowMask = UINT8_C(1) << tp->tm_wday;

      for (file = FileBase; file; file = file->cf_Next)
      {
        syslog(LOG_DEBUG, "FILE %s/%s (user %s):", file->cf_DPath,
               file->cf_FileName, file->cf_UserName);
        if (file->cf_Deleted)
          continue;
        for (line = file->cf_LineBase; line; line = line->cl_Next)
        {
          syslog(LOG_DEBUG, "    LINE %s", line->cl_Shell);
          if (line->cl_Minutes & minMask && line->cl_Hours & hrsMask &&
              (line->cl_DayOfWeek & dowMask ||
               (line->cl_DayOfMonth & dayMask && line->cl_Month & monMask)))
          {
            syslog(LOG_DEBUG, "    JobToDo: %d %s", line->cl_Pid,
                   line->cl_Shell);
            if (line->cl_Pid > 0)
            {
              syslog(LOG_DEBUG, "    process already running: %s",
                     line->cl_Shell);
            }
            else if (line->cl_Pid == 0)
            {
              line->cl_Pid = -1;
              file->cf_Ready = 1;
              ++nJobs;
            }
          }
        }
      }
    }
  }
  return (nJobs);
}

void RunJobs(void)
{
  CronFile *file;
  CronLine *line;

  for (file = FileBase; file; file = file->cf_Next)
  {
    if (file->cf_Ready)
    {
      file->cf_Ready = 0;

      for (line = file->cf_LineBase; line; line = line->cl_Next)
      {
        if (line->cl_Pid < 0)
        {

          RunJob(file, line);

          syslog(LOG_DEBUG, "FILE %s/%s USER %s pid %3d cmd %s",
                 file->cf_DPath, file->cf_FileName, file->cf_UserName,
                 line->cl_Pid, line->cl_Shell);
          if (line->cl_Pid < 0)
            file->cf_Ready = 1;
          else if (line->cl_Pid > 0)
            file->cf_Running = 1;
        }
      }
    }
  }
}

int CheckJobs(void)
{
  //  CheckJobs() - check for job completion
  //
  //  Check for job completion, return number of jobs still running
  //  after all done.

  CronFile *file;
  CronLine *line;
  int nStillRunning = 0;

  for (file = FileBase; file; file = file->cf_Next)
  {
    if (file->cf_Running)
    {
      file->cf_Running = 0;

      for (line = file->cf_LineBase; line; line = line->cl_Next)
      {
        if (line->cl_Pid > 0)
        {
          int status;
          int r = waitpid(line->cl_Pid, &status, WNOHANG);

          if (r < 0 || r == line->cl_Pid)
          {
            EndJob(file, line);
            if (line->cl_Pid)
              file->cf_Running = 1;
          }
          else if (r == 0)
          {
            file->cf_Running = 1;
          }
        }
      }
    }
    nStillRunning += file->cf_Running;
  }
  return (nStillRunning);
}
