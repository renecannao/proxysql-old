#include "proxysql.h"

static void reset(mysql_backend_t *mybe, int force_close) {
	mybe->fd=0;
	if (mybe->ms) {
		// without the IF , this can cause SIGSEGV
		proxy_debug(PROXY_DEBUG_MYSQL_SERVER, 7, "Reset MySQL backend, server %s:%d , fd %d\n", mybe->ms->server_ptr->address, mybe->ms->server_ptr->port, mybe->fd);
		__sync_fetch_and_sub(&mybe->ms->connections_active,1);
	}
	mybe->ms=NULL;
	if (mybe->server_myds) {
		mysql_data_stream_delete(mybe->server_myds);
	}
	mybe->server_myds=NULL;
	if (mybe->server_mycpe) {
		mysql_connpool_detach_connection(&gloconnpool, mybe->server_mycpe, force_close);
	}
	mybe->server_mycpe=NULL;
	memset(&mybe->server_bytes_at_cmd,0,sizeof(bytes_stats));
}

mysql_backend_t *mysql_backend_new() {
	mysql_backend_t *mybe=g_slice_alloc0(sizeof(mysql_backend_t));
	mybe->reset=reset;
	return mybe;
}

void mysql_backend_delete(mysql_backend_t *mybe) {
	g_slice_free1(sizeof(mysql_backend_t),mybe);
}
