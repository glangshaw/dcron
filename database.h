#ifndef DATABASE_H
#define DATABASE_H

void CheckUpdates(const char *dpath, const char *user_override);
void SynchronizeDir(const char *dpath, const char *user_override, int initial_scan);
int TestJobs(time_t t1, time_t t2);
void RunJobs(void);
int CheckJobs(void);

#endif
