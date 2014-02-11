
#include "proxysql.h"



static gint timers = 0;
static gint proxy_admin_port = 0;
static gint proxy_mysql_port = 0;
static gchar *config_file="proxysql.cnf";
static gint verbose = -1;

static GOptionEntry entries[] =
{
  { "admin-port", 0, 0, G_OPTION_ARG_INT, &proxy_admin_port, "Administration port", NULL },
  { "mysql-port", 0, 0, G_OPTION_ARG_INT, &proxy_mysql_port, "MySQL proxy port", NULL },
  { "verbose", 'v', 0, G_OPTION_ARG_INT, &verbose, "Verbose level", NULL },
  { "debug", 'd', 0, G_OPTION_ARG_INT, &gdbg, "debug", NULL },
  { "config", 'c', 0, G_OPTION_ARG_FILENAME, &config_file, "Configuration file", NULL },
  { NULL }
};


pthread_mutex_t conn_mutex;
int conn_cnt=0;



int listen_tcp_fd;
int listen_tcp_admin_fd;
int listen_unix_fd;

long monotonic_time() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (((long) ts.tv_sec) * 1000000) + (ts.tv_nsec / 1000);
}


void send_auth_pkt(mysql_session_t *sess) {
	pkt *hs;
	hs=mypkt_alloc(sess);
	create_handshake_packet(hs,sess->scramble_buf);
	g_ptr_array_add(sess->client_myds->output.pkts, hs);
}


void *mysql_thread(void *arg) {
	int admin=0;
	mysql_thread_init();

	int removing_hosts=0;
	long maintenance_ends=0;
	proxy_mysql_thread_t thr;
	thr.thread_id=*(int *)arg;
	thr.free_pkts.stack=NULL;
	thr.free_pkts.blocks=g_ptr_array_new();
	if (thr.thread_id==glovars.mysql_threads) {
		proxy_debug(PROXY_DEBUG_GENERIC, 4, "Started Admin thread_id = %d\n", thr.thread_id);
		admin=1;
	} else {
		proxy_debug(PROXY_DEBUG_GENERIC, 4, "Started MySQL thread_id = %d\n", thr.thread_id);
	}
	thr.sessions=g_ptr_array_new();
//	thr.QC_rules=NULL;
//	thr.QCRver=0;
//	if (admin==0) { // no need for QC rules in
//		reset_QC_rules(thr.QC_rules);
//	}
	int i, nfds, r;
	mysql_session_t *sess=NULL;
	struct pollfd fds[1000]; // FIXME: this must be dynamic
	if (admin==0) { fds[0].fd=listen_tcp_fd; }
		else { fds[0].fd=listen_tcp_admin_fd; }
	if (admin==0) {
		fds[1].fd=listen_unix_fd; // Unix Domain Socket
		fds[2].fd=proxyipc.fdIn[thr.thread_id]; // IPC pipe
	}
	while(glovars.shutdown==0) {
		if (admin==0) {nfds=3;} else {nfds=1;}
		fds[0].events=POLLIN;
		fds[0].revents=0;
		if (admin==0) {
			// Unix Domain Socket
			fds[1].events=POLLIN;
			fds[1].revents=0;
			// IPC pipe
			fds[2].events=POLLIN;
			fds[2].revents=0;
		}
		for (i=0; i < thr.sessions->len; i++) {
			sess=g_ptr_array_index(thr.sessions, i);
			if (sess->healthy==1) {	
				if (sess->admin==0 && sess->server_myds) {
				//ioctl(sess->server_fd, FIONBIO, (char *)&arg_on);
					sess->fds[1].fd=sess->server_myds->fd;
					sess->last_server_poll_fd=sess->server_myds->fd;	
					sess->nfds=2;
				} else {
					sess->nfds=1;
				}
			
				sess->status=CONNECTION_READING_CLIENT|CONNECTION_WRITING_CLIENT|CONNECTION_READING_SERVER|CONNECTION_WRITING_SERVER;
				sess->conn_poll(sess);
				int j;
				for (j=0; j<sess->nfds; j++) {
					if (sess->fds[j].events) {
						sess->fds[j].revents=0;
						memcpy(&fds[nfds],&sess->fds[j],sizeof(struct pollfd));
						nfds++;
					}
				}
			}
		}
		r=poll(fds,nfds,glovars.mysql_poll_timeout);
		if (r == -1 && errno == EINTR)
	        continue;
		
	    if (r == -1) {
	        PANIC("poll()");
		}
		if (admin==0 && fds[2].revents==POLLIN) { // admin threads is calling
			char c;
			int r=read(fds[2].fd,&c,sizeof(char));
			proxy_debug(PROXY_DEBUG_IPC, 4, "Got byte on thr %d from FD %d\n", thr.thread_id, fds[2].fd);
			gchar *admincmd=g_async_queue_pop(proxyipc.queue[thr.thread_id]);
			proxy_debug(PROXY_DEBUG_IPC, 4, "Got command %s on thr %d\n", admincmd, thr.thread_id);
			if (strncmp(admincmd,"REMOVE SERVER",20)==0) {
				
				removing_hosts=1;
				maintenance_ends=monotonic_time()+glovars.mysql_maintenance_timeout*1000;
//				mysql_server *ms=g_async_queue_pop(proxyipc.queue[thr.thread_id]);
//				proxy_debug(PROXY_DEBUG_IPC, 6, "Got %p on thr %d\n", ms, thr.thread_id);
			}
			g_free(admincmd);
		}
		if (admin==0 && removing_hosts==1) {
//				proxy_debug(PROXY_DEBUG_ADMIN, 1, "Received order REMOVE SERVER for server %s:%d\n", ms->address, ms->port);
			int i;
			int j;
			int cnt=0;
			for (i=0; i < thr.sessions->len; i++) {
				sess=g_ptr_array_index(thr.sessions, i);
				cnt+=sess->remove_all_backends_offline_soft(sess);
			}
			if (cnt==0) {
				removing_hosts=0;
				gchar *ack=g_malloc0(20);
				sprintf(ack,"%d",cnt);
				proxy_debug(PROXY_DEBUG_IPC, 4, "Sending ACK from thr %d\n", thr.thread_id);
				g_async_queue_push(proxyipc.queue[glovars.mysql_threads],ack);
			} else {
				long ct=monotonic_time();
				if (ct > maintenance_ends) {
					// drop all connections that aren't switched yet
					int i;
					int j;
					int t=0;
					for (i=0; i < thr.sessions->len; i++) {
						int c=0;
						sess=g_ptr_array_index(thr.sessions, i);
						c=sess->remove_all_backends_offline_soft(sess);
						/*
						for (j=0; j<glovars.mysql_hostgroups; j++) {
							mysql_backend_t *mybe=g_ptr_array_index(sess->mybes,j);
							if (mybe->server_ptr!=NULL) {
								if (mybe->server_ptr->status==MYSQL_SERVER_STATUS_OFFLINE_SOFT) {
									c++;
								}
							}
						}
						*/
						if (c) {
							t+=c;
							sess->force_close_backends=1;
							sess->close(sess);
						}
					}
					removing_hosts=0;
					gchar *ack=g_malloc0(20);
					sprintf(ack,"%d",t);
					proxy_debug(PROXY_DEBUG_IPC, 4, "Sending ACK from thr %d\n", thr.thread_id);
					g_async_queue_push(proxyipc.queue[glovars.mysql_threads],ack);
				}
			}
		}
		if (admin==0) {nfds=3;} else {nfds=1;}
		for (i=0; i < thr.sessions->len; i++) {
			sess=g_ptr_array_index(thr.sessions, i);
			if (sess->healthy==1) {	
				int j;
				for (j=0; j<sess->nfds; j++) {
					if (sess->fds[j].events) {
						memcpy(&sess->fds[j],&fds[nfds],sizeof(struct pollfd));
						nfds++;
					}
				}
			sess->check_fds_errors(sess);
			sess->handler(sess);
			}
		}
		for (i=0; i < thr.sessions->len; i++) {
			sess=g_ptr_array_index(thr.sessions, i);
			if (sess->healthy==0) {
				g_ptr_array_remove_index_fast(thr.sessions,i);
				i--;
				mysql_session_delete(sess);
			}
		}
		if (fds[0].revents==POLLIN) {
			int c;
			if (admin==0) {
				c=accept(listen_tcp_fd, NULL, NULL);
			} else {
				c=accept(listen_tcp_admin_fd, NULL, NULL);
			}
			if (c>0) {
				int arg_on=1;
				setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (char *) &arg_on, sizeof(int));
				mysql_session_t *ses=mysql_session_new(&thr, c);
				if (admin==1) {
					ses->admin=1;
				}
				send_auth_pkt(ses);
				g_ptr_array_add(thr.sessions,ses);
			}
		}
		if (admin==0 && fds[1].revents==POLLIN) {
			int c=accept(listen_unix_fd, NULL, NULL);
			if (c>0) {
				mysql_session_t *ses=mysql_session_new(&thr, c);
				send_auth_pkt(ses);			
				g_ptr_array_add(thr.sessions,ses);
			}
		}
	}
	while (thr.free_pkts.blocks->len) {
		void *p=g_ptr_array_remove_index_fast(thr.free_pkts.blocks, 0);
		free(p);
    }
    g_ptr_array_free(thr.free_pkts.blocks,TRUE);

	return;
}




int main(int argc, char **argv) {
	gdbg=0;
	int i;
	g_thread_init(NULL);


	// parse all the arguments and the config file
	main_opts(entries, &argc, &argv, config_file);

	admin_init_sqlite3();

	if (glovars.merge_configfile_db==1) {
		sqlite3_flush_users_mem_to_db(0,1);
		sqlite3_flush_debug_levels_mem_to_db(0);
	}
	// copying back and forth should merge the data
	sqlite3_flush_debug_levels_db_to_mem();
	sqlite3_flush_users_db_to_mem();
	sqlite3_flush_query_rules_db_to_mem();


	sqlite3_flush_servers_mem_to_db(0);
	sqlite3_flush_servers_db_to_mem(1);

	pthread_mutex_init(&conn_mutex, NULL);

	//  command line options take precedences over config file
	if (proxy_admin_port) { glovars.proxy_admin_port=proxy_admin_port; }
	if (proxy_mysql_port) { glovars.proxy_mysql_port=proxy_mysql_port; }
	if (verbose>=0) { glovars.verbose=verbose; }

	if (glovars.proxy_admin_port==glovars.proxy_mysql_port) {
		proxy_error("Fatal error: proxy_admin_port (%d) matches proxy_mysql_port (%d) . Configure them to use different ports\n", glovars.proxy_admin_port, glovars.proxy_mysql_port);
		exit(EXIT_FAILURE);
	}

	if (glovars.verbose>0) {
		proxy_debug(PROXY_DEBUG_GENERIC, 1, "mysql port %d, admin port %d, config file %s, verbose %d\n", glovars.proxy_mysql_port, glovars.proxy_admin_port, config_file, verbose);
		proxy_debug(PROXY_DEBUG_QUERY_CACHE, 1, "Query cache partitions: %d\n", glovars.mysql_query_cache_partitions);
		proxy_debug(PROXY_DEBUG_MYSQL_CONNECTION, 1, "MySQL USAGE user: %s, password: %s\n", glovars.mysql_usage_user, glovars.mysql_usage_password);
		proxy_debug(PROXY_DEBUG_MYSQL_COM, 1, "Max query size: %d, Max resultset size: %d\n", glovars.mysql_max_query_size, glovars.mysql_max_resultset_size);
		//fprintf(stderr, "verbose level: %d, print_statistics_interval: %d\n", glovars.verbose, glovars.print_statistics_interval);
	}

	// FIXME : this needs to moved elsewhere
	for (i=0;i<TOTAL_TIMERS;i++) { glotimers[i]=0; }


	listen_tcp_fd=listen_on_port((uint16_t)glovars.proxy_mysql_port);
	listen_tcp_admin_fd=listen_on_port((uint16_t)glovars.proxy_admin_port);
	listen_unix_fd=listen_on_unix(glovars.mysql_socket);
	//int arg_on=1, arg_off=0;
	//ioctl(listen_tcp_fd, FIONBIO, (char *)&arg_on);
	//ioctl(listen_tcp_admin_fd, FIONBIO, (char *)&arg_on);
	//ioctl(listen_unix_fd, FIONBIO, (char *)&arg_on);
	ioctl_FIONBIO(listen_tcp_fd, 1);
	ioctl_FIONBIO(listen_tcp_admin_fd, 1);
	ioctl_FIONBIO(listen_unix_fd, 1);
	struct pollfd fds[2];
	int nfds=2;
	fds[0].fd=listen_tcp_fd;
	fds[1].fd=listen_unix_fd;
	mysql_library_init(0, NULL, NULL);




	// Set threads attributes . For now only setstacksize is defined
	pthread_attr_t attr;
	set_thread_attr(&attr,glovars.stack_size);

//	start background threads:
//	- mysql QC purger ( purgeHash_thread )
//	- timer dumper ( dump_timers )
//	- mysql connection pool purger ( mysql_connpool_purge_thread )
	start_background_threads(&attr);


	init_proxyipc();

	// Note: glovars.mysql_threads+1 threads are created. The +1 is for the admin module
	pthread_t *thi=g_malloc0(sizeof(pthread_t)*(glovars.mysql_threads+1));
	int *args=g_malloc0(sizeof(int)*(glovars.mysql_threads+1));

	// while all other threads are detachable, the mysql connections handlers are not
//	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	for (i=0; i< glovars.mysql_threads+1; i++) {
		args[i]=i;
		int rc;
		rc=pthread_create(&thi[i], &attr, mysql_thread , &args[i]);
		assert(rc==0);
	}
	// wait for graceful shutdown
	for (i=0; i<glovars.mysql_threads+1; i++) {
		pthread_join(thi[i], NULL);
	}
	g_free(thi);
	g_free(args);
	pthread_join(thread_dt, NULL);
	pthread_join(thread_cppt, NULL);
	pthread_join(thread_qct, NULL);
	return 0;
}
