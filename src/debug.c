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
	while (glovars.shutdown==0) {
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
	free(timers_name);
	proxy_error("Shutdown dump_timers\n");
	return;
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

void proxy_debug_func(enum debug_module module, int verbosity, const char *fmt, ...) {
	assert(module<PROXY_DEBUG_UNKNOWN);
	if (gdbg_lvl[module].verbosity < verbosity) return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
};


void proxy_error_func(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);	
};

void init_debug_struct() {
	int i;
	gdbg_lvl=g_malloc0_n(PROXY_DEBUG_UNKNOWN,sizeof(debug_level));
	for (i=0;i<PROXY_DEBUG_UNKNOWN;i++) {
		gdbg_lvl[i].module=i;
		gdbg_lvl[i].verbosity=( gdbg ? INT_MAX : 0 );
		gdbg_lvl[i].name=NULL;
	}
	gdbg_lvl[PROXY_DEBUG_GENERIC].name="debug_generic"; 
	gdbg_lvl[PROXY_DEBUG_NET].name="debug_net";
	gdbg_lvl[PROXY_DEBUG_PKT_ARRAY].name="debug_pkt_array";
	gdbg_lvl[PROXY_DEBUG_POLL].name="debug_poll";
	gdbg_lvl[PROXY_DEBUG_MYSQL_COM].name="debug_mysql_com";
	gdbg_lvl[PROXY_DEBUG_MYSQL_SERVER].name="debug_mysql_server";
	gdbg_lvl[PROXY_DEBUG_MYSQL_CONNECTION].name="debug_mysql_connection";
	gdbg_lvl[PROXY_DEBUG_MYSQL_RW_SPLIT].name="debug_mysql_rw_split";
	gdbg_lvl[PROXY_DEBUG_MYSQL_AUTH].name="debug_mysql_auth";
	gdbg_lvl[PROXY_DEBUG_MEMORY].name="debug_memory";
	gdbg_lvl[PROXY_DEBUG_ADMIN].name="debug_admin";
	gdbg_lvl[PROXY_DEBUG_SQLITE].name="debug_sqlite";
	gdbg_lvl[PROXY_DEBUG_IPC].name="debug_ipc";
	gdbg_lvl[PROXY_DEBUG_QUERY_CACHE].name="debug_query_cache";

	for (i=0;i<PROXY_DEBUG_UNKNOWN;i++) {
		// if this happen, the above table is not populated correctly
		assert(gdbg_lvl[i].name!=NULL);
	}
}	
