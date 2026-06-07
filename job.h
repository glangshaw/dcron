#ifndef JOBS_H
#define JOBS_H

#include "defs.h"

void RunJob(CronFile *file, CronLine *line);
void EndJob(CronFile *file, CronLine *line);

#endif
