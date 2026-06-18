#ifndef SUBS_H
#define SUBS_H

/*
 * SUBS.H
 *
 * Copyright 1994-1998 Matthew Dillon (dillon@backplane.com)
 * Copyright 2026 Gary Langshaw (gary.langshaw@gmail.com)
 * May be distributed under the GNU General Public License
 */

#include <stdarg.h>

void logn(int level, const char *ctl, ...);
void logfd(int fd, const char *ctl, ...);
void fdprintf(int fd, const char *ctl, ...);
int ChangeUser(const char *user, short dochdir);

#endif
