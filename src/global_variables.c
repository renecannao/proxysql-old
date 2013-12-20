#include "proxysql.h"



void main_opts(const GOptionEntry *entries, gint *argc, gchar ***argv, gchar *config_file) {


	// Prepare the processing of config file
	GKeyFile *keyfile;

	GError *error = NULL;
	GOptionContext *context;

	signal(SIGSEGV, crash_handler);

	context = g_option_context_new ("- High Performance Advanced Proxy for MySQL");
	g_option_context_add_main_entries (context, entries, NULL);
//  g_option_context_add_group (context, gtk_get_option_group (TRUE));
	//if (!g_option_context_parse (context, &argc, &argv, &error))
	if (!g_option_context_parse (context, argc, argv, &error))
	{
		g_print ("option parsing failed: %s\n", error->message);
		exit (1);
	}

	init_debug_struct();
/*
	if (gdbg) {
		int i;
		for (i=0;i<PROXY_DEBUG_UNKNOWN;i++) {
			gdbg_lvl[i]=INT_MAX;
		}
	}
*/
	proxy_debug(PROXY_DEBUG_GENERIC, 1, "processing opts\n");

	// check if file exists and is readable
	if (!g_file_test(config_file,G_FILE_TEST_EXISTS|G_FILE_TEST_IS_REGULAR)) {
		g_print("Config file %s does not exist\n", config_file); exit(EXIT_FAILURE);
	}   
	if (g_access(config_file, R_OK)) {
		g_print("Config file %s is not readable\n", config_file); exit(EXIT_FAILURE);
	}
	keyfile = g_key_file_new();
	if (!g_key_file_load_from_file(keyfile, config_file, G_KEY_FILE_NONE, &error)) {
		g_print ("Error loading config file %s: %s\n", config_file, error->message); exit(EXIT_FAILURE);
	}
	
	// initialize variables and process config file
	init_global_variables(keyfile);

	g_key_file_free(keyfile);
}


int init_global_variables(GKeyFile *gkf) {
	int i;
	GError *error=NULL;

	// open the file and verify it has [global] section
	proxy_debug(PROXY_DEBUG_GENERIC, 1, "Checking [global]\n");
	if (g_key_file_has_group(gkf,"global")==FALSE) {
		g_print("[global] section not found\n"); exit(EXIT_FAILURE);
	}

	// open the file and verify it has [mysql users] section
	proxy_debug(PROXY_DEBUG_GENERIC, 1, "Checking [mysql users]\n");
	if (g_key_file_has_group(gkf,"mysql users")==FALSE) {
		g_print("[mysql users] section not found\n"); exit(EXIT_FAILURE);
	}

	// processing [debug] section
	proxy_debug(PROXY_DEBUG_GENERIC, 1, "Processing [debug]\n");
	if (g_key_file_has_group(gkf,"mysql users")==FALSE) {
		proxy_debug(PROXY_DEBUG_GENERIC, 1, "[debug] missing\n");	
		memset(gdbg_lvl,0,sizeof(int)*PROXY_DEBUG_UNKNOWN);
	} else {
		int i;
		for (i=0; i<PROXY_DEBUG_UNKNOWN; i++) {
			gdbg_lvl[i].verbosity=0;
			if (g_key_file_has_key(gkf, "debug", gdbg_lvl[i].name, NULL)) {
				gint r=g_key_file_get_integer(gkf, "debug", gdbg_lvl[i].name, &error);
				if (r >= 0 ) { gdbg_lvl[i].verbosity=r; }
			}	
		}
/*
		gdbg_lvl[PROXY_DEBUG_GENERIC]=0;
		if (g_key_file_has_key(gkf, "debug", "debug_generic", NULL)) {
			gint r=g_key_file_get_integer(gkf, "debug", "debug_generic", &error);
			if (r >= 0 ) { gdbg_lvl[PROXY_DEBUG_GENERIC]=r; }
		}
		gdbg_lvl[PROXY_DEBUG_NET]=0;
		if (g_key_file_has_key(gkf, "debug", "debug_net", NULL)) {
			gint r=g_key_file_get_integer(gkf, "debug", "debug_net", &error);
			if (r >= 0 ) { gdbg_lvl[PROXY_DEBUG_NET]=r; }
		}
		gdbg_lvl[PROXY_DEBUG_PKT_ARRAY]=0;
		if (g_key_file_has_key(gkf, "debug", "debug_pkt_array", NULL)) {
			gint r=g_key_file_get_integer(gkf, "debug", "debug_pkt_array", &error);
			if (r >= 0 ) { gdbg_lvl[PROXY_DEBUG_PKT_ARRAY]=r; }
		}
		gdbg_lvl[PROXY_DEBUG_MEMORY]=0;
		if (g_key_file_has_key(gkf, "debug", "debug_memory", NULL)) {
			gint r=g_key_file_get_integer(gkf, "debug", "debug_memory", &error);
			if (r >= 0 ) { gdbg_lvl[PROXY_DEBUG_MEMORY]=r; }
		}
		gdbg_lvl[PROXY_DEBUG_POLL]=0;
		if (g_key_file_has_key(gkf, "debug", "debug_poll", NULL)) {
			gint r=g_key_file_get_integer(gkf, "debug", "debug_poll", &error);
			if (r >= 0 ) { gdbg_lvl[PROXY_DEBUG_POLL]=r; }
		}
		gdbg_lvl[PROXY_DEBUG_MYSQL_COM]=0;
		if (g_key_file_has_key(gkf, "debug", "debug_mysql_com", NULL)) {
			gint r=g_key_file_get_integer(gkf, "debug", "debug_mysql_com", &error);
			if (r >= 0 ) { gdbg_lvl[PROXY_DEBUG_MYSQL_COM]=r; }
		}
		gdbg_lvl[PROXY_DEBUG_MYSQL_AUTH]=0;
		if (g_key_file_has_key(gkf, "debug", "debug_mysql_auth", NULL)) {
			gint r=g_key_file_get_integer(gkf, "debug", "debug_mysql_auth", &error);
			if (r >= 0 ) { gdbg_lvl[PROXY_DEBUG_MYSQL_AUTH]=r; }
		}
		gdbg_lvl[PROXY_DEBUG_SQLITE]=0;
		if (g_key_file_has_key(gkf, "debug", "debug_sqlite", NULL)) {
			gint r=g_key_file_get_integer(gkf, "debug", "debug_sqlite", &error);
			if (r >= 0 ) { gdbg_lvl[PROXY_DEBUG_SQLITE]=r; }
		}
*/
	}

	

	pthread_rwlock_init(&glovars.rwlock_global, NULL);
	pthread_rwlock_init(&glomysrvs.rwlock, NULL);
	pthread_rwlock_init(&glovars.rwlock_usernames, NULL);

	pthread_rwlock_wrlock(&glovars.rwlock_global);

	glovars.protocol_version=10;
	//glovars.server_version="5.0.15";
	glovars.server_capabilities= CLIENT_FOUND_ROWS | CLIENT_PROTOCOL_41 | CLIENT_IGNORE_SIGPIPE | CLIENT_TRANSACTIONS | CLIENT_SECURE_CONNECTION | CLIENT_CONNECT_WITH_DB;
//	glovars.server_capabilities=0xffff;
	glovars.server_language=33;
	glovars.server_status=2;

	glovars.thread_id=1;


	glovars.shutdown=0;

	fdb_system_var.hash_purge_time=10000000;
	if (g_key_file_has_key(gkf, "fundadb", "fundadb_hash_purge_time", NULL)) {
		gint r=g_key_file_get_integer(gkf, "fundadb", "fundadb_hash_purge_time", &error);
		if (r >= 100 ) {		// minimum millisecond to purge a whole hash tabe
			fdb_system_var.hash_purge_time=r*1000; // convert from millisecond to microsecond
		}
	}

	fdb_system_var.hash_purge_loop=100000;
	if (g_key_file_has_key(gkf, "fundadb", "fundadb_hash_purge_loop", NULL)) {
		gint r=g_key_file_get_integer(gkf, "fundadb", "fundadb_hash_purge_loop", &error);
		if (r >= 100 ) {		// minimum millisecond to purge a single block
			fdb_system_var.hash_purge_time=r*1000; // convert from millisecond to microsecond
		}
		if (fdb_system_var.hash_purge_loop > fdb_system_var.hash_purge_time) {
			fdb_system_var.hash_purge_loop=fdb_system_var.hash_purge_time;
		}
	}

	fdb_system_var.hash_expire_max=3600*24*365*10;

	fdb_system_var.hash_expire_default=10;
	if (g_key_file_has_key(gkf, "fundadb", "fundadb_hash_expire_default", NULL)) {
		gint r=g_key_file_get_integer(gkf, "fundadb", "fundadb_hash_expure_default", &error);
		if (r >= 1 && r < fdb_system_var.hash_expire_max) {
			fdb_system_var.hash_expire_default=r;
		}
	}


	
	// set core dump file size
	glovars.core_dump_file_size=0;
	if (g_key_file_has_key(gkf, "global", "core_dump_file_size", NULL)) {
		gint r=g_key_file_get_integer(gkf, "global", "core_dump_file_size", &error);
		struct rlimit rlim;
		rlim.rlim_cur=r;
		rlim.rlim_max=r;
		setrlimit(RLIMIT_CORE,&rlim);
	}

	// set stack_size
	glovars.stack_size=512*1024;	// default stack_size
	if (g_key_file_has_key(gkf, "global", "stack_size", NULL)) {
		gint r=g_key_file_get_integer(gkf, "global", "stack_size", &error);
		if (r >= 64*1024 ) {		// minimum stack_size is 64KB
			glovars.stack_size=r/1024*1024;	// rounding to 1K
		}
	}

	pthread_mutex_init(&conn_queue_pool.mutex, NULL);
	// set net_buffer_size
	conn_queue_pool.size=8*1024;	// default net_buffer_size
	if (g_key_file_has_key(gkf, "global", "net_buffer_size", NULL)) {
		gint r=g_key_file_get_integer(gkf, "global", "net_buffer_size", &error);
		if (r >= 1024 ) {		// minimum net_buffer_size
			conn_queue_pool.size=r/1024*1024; // rounding to 1KB
			if (r >= 1024*1024*16 ) {		// maximum net_buffer_size
				conn_queue_pool.size=16*1024*1024; // rounding to 1KB
			}
		}
	}

	// set conn_qeue_allocator_blocks : this defines how many queues are allocated when more queues are needed
	conn_queue_pool.incremental=128; // default queue allocator blocks size
	if (g_key_file_has_key(gkf, "global", "conn_queue_allocator_blocks", NULL)) {
		gint r=g_key_file_get_integer(gkf, "global", "conn_queue_allocator_blocks", &error);
		if (r >= 64 ) {		// minimum conn_queue_allocator_blocks
			conn_queue_pool.incremental=r/16*16; // rounding to 16
		}
	}

	
	pthread_mutex_init(&myds_pool.mutex, NULL);
	myds_pool.size=sizeof(mysql_data_stream_t);
	myds_pool.incremental=1024;
	myds_pool.blocks=g_ptr_array_new();
	
	{
		// pop and push on element : initialize
		mysql_data_stream_t *t=stack_alloc(&myds_pool);
		stack_free(t,&myds_pool);
	}
	
	


	// set debug
	if (g_key_file_has_key(gkf, "global", "debug", NULL)) {
		gint r=g_key_file_get_integer(gkf, "global", "debug", &error);
		if (r >= 0 ) {
			gdbg=1;
		}
	}

	// set verbose
	glovars.verbose=0;
	if (g_key_file_has_key(gkf, "global", "verbose", NULL)) {
		gint r=g_key_file_get_integer(gkf, "global", "verbose", &error);
		if (r >= 0 ) {
			glovars.verbose=r;
		}
	}

	// set enable_timers
	glovars.enable_timers=0;
	if (g_key_file_has_key(gkf, "global", "enable_timers", NULL)) {
		gint r=g_key_file_get_integer(gkf, "global", "enable_timers", &error);
		if (r >= 0 ) {
			glovars.enable_timers=TRUE;
		}
	}

	// set print_statistics_interval
	glovars.print_statistics_interval=10;
	if (g_key_file_has_key(gkf, "global", "print_statistics_interval", NULL)) {
		gint r=g_key_file_get_integer(gkf, "global", "print_statistics_interval", &error);
		if (r >= 0 ) {
			glovars.print_statistics_interval=r;
		}
	}

	// set backlog
	glovars.backlog=2000;	// used by listen()
	if (g_key_file_has_key(gkf, "global", "backlog", NULL)) {
		gint r=g_key_file_get_integer(gkf, "global", "backlog", &error);
		if (r >= 50 ) {		// minimum backlog length
			glovars.backlog=r;
		}
	}

	// set proxy_mysql_port
	glovars.proxy_mysql_port=6033;	// default proxy mysql port
	if (g_key_file_has_key(gkf, "mysql", "proxy_mysql_port", NULL)) {
		gint r=g_key_file_get_integer(gkf, "mysql", "proxy_mysql_port", &error);
		if (r >= 0 ) {
			glovars.proxy_mysql_port=r;
		}
	}

	// set proxy_admin_port
	glovars.proxy_admin_port=glovars.proxy_mysql_port-1;	// default proxy admin port is proxy mysql port -1
	if (g_key_file_has_key(gkf, "global", "proxy_admin_port", NULL)) {
		gint r=g_key_file_get_integer(gkf, "global", "proxy_admin_port", &error);
		if (r >= 0 ) {
			glovars.proxy_admin_port=r;
		}
	}


	// set mysql_threads
	glovars.mysql_threads=sysconf(_SC_NPROCESSORS_ONLN)*2;
	if (g_key_file_has_key(gkf, "mysql", "mysql_threads", NULL)) {
		gint r=g_key_file_get_integer(gkf, "mysql", "mysql_threads", &error);
		if (r >= 1 ) { 	// 1 thread is the minimum
			glovars.mysql_threads=r;
			if ( r > 128 ) // 128 threads is the maximum
				glovars.mysql_threads=128;
		}
	}

	// set mysql_default_schema
	if (g_key_file_has_key(gkf, "mysql", "mysql_default_schema", NULL)) {
		glovars.mysql_default_schema=g_key_file_get_string(gkf, "mysql", "mysql_default_schema", &error);	
	} else {
		glovars.mysql_default_schema=strdup("information_schema");	// default mysql_default_schema
	}

	// set mysql_socket
	if (g_key_file_has_key(gkf, "mysql", "mysql_socket", NULL)) {
		glovars.mysql_socket=g_key_file_get_string(gkf, "mysql", "mysql_socket", &error);	
	} else {
		glovars.mysql_socket=strdup("/tmp/proxysql.sock");	// default mysql_default_schema
	}

	// enable mysql auto-reconnect
	glovars.mysql_auto_reconnect_enabled=TRUE;
	if (g_key_file_has_key(gkf, "mysql", "mysql_auto_reconnect_enabled", NULL)) {
		gint r=g_key_file_get_integer(gkf, "mysql", "mysql_auto_reconnect_enabled", &error);
		if (r == 0 ) {
			glovars.mysql_auto_reconnect_enabled=FALSE;
		}
	}


	// set query cache	
	glovars.mysql_query_cache_enabled=TRUE;
	if (g_key_file_has_key(gkf, "mysql", "mysql_query_cache_enabled", NULL)) {
		gint r=g_key_file_get_integer(gkf, "mysql", "mysql_query_cache_enabled", &error);
		if (r == 0 ) {
			glovars.mysql_query_cache_enabled=FALSE;
		}
	}
	// init gloQR
	init_gloQR();

	if (glovars.mysql_query_cache_enabled==TRUE ) {
	// set query_cache_partitions
		glovars.mysql_query_cache_partitions=16;	// default mysql query cache partitions
		if (g_key_file_has_key(gkf, "mysql", "mysql_query_cache_partitions", NULL)) {
			gint r=g_key_file_get_integer(gkf, "mysql", "mysql_query_cache_partitions", &error);
			if (r >= 1 ) { 	// minimum query cache partitions
				glovars.mysql_query_cache_partitions=r;
			}
		}

	// set query_cache default timeout
		glovars.mysql_query_cache_default_timeout=1;
		if (g_key_file_has_key(gkf, "mysql", "mysql_query_cache_default_timeout", NULL)) {
			gint r=g_key_file_get_integer(gkf, "mysql", "mysql_query_cache_default_timeout", &error);
			if (r >= 0 && r<fdb_system_var.hash_expire_max ) {
				glovars.mysql_query_cache_default_timeout=r;
			}
		}
	}

	// set mysql_max_resultset_size
	glovars.mysql_max_resultset_size=1024*1024;	// default max_resultset_size
	if (g_key_file_has_key(gkf, "mysql", "mysql_max_resultset_size", NULL)) {
		gint r=g_key_file_get_integer(gkf, "mysql", "mysql_max_resultset_size", &error);
		if (r >= 0 ) { 	// no minimum resultset_size
			glovars.mysql_max_resultset_size=r;
		}
	}


//#define MAX_PKT_SIZE 16777216
#define MAX_PKT_SIZE 16777210
	// set mysql_max_query_size
	glovars.mysql_max_query_size=1024*1024;	// default max_query_size
	if (g_key_file_has_key(gkf, "mysql", "mysql_max_query_size", NULL)) {
		gint r=g_key_file_get_integer(gkf, "mysql", "mysql_max_query_size", &error);
		if (r <= MAX_PKT_SIZE ) { 	// maximum max_query_size is 16M . This to avoid queries split over multiple packages.
			glovars.mysql_max_query_size=r;
		} else {
			glovars.mysql_max_query_size=MAX_PKT_SIZE;
		}
	}

	// set mysql_poll_timeout
	glovars.mysql_poll_timeout=10000;	// default mysql poll timeout is milliseconds ( 10 seconds )
	if (g_key_file_has_key(gkf, "mysql", "mysql_poll_timeout", NULL)) {
		gint r=g_key_file_get_integer(gkf, "mysql", "mysql_poll_timeout", &error);
		if (r >= 100 ) { 	// minimum mysql poll timeout
			glovars.mysql_poll_timeout=r;
		}
	}

	// set mysql_wait_timeout
	glovars.mysql_wait_timeout=(unsigned long long) 8*3600*1000000;	// default mysql wait timeout is microseconds ( 8 hours )
	if (g_key_file_has_key(gkf, "mysql", "mysql_wait_timeout", NULL)) {
		gint r=g_key_file_get_integer(gkf, "mysql", "mysql_wait_timeout", &error);
		if (r >= 1 ) { 	// minimum mysql connection pool timeout
			glovars.mysql_wait_timeout=r*1000000;
		}
	}

	// set mysql_server_version
	glovars.server_version="5.1.30"; // 5.1 GA
	if (g_key_file_has_key(gkf, "mysql", "mysql_server_version", NULL)) {
			glovars.server_version=g_key_file_get_string(gkf, "mysql", "mysql_server_version", &error);
	}


	// set mysql_usage_user
	glovars.mysql_usage_user="proxy";	// default
	if (g_key_file_has_key(gkf, "mysql", "mysql_usage_user", NULL)) {
			glovars.mysql_usage_user=g_key_file_get_string(gkf, "mysql", "mysql_usage_user", &error);
	}

	// set mysql_usage_password
	glovars.mysql_usage_password="proxy";	// default
	if (g_key_file_has_key(gkf, "mysql", "mysql_usage_password", NULL)) {
			glovars.mysql_usage_password=g_key_file_get_string(gkf, "mysql", "mysql_usage_password", &error);
	}

	// set proxy_admin_user
	glovars.proxy_admin_user="admin";	// default
	if (g_key_file_has_key(gkf, "mysql", "proxy_admin_user", NULL)) {
			glovars.mysql_usage_user=g_key_file_get_string(gkf, "mysql", "proxy_admin_user", &error);
	}

	// set proxy_admin_password
	glovars.proxy_admin_password="admin";	// default
	if (g_key_file_has_key(gkf, "global", "proxy_admin_password", NULL)) {
			glovars.proxy_admin_password=g_key_file_get_string(gkf, "global", "proxy_admin_password", &error);
	}

	glomysrvs.mysql_use_masters_for_reads=1;
	if (g_key_file_has_key(gkf, "mysql", "mysql_use_masters_for_reads", NULL)) {
		gint r=g_key_file_get_integer(gkf, "mysql", "mysql_use_masters_for_reads", &error);
		if (r == 0 ) {
			glomysrvs.mysql_use_masters_for_reads=0;
		}
	}
	

	glomysrvs.mysql_connections_max=10000; // hardcoded for now , theorically no limit : NOT USED YET
//	if ((glovars.connections=malloc(sizeof(connection *)*glovars.max_connections))==NULL) { exit(1); }
	glomysrvs.mysql_connections_cur=0; // hardcoded for now


	glomysrvs.mysql_connections=g_ptr_array_sized_new(glomysrvs.mysql_connections_max/10+4);

	pthread_rwlock_wrlock(&glomysrvs.rwlock);
	glomysrvs.servers_masters=g_ptr_array_new();



	// create the connection pool
	gloconnpool=mysql_connpool_init();


	// enable connection pool
	gloconnpool->enabled=TRUE;
	if (g_key_file_has_key(gkf, "mysql", "mysql_connection_pool_enabled", NULL)) {
		gint r=g_key_file_get_integer(gkf, "mysql", "mysql_connection_pool_enabled", &error);
		if (r == 0 ) {
			gloconnpool->enabled=FALSE;
		}
	}



/*
	glomysrvs.count_masters=3; // hardcoded for now
	// initialize masters
	for (i=0;i<glomysrvs.count_masters;i++) {
		mysql_server *ms;
		if ((ms=malloc(sizeof(mysql_server)))==NULL) { exit(1); }
		ms->address="127.0.0.1";
		ms->port=3306;
		ms->connections=0;
		ms->alive=1;
		g_ptr_array_add(glomysrvs.servers_masters, (gpointer) ms);
	}
*/
	// load all servers
	glomysrvs.count_masters=0;
	glomysrvs.count_slaves=0;
	glomysrvs.servers_slaves=g_ptr_array_new();
	if (g_key_file_has_key(gkf, "mysql", "mysql_servers", NULL)) {
		gsize l=0;
		glomysrvs.mysql_servers_name=g_key_file_get_string_list(gkf, "mysql", "mysql_servers", &l, &error);
		int i;
		for (i=0; i<l; i++) {
			char *c;
			c=index(glomysrvs.mysql_servers_name[i],':');
			mysql_server *ms=g_slice_alloc0(sizeof(mysql_server));
			if (ms==NULL) { exit(EXIT_FAILURE); }
			if (c) {
				int sl=strlen(glomysrvs.mysql_servers_name[i]);
				char *s;
				if ((s=malloc(sl))==NULL) { exit(EXIT_FAILURE); }
				char *p;
				if ((p=malloc(sl))==NULL) { exit(EXIT_FAILURE); }
				*c=' ';
				sscanf(glomysrvs.mysql_servers_name[i],"%s %s",s,p);
				proxy_debug(PROXY_DEBUG_MYSQL_SERVER, 1, "Configuring server %s port %s\n", s, p);
				ms->address=g_strdup(s);
				ms->port=atoi(p);
				free(s);
				free(p);
			} else {
				ms->address=g_strdup(glomysrvs.mysql_servers_name[i]);
				ms->port=3306;
			}
			int ro=mysql_check_alive_and_read_only(ms->address,  ms->port);
			if (ro==-1) {
				ms->alive=0;
			} else {
				ms->alive=1;
			}
			proxy_debug(PROXY_DEBUG_MYSQL_SERVER, 1, "Adding slave %s:%d , %s\n", ms->address, ms->port , (ms->alive ? "ACTIVE" : "DEAD"));
			if ((ro==1) || (glomysrvs.mysql_use_masters_for_reads==1)) {
				g_ptr_array_add(glomysrvs.servers_slaves, (gpointer) ms);
				glomysrvs.count_slaves++;
			}
			if (ro==0) {
				g_ptr_array_add(glomysrvs.servers_masters, (gpointer) ms);
				glomysrvs.count_masters++;
				proxy_debug(PROXY_DEBUG_MYSQL_SERVER, 1, "Adding master %s:%d\n", ms->address, ms->port);
			}
		}
	} else {
		// This needs to go away. Servers can be configured in sqlite, or added alter on
		g_print("mysql_servers not defined in [mysql]\n"); exit(EXIT_FAILURE);
	}

	pthread_rwlock_unlock(&glomysrvs.rwlock);
	
{ // load usernames and password
	pthread_rwlock_wrlock(&glovars.rwlock_usernames);
	glovars.mysql_users_name=g_ptr_array_new();
	glovars.mysql_users_pass=g_ptr_array_new();
	glovars.usernames = g_hash_table_new(g_str_hash, g_str_equal);
	//gchar **users_keys=NULL;
	gsize l=0;
	gchar **mysql_users_name=NULL;
	gchar **mysql_users_pass=NULL;
	mysql_users_name=g_key_file_get_keys(gkf, "mysql users", &l, &error);
	if (l==0) {
		g_print("No mysql users defined in [mysql users]\n"); exit(EXIT_FAILURE);
	} else {
		mysql_users_pass=g_strdupv(mysql_users_name);
		int i;
		for (i=0; i<l; i++) {
			g_free(mysql_users_pass[i]);
			mysql_users_pass[i]=g_key_file_get_string(gkf, "mysql users", mysql_users_name[i], &error);
			if (mysql_users_pass[i]==NULL) {
				g_print("Error in password for user %s\n", mysql_users_name[i]); exit(EXIT_FAILURE);
			}
			proxy_debug(PROXY_DEBUG_MYSQL_AUTH, 4, "Adding user %s password %s (%d)\n", mysql_users_name[i], mysql_users_pass[i], strlen(mysql_users_pass[i]));
			g_ptr_array_add(glovars.mysql_users_name,g_strdup(mysql_users_name[i]));
			g_ptr_array_add(glovars.mysql_users_pass,g_strdup(mysql_users_pass[i]));
			g_hash_table_insert(glovars.usernames, g_ptr_array_index(glovars.mysql_users_name,i), g_ptr_array_index(glovars.mysql_users_pass,i));

		}
	}
	g_strfreev(mysql_users_name);
	g_strfreev(mysql_users_pass);
	pthread_rwlock_unlock(&glovars.rwlock_usernames);
}

	pthread_mutex_lock(&conn_queue_pool.mutex);
	conn_queue_pool.blocks=g_ptr_array_new();

	for (i=0;i<1;i++) { // create a memory block for queues
		mem_block_t *mb=create_mem_block(&conn_queue_pool);
		g_ptr_array_add(conn_queue_pool.blocks,mb);	
	}
	pthread_mutex_unlock(&conn_queue_pool.mutex);


/*
	for (i=0;i<glovars.count_slaves;i++) {
		if ((glovars.servers_slaves[i]=malloc(sizeof(mysql_server)))==NULL) { exit(1); }
			glovars.servers_slaves[i]->address="127.0.0.1";
			glovars.servers_slaves[i]->port=3306;
			glovars.servers_slaves[i]->connections=0;
			glovars.servers_slaves[i]->valid=1;
	}
*/
	pthread_rwlock_unlock(&glovars.rwlock_global);
	return 0;
}

mysql_server * new_server_master() {
	pthread_rwlock_wrlock(&glomysrvs.rwlock);
	if ( glomysrvs.count_masters==0 ) return NULL;
	int i=rand()%glomysrvs.count_masters;
	mysql_server *ms=g_ptr_array_index(glomysrvs.servers_masters,i);
	proxy_debug(PROXY_DEBUG_MYSQL_SERVER, 4, "Using master %s port %d , index %d from a pool of %d servers\n", ms->address, ms->port, i, glomysrvs.count_masters);
	pthread_rwlock_unlock(&glomysrvs.rwlock);
	return ms;
}

mysql_server * new_server_slave() {
	pthread_rwlock_wrlock(&glomysrvs.rwlock);
	if ( glomysrvs.count_slaves==0 ) return NULL;
	int i=rand()%glomysrvs.count_slaves;
	mysql_server *ms=g_ptr_array_index(glomysrvs.servers_slaves,i);
	proxy_debug(PROXY_DEBUG_MYSQL_SERVER, 4, "Using slave %s port %d , index %d from a pool of %d servers\n", ms->address, ms->port, i, glomysrvs.count_slaves);
	pthread_rwlock_unlock(&glomysrvs.rwlock);
	return ms;
}
