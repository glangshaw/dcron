//  CHUSER.C
//
//  Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
//  Copyright 2026 Gary Langshaw (gary.langshaw@gmail.com)
//  May be distributed under the GNU General Public License

#include "chuser.h"

#include "defs.h"

#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

int ChangeUser(const char *user, int dochdir)
{
  struct passwd *pas;

  // Obtain password entry and change privileges

  if ((pas = getpwnam(user)) == NULL)
    return (-1);

  setenv("LOGNAME", pas->pw_name, 1);
  setenv("HOME", pas->pw_dir, 1);
  setenv("SHELL", "/bin/sh", 1);

  //  Change running state to the user in question

  if (initgroups(user, pas->pw_gid) < 0)
    return (-1);

  if (setregid(pas->pw_gid, pas->pw_gid) < 0)
    return (-1);

  if (setreuid(pas->pw_uid, pas->pw_uid) < 0)
    return (-1);

  if (dochdir)
    if (chdir(pas->pw_dir) < 0)
      if (chdir(TMPDIR) < 0)
        return (-1);

  return (pas->pw_uid);
}
