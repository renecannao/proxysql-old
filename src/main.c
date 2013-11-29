
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
  { "config", 'c', 0, G_OPTION_ARG_FILENAME, &config_file, "Configuration file", NULL },
  { NULL }
};


pthread_mutex_t conn_mutex;
int conn_cnt=0;



int listen_tcp_fd;
int listen_tcp_admin_fd;
int listen_unix_fd;


void send_auth_pkt(mysql_session_t *sess) {
	pkt *hs;
	hs=mypkt_alloc(sess);
	create_handshake_packet(hs,sess->scramble_buf);
	g_ptr_array_add(sess->client_myds->output.pkts, hs);
}

void mysql_session_init2(mysql_session_t *sess) {
	sess->client_myds=mysql_data_stream_init(sess->client_fd, sess);
	sess->client_myds->fd=sess->client_fd;
	sess->fds[0].fd=sess->client_myds->fd;
	sess->fds[0].events=POLLIN|POLLOUT;
	sess->nfds=1;
	sess->query_to_cache=FALSE;
	sess->client_command=COM_END;	// always reset this
	sess->send_to_slave=FALSE;
}


void *mysql_thread(void *arg) {
	int admin=0;
	mysql_thread_init();

	int thread_id=*(int *)arg;
	fprintf(stderr, "thread_id = %d\n", thread_id);
	if (thread_id==glovars.mysql_threads) {
		fprintf(stderr, "started admin thread\n");
		admin=1;
	}
	GPtrArray *sessions=g_ptr_array_new();
	int i, nfds, r;
	mysql_session_t *sess=NULL;
	struct pollfd fds[1000];
	if (admin==0) { fds[0].fd=listen_tcp_fd; }
		else { fds[0].fd=listen_tcp_admin_fd; }
	if (admin==0) { fds[1].fd=listen_unix_fd; }
	while(glovars.shutdown==0) {
		if (admin==0) {nfds=2;} else {nfds=1;}
		fds[0].events=POLLIN;
		fds[0].revents=0;
		if (admin==0) {
			fds[1].events=POLLIN;
			fds[1].revents=0;
		}
		for (i=0; i < sessions->len; i++) {
			sess=g_ptr_array_index(sessions, i);
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
				conn_poll(sess);
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
		if (admin==0) {nfds=2;} else {nfds=1;}
		for (i=0; i < sessions->len; i++) {
			sess=g_ptr_array_index(sessions, i);
			if (sess->healthy==1) {	
				int j;
				for (j=0; j<sess->nfds; j++) {
					if (sess->fds[j].events) {
						memcpy(&sess->fds[j],&fds[nfds],sizeof(struct pollfd));
						nfds++;
					}
				}
			check_fds_errors(sess);
			connection_handler_mysql(sess);
			}
		}
		for (i=0; i < sessions->len; i++) {
			sess=g_ptr_array_index(sessions, i);
			if (sess->healthy==0) {
				g_ptr_array_remove_index_fast(sessions,i);
				i--;
				free(sess);
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
				mysql_session_t *ses=NULL;
				ses=malloc(sizeof(mysql_session_t));
				if (ses==NULL) { exit(EXIT_FAILURE); }
				ses->client_fd = c; 
				int arg_on=1;
				setsockopt(ses->client_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &arg_on, sizeof(int));
				mysql_session_init(ses);
				mysql_session_init2(ses);
				if (admin==1) {
					ses->admin=1;
				}
				send_auth_pkt(ses);
				g_ptr_array_add(sessions,ses);
//				fprintf("thread %d created session %p\n" , thread_id , sess);
			}
		}
		if (fds[1].revents==POLLIN) {
			int c=accept(listen_unix_fd, NULL, NULL);
			if (c>0) {
				mysql_session_t *ses=NULL;
				ses=malloc(sizeof(mysql_session_t));
				if (ses==NULL) { exit(EXIT_FAILURE); }
				ses->client_fd = c; 
				mysql_session_init(ses);
				mysql_session_init2(ses);
				send_auth_pkt(ses);			
				g_ptr_array_add(sessions,ses);
			}
		}
	}
	return;
}

// thread that handles connection
int connection_handler_mysql(mysql_session_t *sess) {

	sess->status=CONNECTION_READING_CLIENT|CONNECTION_WRITING_CLIENT|CONNECTION_READING_SERVER|CONNECTION_WRITING_SERVER;
	if (sess->healthy) {
	

		if (sess->client_myds->active==FALSE) { // || sess->server_myds->active==FALSE) {
			mysql_session_close(sess); return -1;
		}

		if (sync_net(sess,0)==FALSE) {
			mysql_session_close(sess); return -1;
		}

		buffer2array_2(sess);

		if (sess->client_myds->pkts_sent==1 && sess->client_myds->pkts_recv==1) {
			pkt *hs=NULL;
			hs=g_ptr_array_remove_index(sess->client_myds->input.pkts, 0);
			sess->ret=check_client_authentication_packet(hs,sess);
			g_slice_free1(hs->length, hs->data);

			if (sess->ret) {
				create_err_packet(hs, 2, 1045, "#28000Access denied for user");
//				authenticate_mysql_client_send_ERR(sess, 1045, "#28000Access denied for user");
			} else {
				create_ok_packet(hs,2);
				if (sess->mysql_schema_cur==NULL) {
			 		sess->mysql_schema_cur=strdup(glovars.mysql_default_schema);
				}
			}
			g_ptr_array_add(sess->client_myds->output.pkts, hs);
		}
		// set status to all possible . Remove options during processing
//		sess->status=CONNECTION_READING_CLIENT|CONNECTION_WRITING_CLIENT|CONNECTION_READING_SERVER|CONNECTION_WRITING_SERVER;

	
		start_timer(sess->timers,TIMER_processdata);
		if (process_mysql_client_pkts(sess)==-1) {
			// we got a COM_QUIT
			mysql_session_close(sess);
			return -1;
		}
		process_mysql_server_pkts(sess);
		stop_timer(sess->timers,TIMER_processdata);

		array2buffer_2(sess);


		if ( (sess->server_myds==NULL) || (sess->last_server_poll_fd==sess->server_myds->fd)) {
// this optimization is possible only if a connection to the backend didn't break in the meantime,
// or a master/slave didn't occur,
// or we never connected to a backend
			if (sync_net(sess,1)==FALSE) {
				mysql_session_close(sess); return -1;
			}
		}
		if (sess->client_myds->pkts_sent==2 && sess->client_myds->pkts_recv==1) {
			if (sess->mysql_schema_cur==NULL) {
				mysql_session_close(sess); return -1;
			}
		}
		return 0;
	} else {
	mysql_session_close(sess);
	return -1;
	}
}




int main(int argc, char **argv) {

	g_thread_init(NULL);
	// parse all the arguments and the config file
	main_opts(entries, &argc, &argv, config_file);
	pthread_mutex_init(&conn_mutex, NULL);

	//  command line options take precedences over config file
	if (proxy_admin_port) { glovars.proxy_admin_port=proxy_admin_port; }
	if (proxy_mysql_port) { glovars.proxy_mysql_port=proxy_mysql_port; }
	if (verbose>=0) { glovars.verbose=verbose; }

	if (glovars.proxy_admin_port==glovars.proxy_mysql_port) {
		fprintf(stderr, "Fatal error: proxy_admin_port (%d) matches proxy_mysql_port (%d) . Configure them to use different ports\n", glovars.proxy_admin_port, glovars.proxy_mysql_port);
		exit(EXIT_FAILURE);
	}

	if (glovars.verbose>0) {
		fprintf(stderr, "mysql port %d, admin port %d, config file %s, verbose %d\n", glovars.proxy_mysql_port, glovars.proxy_admin_port, config_file, verbose);
		fprintf(stderr, "Query cache partitions: %d\n", glovars.mysql_query_cache_partitions);
		fprintf(stderr, "MySQL USAGE user: %s, password: %s\n", glovars.mysql_usage_user, glovars.mysql_usage_password);
		fprintf(stderr, "Max query size: %d, Max resultset size: %d\n", glovars.mysql_max_query_size, glovars.mysql_max_resultset_size);
		fprintf(stderr, "verbose level: %d, print_statistics_interval: %d\n", glovars.verbose, glovars.print_statistics_interval);
	}

	// FIXME : this needs to moved elsewhere
	int i;
	for (i=0;i<TOTAL_TIMERS;i++) { glotimers[i]=0; }

	// Set threads attributes . For now only setstacksize is defined
	pthread_attr_t attr;
	set_thread_attr(&attr,glovars.stack_size);


	start_background_threads(&attr);


	listen_tcp_fd=listen_on_port((uint16_t)glovars.proxy_mysql_port);
	listen_tcp_admin_fd=listen_on_port((uint16_t)glovars.proxy_admin_port);
	listen_unix_fd=listen_on_unix(glovars.mysql_socket);
	int arg_on=1, arg_off=0;
	ioctl(listen_tcp_fd, FIONBIO, (char *)&arg_on);
	ioctl(listen_tcp_admin_fd, FIONBIO, (char *)&arg_on);
	ioctl(listen_unix_fd, FIONBIO, (char *)&arg_on);
	struct pollfd fds[2];
	int nfds=2;
	fds[0].fd=listen_tcp_fd;
	fds[1].fd=listen_unix_fd;
	mysql_library_init(0, NULL, NULL);
	// Note: glovars.mysql_threads+1 threads are created. The +1 is for the admin module
	pthread_t *thi=malloc(sizeof(pthread_t)*(glovars.mysql_threads+1));
	int *args=malloc(sizeof(int)*(glovars.mysql_threads+1));
	if (thi==NULL) exit(EXIT_FAILURE);
	// while all other threads are detachable, the mysql connections handlers are not
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	for (i=0; i< glovars.mysql_threads+1; i++) {
		args[i]=i;
		if ( pthread_create(&thi[i], &attr, mysql_thread , &args[i]) != 0 )
			perror("Thread creation");
    }
	// wait for graceful shutdown
	for (i=0; i<glovars.mysql_threads+1; i++) {
		pthread_join(thi[i], NULL);
	}
	free(thi);
	return 0;
}
