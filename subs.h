#ifndef SUBS_H
#define SUBS_H

#include <stdarg.h>

void logn(int level, const char *ctl, ...);
void log9(const char *ctl, ...);
void logfd(int fd, const char *ctl, ...);
void fdprintf(int fd, const char *ctl, ...);
int ChangeUser(const char *user, short dochdir);
void vlog(int level, int fd, const char *ctl, va_list va);
int slog(char *buf, const char *ctl, int nmax, va_list va, short useDate);

#endif
