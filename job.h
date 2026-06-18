#ifndef JOB_H
#define JOB_H

/*
 * JOB.H
 *
 * Copyright 1994-1998 Matthew Dillon (dillon@backplane.com)
 * Copyright 2026 Gary Langshaw (gary.langshaw@gmail.com)
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

void RunJob(CronFile *file, CronLine *line);
void EndJob(CronFile *file, CronLine *line);

#endif
