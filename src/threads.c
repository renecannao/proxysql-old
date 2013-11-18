#include "proxysql.h"
void set_thread_attr(pthread_attr_t *attr, size_t stacksize) {
	pthread_attr_init(attr);
	pthread_attr_setdetachstate(attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize (attr, stacksize);
}

void start_background_threads(pthread_attr_t *attr) {
	int r;

	if (glovars.mysql_query_cache_enabled==TRUE) {
		fdb_hashes_new(&QC,glovars.mysql_query_cache_partitions,glovars.mysql_query_cache_default_timeout);
 		pthread_t qct;
		pthread_create(&qct, NULL, purgeHash_thread, &QC);
	}
	pthread_t dt;
	r=pthread_create(&dt, attr, dump_timers , NULL);
	if (r) {
		exit(EXIT_FAILURE);
	}
	pthread_t cppt;
	r=pthread_create(&cppt, attr, mysql_connpool_purge_thread , NULL);
	if (r) {
		exit(EXIT_FAILURE);
	}	
}
