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
uint64_t ParseField(char *user, int modvalue, int off, int star,
                    const char **names, char **pptr);

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

  logn(LOG_DEBUG, "CheckUpdates on %s/%s\n", dpath, CRONUPDATE);

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
        logn(LOG_INFO, "ignoring %s/%s (non-existant user)\n", dpath, ptr);
    }
    if (ferror(fi))
      logn(LOG_ERR, "getline() error reading %s: %s\n", CRONUPDATE,
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
        logn(LOG_INFO, "ignoring %s/%s (non-existant user)\n", dpath,
             den->d_name);
        continue;
      }
      SynchronizeFile(dpath, den->d_name,
                      (user_override) ? user_override : den->d_name);
    }
    closedir(dir);
  }
  else if (initial_scan) /* softerror, do not exit the program */
    logn(LOG_ERR, "Unable to scan directory %s!\n", dpath);
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

        logn(LOG_DEBUG, "User %s Entry %s\n", userName, lineBuf);

        //  parse date ranges

        line.cl_Minutes = ParseField(file->cf_UserName, 60, 0, 1, NULL, &ptr);
        line.cl_Hours =
            (uint32_t)ParseField(file->cf_UserName, 24, 0, 1, NULL, &ptr);
        line.cl_DayOfMonth =
            (uint32_t)ParseField(file->cf_UserName, 31, -1, 0, NULL, &ptr);
        line.cl_Month =
            (uint16_t)ParseField(file->cf_UserName, 12, -1, 0, MonAry, &ptr);
        line.cl_DayOfWeek =
            (uint8_t)ParseField(file->cf_UserName, 7, 0, 0, DowAry, &ptr);

        // Fixups for fields marked as '*'
        //
        //  We can't let PaseField() set all bits for Day/Month and
        //  DayOfWeek independently as we do for Hours and Minutes
        //  owing to the interactions between them.

        // When Month is specified and DayOfMonths is not,
        // set all days of month.

        if (line.cl_Month && !line.cl_DayOfMonth)
          line.cl_DayOfMonth = ~UINT32_C(0);

        //  If both day fields are '*' then we need to set at least
        //  one of them.  Use DayOfWeek as that is the first test
        //  condition in TestJobs() and will short-circuit the
        //  remaining condition checks.

        if (!line.cl_DayOfWeek && !line.cl_DayOfMonth)
          line.cl_DayOfWeek = ~UINT8_C(0);

        // When Month is not specified, set all months

        if (!line.cl_Month)
          line.cl_Month = ~UINT16_C(0);

        logn(LOG_DEBUG, "    bitsMins: %016" PRIX64 "\n", line.cl_Minutes);
        logn(LOG_DEBUG, "    bitsHrs:  %08" PRIX32 "\n", line.cl_Hours);
        logn(LOG_DEBUG, "    bitsDays: %08" PRIX32 "\n", line.cl_DayOfMonth);
        logn(LOG_DEBUG, "    bitsMons: %04" PRIX16 "\n", line.cl_Month);
        logn(LOG_DEBUG, "    bitsDow:  %02" PRIX8 "\n", line.cl_DayOfWeek);

        //  check failure

        if (ptr == NULL)
          continue;

        *pline = calloc(1, sizeof(CronLine));
        **pline = line;

        //  copy command

        (*pline)->cl_Shell = strdup(ptr);

        logn(LOG_DEBUG, "    Command %s\n", ptr);

        pline = &((*pline)->cl_Next);
      }
      free(lineBuf);

      *pline = NULL;

      file->cf_Next = FileBase;
      FileBase = file;

      if (maxLines == 0 || maxEntries == 0)
        logn(LOG_INFO, "Maximum number of lines reached for user %s\n",
             userName);
    }
    fclose(fi);
  }
  free(path);
}

uint64_t ParseField(char *user, int modvalue, int off, int star,
                    const char **names, char **pptr)
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
    int skip = 0;

    //  Handle numeric digit or symbol or '*'

    if (*ptr == '*')
    {
      n1 = 0; /* everything will be filled */
      n2 = modvalue - 1;
      skip = 1;
      ++ptr;
    }
    else if (*ptr >= '0' && *ptr <= '9')
    {
      if (n1 < 0)
        n1 = strtol(ptr, &ptr, 10) + off;
      else
        n2 = strtol(ptr, &ptr, 10) + off;
      skip = 1;
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
        skip = 1;
      }
    }

    //  handle optional range '-'

    if (skip == 0)
    {
      logn(LOG_NOTICE, "failed user %s parsing %s\n", user, *pptr);
      *pptr = NULL;
      return 0;
    }
    if (*ptr == '-' && n2 < 0)
    {
      ++ptr;
      continue;
    }

    //  collapse single-value ranges, handle skipmark, and fill in the
    //  character array appropriately.

    if (n2 < 0)
      n2 = n1;

    if (*ptr == '/')
      skip = strtol(ptr + 1, &ptr, 10);

    // Set apprropriate bits for range: skip when all bits and star is 0:

    if (n1 != 0 || n2 != modvalue - 1 || skip != 1 || star == 1)
      bits = setbits64(bits, modvalue, n1, n2, skip);

    if (*ptr != ',')
      break;
    ++ptr;
    n1 = -1;
    n2 = -1;
  }
  if (*ptr != ' ' && *ptr != '\t' && *ptr != '\n')
  {
    logn(LOG_NOTICE, "failed user %s parsing %s\n", user, *pptr);
    *pptr = NULL;
    return 0;
  }

  while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n')
    ++ptr;

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
        logn(LOG_DEBUG, "FILE %s/%s (user %s):\n", file->cf_DPath,
             file->cf_FileName, file->cf_UserName);
        if (file->cf_Deleted)
          continue;
        for (line = file->cf_LineBase; line; line = line->cl_Next)
        {
          logn(LOG_DEBUG, "    LINE %s\n", line->cl_Shell);
          if (line->cl_Minutes & minMask && line->cl_Hours & hrsMask &&
              (line->cl_DayOfWeek & dowMask ||
               (line->cl_DayOfMonth & dayMask && line->cl_Month & monMask)))
          {
            logn(LOG_DEBUG, "    JobToDo: %d %s\n", line->cl_Pid,
                 line->cl_Shell);
            if (line->cl_Pid > 0)
            {
              logn(LOG_DEBUG, "    process already running: %s\n",
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

          logn(LOG_DEBUG, "FILE %s/%s USER %s pid %3d cmd %s\n", file->cf_DPath,
               file->cf_FileName, file->cf_UserName, line->cl_Pid,
               line->cl_Shell);
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
