#include "proxysql.h"


/*
  set ret to the default values for debug , and exc for all the exception 
  for debug all but exceptions  : ret=1 , exc=0
  for debug none but exceptions : ret=0 , exc=1
*/
/*
int __debug_code(const char *func) {
	int ret=1;
	int exc=0;
	int s=strlen(func);
	if (!strncmp(func,"buffer2array",s)) return exc;
	if (!strncmp(func,"array2buffer",s)) return exc;
	if (!strncmp(func,"write_to_net",s)) return exc;
	if (!strncmp(func,"read_from_net",s)) return exc;
	return ret;
}
*/





void * dump_timers() {
	int i;
	char **timers_name=malloc(sizeof(char *)*TOTAL_TIMERS);
	timers_name[0]="array2buffer ";
	timers_name[1]="buffer2array ";
	timers_name[2]="read_from_net";
	timers_name[3]="write_to_net ";
	timers_name[4]="processdata  ";
	timers_name[5]="find_queue   ";
	timers_name[6]="find_queue   ";
	while (1) {
		sleep(glovars.print_statistics_interval);
		if (glovars.verbose < 10 || glovars.enable_timers==0) continue;
		for (i=0;i<TOTAL_TIMERS;i++)
			fprintf(stderr,"TIMER %d - %s : total = %lld\n", i, timers_name[i], glotimers[i]);
		if (glovars.mysql_query_cache_enabled==TRUE) {
			int QC_entries=0;
			for (i=0; i<QC.size; i++) {
				QC_entries+=QC.fdb_hashes[i]->ptrArray->len;
			}
			fprintf(stderr,"QC entries: %d , SET: %llu , GET: %llu(OK)/%llu(Total) , Purged: %llu\n", QC_entries, QC.cntSet, QC.cntGetOK, QC.cntGet, QC.cntPurge);
		}
	}
}



void crash_handler(int sig) {
	void *arr[20];
	size_t s;

	s = backtrace(arr, 20);

	fprintf(stderr, "Error: signal %d:\n", sig);
	backtrace_symbols_fd(arr, s, STDERR_FILENO);
	exit(EXIT_FAILURE);
}

inline void start_timer(timer *tim, enum enum_timer t) {
	if (!glovars.enable_timers) return;
	struct timeval  tv;
	gettimeofday(&tv, NULL);
	tim[t].begin=(tv.tv_sec) * 1000000 + (tv.tv_usec);
};

inline void stop_timer(timer *tim, enum enum_timer t) {
	if (!glovars.enable_timers) return;
	struct timeval  tv;
	gettimeofday(&tv, NULL);
	unsigned long long l=(tv.tv_sec) * 1000000 + (tv.tv_usec) - tim[t].begin;
	tim[t].total+=l;
	__sync_fetch_and_add(&glotimers[t],l);
};
