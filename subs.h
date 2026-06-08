#ifndef SUBS_H
#define SUBS_H

#include <stdarg.h>

void logn(int level, const char *ctl, ...);
void logfd(int fd, const char *ctl, ...);
void fdprintf(int fd, const char *ctl, ...);
int ChangeUser(const char *user, short dochdir);

#endif
