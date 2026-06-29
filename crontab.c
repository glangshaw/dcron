//  crontab.c
//
//  Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
//  Copyright 2026 Gary Langshaw (gary.langshaw@gmail.com)
//  May be distributed under the GNU General Public License

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "defs.h"
#include "subs.h"

const char *CDir = CRONTABS;
const char *CDirOpt = NULL;
int UserId;
int LogLevel = 9;

void EditFile(const char *user, const char *file);
int GetReplaceStream(const char *user, const char *file);

int main(int argc, char *argv[])
{
  enum
  {
    NONE,
    EDIT,
    LIST,
    REPLACE,
    DELETE
  } option = NONE;
  struct passwd *pas;
  char edFile[] = TMPDIR "/crontab.XXXXXX";
  char *repFileName = NULL;
  char *userName = NULL;
  int repFd = 0;
  char caller[256]; /* user that ran program */
  int opt;

  UserId = getuid();
  if ((pas = getpwuid(UserId)) == NULL)
  {
    perror("getpwuid");
    exit(1);
  }
  snprintf(caller, sizeof(caller), "%s", pas->pw_name);

#define ERRMSG_EXCLUSIVE_OPTION "only one of: -r, -e, -l can be specified."
  while ((opt = getopt(argc, argv, "ldreu:")) != -1)
  {
    if (opt == '?')
      errx(1, "aborted. unrecognised option");
    switch (opt)
    {
    case 'c':
      CDirOpt = optarg;
      break;
    case 'u':
      userName = optarg;
      break;
    case 'e':
      if (option != NONE)
        errx(1, ERRMSG_EXCLUSIVE_OPTION);
      option = EDIT;
      break;
    case 'l':
      if (option != NONE)
        errx(1, ERRMSG_EXCLUSIVE_OPTION);
      option = LIST;
      break;
    case 'd': /* for backward compatibility, POSIX option is -r */
    case 'r':
      if (option != NONE)
        errx(1, ERRMSG_EXCLUSIVE_OPTION);
      option = DELETE;
      break;
    default:
      break;
    }
  }
#undef ERRMSG_EXCLUSIVE_OPTION

  if (optind != argc && (option == LIST || option == EDIT || option == DELETE))
    errx(1, "unexpected FILE argument: %s\n", argv[optind]);

  if (argc > optind + 1)
    errx(1, "too many arguments\n");

  if (option == NONE)
  {
    option = REPLACE;
    if (optind == argc - 1)
      repFileName = argv[optind];
    else
      repFileName = "-";
  }

  if (userName)
  {
    if (getuid() == (uid_t)0)
    {
      pas = getpwnam(userName);
      if (pas)
      {
        UserId = pas->pw_uid;
      }
      else
      {
        errx(1, "user %s unknown\n", userName);
      }
    }
    else
      errx(1, "only the superuser may specify a user\n");
  }

  if (CDirOpt)
  {
    if (getuid() == (uid_t)0)
      CDir = CDirOpt;
    else
      errx(1, "-c option: superuser only\n");
  }

  //  Get password entry

  if ((pas = getpwuid(UserId)) == NULL)
  {
    perror("getpwuid");
    exit(1);
  }

  //  Change directory to our crontab directory

  if (chdir(CDir) < 0)
    errx(1, "cannot change dir to %s: %s\n", CDir, strerror(errno));

  //  Handle options as appropriate

  switch (option)
  {
  case LIST:
  {
    FILE *fi;
    char buf[1024];

    if ((fi = fopen(pas->pw_name, "r")))
    {
      while (fgets(buf, sizeof(buf), fi) != NULL)
        fputs(buf, stdout);
      fclose(fi);
    }
    else
    {
      fprintf(stderr, "no crontab for %s\n", pas->pw_name);
    }
  }
  break;
  case EDIT:
  {
    FILE *fi;
    int fd;
    int n;
    char buf[1024];

    if ((fd = mkstemp(edFile)) >= 0)
    {
      chown(edFile, getuid(), getgid());
      if ((fi = fopen(pas->pw_name, "r")))
      {
        while ((n = fread(buf, 1, sizeof(buf), fi)) > 0)
          write(fd, buf, n);
      }
      close(fd);
      EditFile(caller, edFile);
      repFileName = edFile;
    }
    else
    {
      errx(1, "unable to create %s\n", edFile);
    }
  }
    option = REPLACE;
    // fall through.
  case REPLACE:
  {
    char buf[1024];
    char path[1024];
    int fd;
    int n;

    //  If there is a replacement file, obtain a secure descriptor to it.
    if (repFileName)
    {
      repFd = GetReplaceStream(caller, repFileName);
      if (repFileName == edFile)
        remove(edFile);
      if (repFd < 0)
      {
        errx(1, "unable to read replacement file\n");
      }
    }

    snprintf(path, sizeof(path), "%s.new", pas->pw_name);
    if ((fd = open(path, O_CREAT | O_TRUNC | O_EXCL | O_APPEND | O_WRONLY,
                   0600)) >= 0)
    {
      while ((n = read(repFd, buf, sizeof(buf))) > 0)
      {
        write(fd, buf, n);
      }
      close(fd);
      rename(path, pas->pw_name);
    }
    else
    {
      fprintf(stderr, "unable to create %s/%s: %s\n", CDir, path,
              strerror(errno));
    }
    close(repFd);
  }
  break;
  case DELETE:
    remove(pas->pw_name);
    break;
  case NONE:
  default:
    break;
  }

  //   Bump notification file.  Handle window where crond picks file
  //   up before we can write our entry out.

  if (option == REPLACE || option == DELETE)
  {
    FILE *fo;
    struct stat st;

    while ((fo = fopen(CRONUPDATE, "a")))
    {
      fprintf(fo, "%s\n", pas->pw_name);
      fflush(fo);
      if (fstat(fileno(fo), &st) != 0 || st.st_nlink != 0)
      {
        fclose(fo);
        break;
      }
      fclose(fo);
    }
    if (fo == NULL)
    {
      fprintf(stderr, "unable to append to %s/%s\n", CDir, CRONUPDATE);
    }
  }
  (volatile void)exit(0);
  // not reached.
}

int GetReplaceStream(const char *user, const char *file)
{
  int filedes[2];
  int pid;
  int fd;
  int n;
  char buf[1024];

  if (pipe(filedes) < 0)
  {
    perror("pipe");
    return (-1);
  }
  if ((pid = fork()) < 0)
  {
    perror("fork");
    return (-1);
  }
  if (pid > 0)
  {
    //  PARENT
    close(filedes[1]);
    if (read(filedes[0], buf, 1) != 1)
    {
      close(filedes[0]);
      filedes[0] = -1;
    }
    return (filedes[0]);
  }

  //  CHILD

  close(filedes[0]);

  if (ChangeUser(user, 0) < 0)
    exit(0);

  if (strcmp("-", file) == 0)
    fd = 0;
  else
    fd = open(file, O_RDONLY);

  if (fd < 0)
    errx(0, "unable to open %s\n", file);

  buf[0] = 0;
  write(filedes[1], buf, 1);
  while ((n = read(fd, buf, sizeof(buf))) > 0)
  {
    write(filedes[1], buf, n);
  }
  exit(0);
}

void EditFile(const char *user, const char *file)
{
  int pid;

  if ((pid = fork()) == 0)
  {
    //  CHILD - change user and run editor
    const char *ptr;
    char visual[1024];

    if (ChangeUser(user, 1) < 0)
      exit(0);
    if ((ptr = getenv("VISUAL")) == NULL || strlen(ptr) > 256)
      ptr = PATH_VI;

    snprintf(visual, sizeof(visual), "%s %s", ptr, file);
    execl("/bin/sh", "/bin/sh", "-c", visual, NULL);
    perror("exec");
    exit(0);
  }
  if (pid < 0)
  {
    //  PARENT - failure
    perror("fork");
    exit(1);
  }
  waitpid(pid, NULL, 0);
}
