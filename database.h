#ifndef DATABASE_H
#define DATABASE_H

/*
 * DATABASE.H
 *
 * Copyright 1994-1998 Matthew Dillon (dillon@backplane.com)
 * Copyright 2026 Gary Langshaw (gary.langshaw@gmail.com)
 * May be distributed under the GNU General Public License
 */

#include <time.h>

void CheckUpdates(const char *dpath, const char *user_override);
void SynchronizeDir(const char *dpath, const char *user_override,
                    int initial_scan);
int TestJobs(time_t t1, time_t t2);
void RunJobs(void);
int CheckJobs(void);

#endif /* DATABASE_H */
