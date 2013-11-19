#include "proxysql.h"

/*
 * need to add several
 * #ifdef DEBUG_mysql_conn
 * for debugging
*/


//gboolean reconnect_server_on_shut_fd(mysql_session_t *sess, mysql_cp_entry_t **myc) {
gboolean reconnect_server_on_shut_fd(mysql_session_t *sess) {
	if (sess->server_myds==NULL) { // the backend is not initialized, return
		return TRUE;
	}
	if ( sess->server_myds->active==TRUE ) {
		return TRUE;
    }
	if (
		( sess->server_myds->active==FALSE ) // connection is not active
		&& (glovars.mysql_auto_reconnect_enabled==FALSE) // auto-reconnect is globally disabled
	) {
#ifdef DEBUG_mysql_conn
		debug_print("%s\n", "mysql_auto_reconnect_enabled is OFF");
#endif
		return FALSE;
	}

#ifdef DEBUG_mysql_conn
	debug_print("%s\n", "Entering reconnect_server_on_shut_fd");
#endif
	mysql_cp_entry_t *mycpe=NULL;
	if (
		( sess->server_myds->active==FALSE ) // connection is not active
		&& ( sess->mysql_server_reconnect==TRUE ) // the session is configured to reconnect
		&& ( sess->server_bytes_at_cmd.bytes_sent==sess->server_myds->bytes_info.bytes_sent) // no bytes sent so far
		&& ( sess->server_bytes_at_cmd.bytes_recv==sess->server_myds->bytes_info.bytes_recv) // no bytes recv so far
	) {
		int tries=10;
		while (tries--) {
#ifdef DEBUG_mysql_conn
	debug_print("%s\n", "Trying to reconnect...");
#endif
			if (sess->server_mycpe) {
//				shutdown(sess->server_mycpe->conn->net.fd, SHUT_RDWR);
//				close(sess->server_mycpe->conn->net.fd);
#ifdef DEBUG_mysql_conn
	debug_print("Closing mysql connection on fd %d\n", sess->server_mycpe->conn->net.fd);
#endif
//				sess->server_mycpe->conn->net.vio=0;
				if (sess->server_mycpe->conn->net.fd==0) {
					// for some unknown reason, conn->net.fd may be 0. This seems a bug!
					sess->server_mycpe->conn->net.vio=0;
				}
				mysql_close(sess->server_mycpe->conn);  // drop the connection
				if (sess->send_to_slave==FALSE) {
					free(sess->master_mycpe);
					sess->master_mycpe=NULL;
				} else {
					free(sess->slave_mycpe);
					sess->slave_mycpe=NULL;
				}
				sess->server_mycpe=NULL;
			}
			mycpe=mysql_connpool_get_connection(gloconnpool, sess->server_ptr->address, sess->mysql_username, sess->mysql_password, sess->mysql_schema_cur, sess->server_ptr->port);   // re-establish a new connection	
			// try it
			if (mycpe) {
#ifdef DEBUG_mysql_conn
	debug_print("Obtained mysql connection on fd %d\n", mycpe->conn->net.fd);
#endif
				if (mysql_query(mycpe->conn,"SELECT 1")) {
#ifdef DEBUG_mysql_conn
			debug_print("SELECT 1 failed on fd %d\n", mycpe->conn->net.fd);
#endif
					//shutdown(mycpe->conn->net.fd, SHUT_RDWR);
					mysql_close(mycpe->conn);
					//close(mycpe->conn->net.fd);
					free(mycpe);
					mycpe=NULL;
					continue;
				}
				MYSQL_RES *result = mysql_store_result(mycpe->conn);
				mysql_free_result(result);
				tries=0;
				continue;
			}
		}
		if (mycpe==NULL) {
#ifdef DEBUG_mysql_conn
	debug_print("%s\n", "Unable to return a connection. Reconnection FAILED");
#endif
			// maybe is better if the calling function sends the error to the client. The follow 3 lines should be moved out of here
			pkt *hs;
#ifdef PKTALLOC
#ifdef DEBUG_pktalloc
			debug_print("%s\n", "mypkt_alloc");
#endif
			hs=mypkt_alloc(sess);
#else
			hs=g_slice_alloc(sizeof(pkt));
#endif
			create_err_packet(hs, 2, 1045, "#28000Access denied for user");
			write_one_pkt_to_net(sess->client_myds,hs);
			return FALSE;
		} else {
			//*myc=mycpe;
			if (sess->send_to_slave==FALSE) {
				if (sess->master_myds) {
					sess->master_fd=mycpe->conn->net.fd;
					sess->master_myds->fd=sess->master_fd;
					sess->master_mycpe=mycpe;
					sess->server_fd=sess->master_fd;
					sess->server_myds=sess->master_myds;
					sess->server_mycpe=sess->master_mycpe;
				}
			} else {
				sess->slave_fd=mycpe->conn->net.fd;
				sess->slave_myds->fd=sess->slave_fd;
				sess->slave_mycpe=mycpe;
				sess->server_fd=sess->slave_fd;
				sess->server_myds=sess->slave_myds;
				sess->server_mycpe=sess->slave_mycpe;
			}
			sess->fds[1].fd=sess->server_myds->fd;
			sess->server_myds->active=TRUE;
		}
	}
	return TRUE;
}


myConnPools *mysql_connpool_init() {
	myConnPools *cp;
	if ((cp=malloc(sizeof(myConnPools)))==NULL) { exit(EXIT_FAILURE); }
	pthread_mutex_init(&cp->mutex, NULL);
	cp->connpools=g_ptr_array_new();
	cp->enabled=TRUE;	// enabled by default
#ifdef DEBUG_mysql_conn
	debug_print("%s\n", "Main connection pool struct created");
#endif
	return cp;
}


mysql_connpool *mysql_connpool_find(myConnPools *cp, const char *hostname, const char *username, const char *password, const char *db, unsigned int port) {
//	NOTE: the calling function must lock the mutex
#ifdef DEBUG_mysql_conn
	debug_print("Searching a connection for %s %s %s %s %d\n", hostname, username, password, db, port);
#endif
	guint l;
	for (l=0; l<cp->connpools->len; l++) {
		mysql_connpool *mcp=g_ptr_array_index(cp->connpools,l);
		if (
			(strcmp(hostname,mcp->hostname)==0) &&
			(strcmp(username,mcp->username)==0) &&
			(strcmp(password,mcp->password)==0) &&
			(strcmp(db,mcp->db)==0) &&
			(port==mcp->port)
		) {	// we found the matching hostname/username/password/port
#ifdef DEBUG_mysql_conn
			debug_print("Found connection for %s %s %s %s %d\n", hostname, username, password, db, port);
#endif
			return mcp;
		}// else {
		//	return NULL;
		//}
	}
#ifdef DEBUG_mysql_conn
	debug_print("NOT found connection for %s %s %s %s %d\n", hostname, username, password, db, port);
#endif
	return NULL; // no match found
}

mysql_connpool *mysql_connpool_create(myConnPools *cp, const char *hostname, const char *username, const char *password, const char *db, unsigned int port) {
//	NOTE: the calling function must lock the mutex
	mysql_connpool *mcp;
	if ((mcp=malloc(sizeof(mysql_connpool)))==NULL) { exit(EXIT_FAILURE); }
	mcp->hostname=strdup(hostname);
	mcp->username=strdup(username);
	mcp->password=strdup(password);
	mcp->db=strdup(db);
	mcp->port=port;
//	mcp->used_conns=g_ptr_array_new();
	mcp->free_conns=g_ptr_array_new();
#ifdef DEBUG_mysql_conn
	debug_print("Created connection pool for %s %s %s %s %d\n", hostname, username, password, db, port);
#endif
	return mcp;
}

mysql_cp_entry_t *mysql_connpool_get_connection(myConnPools *cp, const char *hostname, const char *username, const char *password, const char *db, unsigned int port) {
//	NOTE: this function locks the mutex
#ifdef DEBUG_mysql_conn
	debug_print("Getting a connection for %s %s %s %s %d\n", hostname, username, password, db, port);
#endif
	guint l;
	mysql_cp_entry_t *mycpe=NULL;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	unsigned long long curr_time=(tv.tv_sec) * 1000000 + (tv.tv_usec);
	if (cp->enabled==TRUE) {
		pthread_mutex_lock(&cp->mutex);
		mysql_connpool *mcp=mysql_connpool_find(cp, hostname, username, password, db, port);
		if (mcp==NULL) {
			mcp=mysql_connpool_create(cp, hostname, username, password, db, port);
			g_ptr_array_add(cp->connpools,mcp);
		}
		while (mcp->free_conns->len) {
			mysql_cp_entry_t *mc=g_ptr_array_index(mcp->free_conns,0);	// get the first connection
			g_ptr_array_remove_index_fast(mcp->free_conns,0);	// remove it
			if (mc->expire <= curr_time) {	// if the connection is expired ...
#ifdef DEBUG_mysql_conn
				debug_print("Closing expired connection for %s %s %s %s %d\n", hostname, username, password, db, port);
#endif
				mysql_close(mc->conn);	// ... close it
				free(mc);
			} else { // found a potential good connection
				mc->expire=curr_time + glovars.mysql_wait_timeout;
			// mysql_ping seems bugged, as it re-establishes a connection, disabling
//			if (mysql_ping(mc->conn)!=0) { // the conn is dead
//				fprintf(stderr, "%s\n", mysql_error(mc->conn));
//				mysql_close(mc->conn);
//				free(mc);
//			} else { // the connection is really good
//				g_ptr_array_add(mcp->used_conns,mc);	
				mycpe=mc;
				break;
//			}
			}
		}
		pthread_mutex_unlock(&cp->mutex); // free the lock now!
	}	/* (cp->enabled==TRUE) */
	if (mycpe==NULL) {
	// if we reached here it means we couldn't find any connection
	// therefore we need to create one
		MYSQL *mysql_con = mysql_init(NULL);
//		my_bool  my_true = 1;
//		mysql_options(mysql_con, MYSQL_OPT_RECONNECT, &my_true);
		if (mysql_real_connect(mysql_con, hostname, username, password, db, port, NULL, 0) == NULL) {
			// we aren't able to connect
			fprintf(stderr, "%s\n", mysql_error(mysql_con));
			// we don't abort because the called may decide to connect to another slave if available
		} else {
			if ((mycpe=malloc(sizeof(mysql_cp_entry_t)))==NULL) { exit(1); }
#ifdef DEBUG_mysql_conn
			debug_print("Created new connection for %s %s %s %s %d\n", hostname, username, password, db, port);
#endif
			mycpe->conn=mysql_con;
			mycpe->expire = curr_time + glovars.mysql_wait_timeout;
/* NEW STUFF
	While testing auto-reconnect features with hundreds of connections being constantly killed,
	I noticed that connections stay in CLOSE_WAIT state for very long time
	It should be related to the fact that mysql_real_connect() call setsockopt() with SO_KEEPALIVE
	By default a keepalive is sent every tcp_keepalive_time seconds (defaults to 2 hours).
	We are not changing it to 10 minutes, hardcoded for now, configurable later on.
	We also need to add error control.
*/
			int tcp_keepalive_time=600;
			setsockopt(mysql_con->net.fd, SOL_TCP,  TCP_KEEPIDLE, (char *)&tcp_keepalive_time, sizeof(tcp_keepalive_time));
			int arg_on=1;
			ioctl(mysql_con->net.fd, FIONBIO, (char *)&arg_on);
//			pthread_mutex_lock(&cp->mutex);	// acquire the lock only to add the new created connection
//			mcp=mysql_cp_entry_tpool_find(cp, hostname, username, password, db, port);
//			g_ptr_array_add(mcp->used_conns,mycpe);
//			pthread_mutex_unlock(&cp->mutex);
		}
	}
	return mycpe;
}

void mysql_connpool_detach_connection(myConnPools *cp, mysql_cp_entry_t *mc) {
	if (cp->enabled==FALSE) {
		mysql_close(mc->conn);
		free(mc);
		return;
	}
	MYSQL *mysql_con;
	mysql_con=mc->conn;
#ifdef DEBUG_mysql_conn
	debug_print("Detaching connection for %s %s %s %s %d\n", mysql_con->host, mysql_con->user, mysql_con->passwd, mysql_con->db, mysql_con->port);
#endif
	pthread_mutex_lock(&cp->mutex);
	mysql_connpool *mcp=mysql_connpool_find(cp, mysql_con->host, mysql_con->user, mysql_con->passwd, mysql_con->db, mysql_con->port);
	// we may not find the connection if an INITDB was issued, thus changing db
	// we temporary (?) remove the used_conns array
	// maybe a global implementation is better
	if (mcp) {
		//g_ptr_array_remove_fast(mcp->used_conns,mc);
		struct timeval tv;
		gettimeofday(&tv, NULL);
		unsigned long long curr_time=(tv.tv_sec) * 1000000 + (tv.tv_usec);
		if (mc->expire <= curr_time) {	// if the connection is expired ...
#ifdef DEBUG_mysql_conn
			debug_print("Closing expired connection for %s %s %s %s %d\n", mysql_con->host, mysql_con->user, mysql_con->passwd, mysql_con->db, mysql_con->port);
#endif
			mysql_close(mc->conn);	// ... close it
			free(mc);
		} else {
			g_ptr_array_add(mcp->free_conns,mc);
		}
		
	}
	pthread_mutex_unlock(&cp->mutex);
}


void * mysql_connpool_purge_thread() {
	GPtrArray *conns=g_ptr_array_new();
	while(1) {
		usleep(1000000);
		if (gloconnpool->enabled==FALSE) {
			continue;
		}
		unsigned int i;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		unsigned long long curr_time=(tv.tv_sec) * 1000000 + (tv.tv_usec);
		pthread_mutex_lock(&gloconnpool->mutex);
		for (i=0; i<gloconnpool->connpools->len; i++) {
			mysql_connpool *mcp=g_ptr_array_index(gloconnpool->connpools,i);
			int l=mcp->free_conns->len;
			while (l>0) {
				l--;
				mysql_cp_entry_t *mc=g_ptr_array_index(mcp->free_conns,l);
				if (mc->expire <= curr_time) {
					g_ptr_array_add(conns,mc);
					g_ptr_array_remove_index_fast(mcp->free_conns,l);
					l--;
				}
			}
		}
		pthread_mutex_unlock(&gloconnpool->mutex);
		for (i=0; i<conns->len; i++) {
			mysql_cp_entry_t *mc=g_ptr_array_index(conns,0);
			g_ptr_array_remove_index_fast(conns,0);
			mysql_close(mc->conn);
			free(mc);
		}
	}
}
