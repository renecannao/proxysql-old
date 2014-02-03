#include "proxysql.h"



void crash_handler(int sig) {
	void *arr[20];
	size_t s;

	s = backtrace(arr, 20);

	fprintf(stderr, "Error: signal %d:\n", sig);
	backtrace_symbols_fd(arr, s, STDERR_FILENO);
	exit(EXIT_FAILURE);
}

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
