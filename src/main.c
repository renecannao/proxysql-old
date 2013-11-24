
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

GAsyncQueue *conn_queue;

mysql_session_t *ses=NULL;

// thread that handles connection
void * connection_handler_mysql(void *arg) {

	// the pointer to the new session is coming from the argument passed to the thread
	//mysql_session_t *sess=arg;
	mysql_session_t *sess=g_async_queue_pop(conn_queue);

	if (sess->client_fd == 0) {
		fprintf(stderr,"error\n");
	}
//	pthread_mutex_unlock(&conn_mutex);

	mysql_thread_init();
	mysql_session_init(sess);

	sess->client_myds=mysql_data_stream_init(sess->client_fd, sess);

	while(1) if(__sync_val_compare_and_swap(&conn_cnt,1,2)==1) break;

	sess->query_to_cache=FALSE;
	sess->client_command=COM_END;	// always reset this
	sess->send_to_slave=FALSE;

	int arg_on=1, arg_off=0;
	ioctl(sess->client_fd, FIONBIO, (char *)&arg_on);
	sess->client_myds->fd=sess->client_fd;
	sess->fds[0].fd=sess->client_myds->fd;
#ifdef DEBUG_mysql_conn
		debug_print("Starting connection on client fd %d (myds %d , sess %p)\n", sess->client_fd, sess->client_myds->fd, sess);
#endif

	
	sess->status=CONNECTION_READING_CLIENT|CONNECTION_WRITING_CLIENT|CONNECTION_READING_SERVER|CONNECTION_WRITING_SERVER;
	while (sess->healthy) {
	
		// this needs to be changed depending if we are talking to a master or a slave
		if (sess->server_myds) {
			//ioctl(sess->server_fd, FIONBIO, (char *)&arg_on);
			sess->fds[1].fd=sess->server_myds->fd;
			sess->last_server_poll_fd=sess->server_myds->fd;	
			sess->nfds=2;
		} else {
			sess->nfds=1;
		}
		if (sess->client_myds->pkts_sent==0) {
			// send auth pkt to client
			pkt *hs;
#ifdef PKTALLOC
#ifdef DEBUG_pktalloc
			debug_print("%s\n", "mypkt_alloc");
#endif
			hs=mypkt_alloc(sess);
#else
			hs=g_slice_alloc(sizeof(pkt));
#endif
			create_handshake_packet(hs,sess->scramble_buf);
			g_ptr_array_add(sess->client_myds->output.pkts, hs);

		}




		sess->status=CONNECTION_READING_CLIENT|CONNECTION_WRITING_CLIENT|CONNECTION_READING_SERVER|CONNECTION_WRITING_SERVER;
		//r=conn_poll(sess->fds,sess);
		sess->ret=conn_poll(sess);
#ifdef DEBUG_poll
		debug_print("poll: fd %d events %d revents %d , fd %d events %d revents %d\n" , sess->fds[0].fd , sess->fds[0].events, sess->fds[0].revents , sess->fds[1].fd , sess->fds[1].events, sess->fds[1].revents);
#endif
		if (sess->ret == -1 && errno == EINTR)
	        continue;
		
	    if (sess->ret == -1) {
	        PANIC("poll()");
		}
		check_fds_errors(sess);


		//if (sess->client_myds->active==FALSE || sess->server_myds->active==FALSE) {
		if (sess->client_myds->active==FALSE) { // || sess->server_myds->active==FALSE) {
			sess->healthy=0;
			continue;
		}

		if (sync_net(sess,0)==FALSE) {
			mysql_session_close(sess); return NULL;
		}

		buffer2array_2(sess);

		if (sess->client_myds->pkts_sent==1 && sess->client_myds->pkts_recv==1) {
			pkt *hs=NULL;
			hs=g_ptr_array_remove_index(sess->client_myds->input.pkts, 0);
			sess->ret=check_client_authentication_packet(hs,sess);
			g_slice_free1(hs->length, hs->data);
/*
#ifdef PKTALLOC
#ifdef DEBUG_pktalloc
			debug_print("%s\n", "mypkt_free");
#endif
			mypkt_free(hs,sess);
#else
			g_slice_free1(sizeof(pkt), hs);
#endif
*/
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
			return NULL;
		}
		process_mysql_server_pkts(sess);
		stop_timer(sess->timers,TIMER_processdata);

		array2buffer_2(sess);


		if ( (sess->server_myds==NULL) || (sess->last_server_poll_fd==sess->server_myds->fd)) {
// this optimization is possible only if a connection to the backend didn't break in the meantime,
// or a master/slave didn't occur,
// or we never connected to a backend
			if (sync_net(sess,1)==FALSE) {
				mysql_session_close(sess); return NULL;
			}
		}
		if (sess->client_myds->pkts_sent==2 && sess->client_myds->pkts_recv==1) {
			if (sess->mysql_schema_cur==NULL) {
				mysql_session_close(sess); return NULL;
			}
		}

	}

	mysql_session_close(sess);
	return NULL;
}




int main(int argc, char **argv) {

	g_thread_init(NULL);
	conn_queue=g_async_queue_new();
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


	//Unix socket is currently hardcoded , needs to become an option	
#define SERVER_PATH     "/tmp/proxysql.sock"
	
	int listen_tcp_fd=listen_on_port((uint16_t)glovars.proxy_mysql_port);
	int listen_unix_fd = listen_on_unix(SERVER_PATH);
	struct pollfd fds[2];
	int nfds=2;
	fds[0].fd=listen_tcp_fd;
	fds[1].fd=listen_unix_fd;
	mysql_library_init(0, NULL, NULL);
	// starts the main loop
	while (1)
	{
//		pthread_mutex_lock(&conn_mutex);
		fds[0].events=POLLIN;
		fds[1].events=POLLIN;
		// poll() on TCP socket and Unix socket
		poll(fds,nfds,-1);
		if (fds[0].revents==POLLIN) {
			pthread_t child;
			while(1) if(__sync_val_compare_and_swap(&conn_cnt,0,1)==0) break;
			//mysql_session_t *sess=NULL;
			ses=malloc(sizeof(mysql_session_t));
			if (ses==NULL) { exit(EXIT_FAILURE); }
			ses->client_fd = accept(listen_tcp_fd, NULL, NULL); // get the connection from TCP socket
			int arg_on=1;
			setsockopt(ses->client_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &arg_on, sizeof(int));
			g_async_queue_push(conn_queue,ses);
			//if ( pthread_create(&child, &attr, connection_handler_mysql , sess) != 0 ) {
			if ( pthread_create(&child, &attr, connection_handler_mysql , NULL) != 0 ) {
				PANIC("Thread creation");
			}
			while(1) if(__sync_val_compare_and_swap(&conn_cnt,2,0)==2) break;

		}
		if (fds[1].revents==POLLIN) {
			pthread_t child;
			while(1) if(__sync_val_compare_and_swap(&conn_cnt,0,1)==0) break;
			//mysql_session_t *sess=NULL;
			ses=malloc(sizeof(mysql_session_t));
			if (ses==NULL) { exit(EXIT_FAILURE); }
			ses->client_fd = accept(listen_unix_fd, NULL, NULL); // get the connection from Unix socket
			g_async_queue_push(conn_queue,ses);
			//if ( pthread_create(&child, &attr, connection_handler_mysql , sess) != 0 ) {
			if ( pthread_create(&child, &attr, connection_handler_mysql , NULL) != 0 ) {
				PANIC("Thread creation");
			}
			while(1) if(__sync_val_compare_and_swap(&conn_cnt,2,0)==2) break;
		}
		
	}
	return 0;
}
