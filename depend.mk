# DO NOT DELETE

bitset.o: bitset.h
chuser.o: chuser.h defs.h
crond.o: database.h defs.h job.h subs.h
crontab.o: chuser.h defs.h subs.h
database.o: database.h bitset.h defs.h job.h subs.h
job.o: job.h defs.h chuser.h subs.h
subs.o: subs.h defs.h
