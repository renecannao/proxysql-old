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
	assert(r==0);
	pthread_t cppt;
	r=pthread_create(&cppt, attr, mysql_connpool_purge_thread , NULL);
	assert(r==0);
}

void init_proxyipc() {
	int i;
	proxyipc.fdIn=g_malloc0_n(glovars.mysql_threads,sizeof(int));
	proxyipc.fdOut=g_malloc0_n(glovars.mysql_threads,sizeof(int));
	proxyipc.queue=g_malloc0_n(glovars.mysql_threads+1,sizeof(GAsyncQueue *));
	// create pipes
	for (i=0; i<glovars.mysql_threads; i++) {
		int fds[2];
		int rc;
		rc=pipe(fds);
		assert(rc==0);
//		if (rc==-1) {
//			perror("pipe");
//			assert(rc==0);
//		}
		proxyipc.fdIn[i]=fds[0];
		proxyipc.fdOut[i]=fds[1];
	}
	// initialize the async queue
	for (i=0; i<glovars.mysql_threads+1; i++) {
		proxyipc.queue[i]=g_async_queue_new();
	}
}
