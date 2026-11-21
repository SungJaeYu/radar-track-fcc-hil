#ifndef PERF_REPORTER_H
#define PERF_REPORTER_H

// Called only from main, every five seconds; stack scans and logging stay outside locks.
void perf_reporter_log_summary(void);

#endif
