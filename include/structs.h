

typedef struct __fdb_hash_t {
    pthread_rwlock_t lock;
    GHashTable *hash;
    GPtrArray *ptrArray;
    unsigned long long dataSize;
    unsigned long long purgeChunkSize;
    unsigned long long purgeIdx;
} fdb_hash_t;

typedef struct __fdb_hashes_group_t {
	fdb_hash_t **fdb_hashes;
	int size;
	time_t now;
    unsigned int hash_expire_default;
    unsigned long long cntDel;
    unsigned long long cntGet;
    unsigned long long cntGetOK;
    unsigned long long cntSet;
	unsigned long long cntPurge;
} fdb_hashes_group_t;

typedef struct __fdb_hash_entry {
    char *key;
    char *value;
    fdb_hash_t *hash;
    struct __fdb_hash_entry *self;
    unsigned int klen;
    unsigned int length;
    time_t expire;
    time_t access;
} fdb_hash_entry;

typedef struct __fdb_system_var_t {
    long long hash_purge_time;
    long long hash_purge_loop;
    unsigned int hash_expire_max;
    unsigned int hash_expire_default;
} fdb_system_var_t;


EXTERN fdb_system_var_t fdb_system_var;
EXTERN fdb_hash_t **fdb_hashes;

enum enum_timer { 
	TIMER_array2buffer,
	TIMER_buffer2array,
	TIMER_read_from_net,
	TIMER_write_to_net,
	TIMER_processdata,
	TIMER_find_queue,
	TIMER_poll
};


typedef struct _timer {
	unsigned long long total, begin;
} timer;


enum enum_resultset_progress {
	RESULTSET_WAITING,
	RESULTSET_COLUMN_COUNT,
	RESULTSET_COLUMN_DEFINITIONS,
	RESULTSET_EOF1,
	RESULTSET_ROWS,
	RESULTSET_COMPLETED
};

typedef struct _queue_t {
	void *buffer;
	int size;
	int head;
	int tail;
} queue_t;

// structure that defines mysql protocol header
typedef struct _mysql_hdr {
   u_int pkt_length:24, pkt_id:8;
} mysql_hdr;

typedef struct _pkt {
	int length;
	void *data;
} pkt;

typedef struct _mysql_server {
	char *address;
	uint16_t port;
	uint16_t flags;
	unsigned int connections;
	unsigned char alive;
} mysql_server;

typedef struct _bytes_stats {
	uint64_t bytes_recv;
	uint64_t bytes_sent;
} bytes_stats;

typedef struct _mysql_uni_ds_t {
	queue_t queue;
	GPtrArray *pkts;
	int partial;
	pkt *mypkt;
	mysql_hdr hdr;
} mysql_uni_ds_t;

typedef struct _mysql_cp_entry_t {
	MYSQL *conn;
	unsigned long long expire;
} mysql_cp_entry_t;

typedef struct _mysql_connpool {
	char *hostname;
	char *username;
	char *password;
	char *db;
	unsigned int port;
//  GPtrArray *used_conns;  // temporary (?) disabled
	GPtrArray *free_conns;
} mysql_connpool;


typedef struct _mysql_data_stream_t mysql_data_stream_t;
typedef struct _mysql_session_t mysql_session_t;
typedef struct _free_pkts_t free_pkts_t;
typedef struct _shared_trash_stack_t shared_trash_stack_t;

struct _mysql_data_stream_t {
	mysql_session_t *sess;	// this MUST always the first, because will be overwritten when pushed in a trash stack
	uint64_t pkts_recv;
	uint64_t pkts_sent;
	bytes_stats bytes_info;
	mysql_uni_ds_t input;
	mysql_uni_ds_t output;
	int fd;
	gboolean active;
//	mysql_server *server_ptr;
//	mysql_cp_entry_t *mycpe;
};


struct _free_pkts_t {
	GTrashStack *stack;
	GPtrArray *blocks;	
};


struct _shared_trash_stack_t {
	pthread_mutex_t mutex;
	GTrashStack *stack;
	GPtrArray *blocks;
	int size;
	int incremental;	
};

struct _mysql_session_t {
	int healthy;
	int client_fd;
	int server_fd;
	int master_fd;
	int slave_fd;
	int status;
	int ret;	// generic return status
	struct pollfd fds[3];
	int nfds;
	int last_server_poll_fd;
	bytes_stats server_bytes_at_cmd;
	enum enum_server_command client_command;
	enum enum_resultset_progress resultset_progress;
	int resultset_size;
	GChecksum *query_checksum;
	gboolean query_to_cache;
	GRegex *regex[2];
	GPtrArray *resultset;
	mysql_server *server_ptr;
	mysql_server *master_ptr;
	mysql_server *slave_ptr;
	mysql_data_stream_t *client_myds;
	mysql_data_stream_t *server_myds;
	mysql_data_stream_t *master_myds;
	mysql_data_stream_t *slave_myds;
//	mysql_data_stream_t *idle_server_myds;
	mysql_cp_entry_t *server_mycpe;
	mysql_cp_entry_t *master_mycpe;
	mysql_cp_entry_t *slave_mycpe;
//	mysql_cp_entry_t *idle_server_mycpe;
	char *mysql_username;
	char *mysql_password;
	char *mysql_schema_cur;
	char *mysql_schema_new;	
	char scramble_buf[21];
	timer *timers;
	gboolean mysql_query_cache_hit;
	gboolean mysql_server_reconnect;
	gboolean send_to_slave;
	free_pkts_t free_pkts;
};



typedef struct _global_variables {
	pthread_rwlock_t rwlock_global;
	pthread_rwlock_t rwlock_usernames;

	gboolean shutdown;

	unsigned char protocol_version;
	char *server_version;
	uint16_t server_capabilities;
	uint8_t server_language;
	uint16_t server_status;

	uint32_t	thread_id;


	gint core_dump_file_size;
	int stack_size;
	gint proxy_mysql_port;
	gint proxy_admin_port;
	int backlog;
	int verbose;
	int print_statistics_interval;
	
	gboolean enable_timers;

	int mysql_poll_timeout;

	int mysql_threads;	
	gboolean mysql_auto_reconnect_enabled;
	gboolean mysql_query_cache_enabled;
	int mysql_query_cache_partitions;
	unsigned int mysql_query_cache_default_timeout;
	unsigned long long mysql_wait_timeout;
	int mysql_max_resultset_size;
	int mysql_max_query_size;

	// this user needs only USAGE grants
	// and it is use only to create a connection
	unsigned char *mysql_usage_user;
	unsigned char *mysql_usage_password;

	unsigned char *mysql_default_schema;
	unsigned char *mysql_socket;

//	unsigned int count_masters;
//	unsigned int count_slaves;
//	GPtrArray *servers_masters;
//	GPtrArray *servers_slaves;
//	gchar **mysql_servers_name;	// used to parse config file
	GHashTable *usernames;
	gchar **mysql_users_name; // used to parse config file
	gchar **mysql_users_pass; // used to parse config file
//	unsigned int mysql_connections_max;
//	unsigned int mysql_connections_cur;
//	GPtrArray *mysql_connections;
//	unsigned int net_buffer_size;
//	unsigned int conn_queue_allocator_blocks;
//	GPtrArray *conn_queue_allocator;
} global_variables;


typedef struct _global_mysql_servers {
	pthread_rwlock_t rwlock;
	unsigned int mysql_connections_max;
	unsigned int mysql_connections_cur;
	unsigned int count_masters;
	unsigned int count_slaves;
	gchar **mysql_servers_name;	// used to parse config file
	//GPtrArray *servers;
	GPtrArray *servers_masters;
	GPtrArray *servers_slaves;	
	GPtrArray *mysql_connections;
	gboolean mysql_use_masters_for_reads;	
} global_mysql_servers;




enum MySQL_response_type {
	OK_Packet,
	ERR_Packet,
	EOF_Packet,
	UNKNOWN_Packet,
};


typedef struct _mem_block_t {
	GPtrArray *used;
	GPtrArray *free;
	void *mem;
} mem_block_t;


typedef struct _mem_superblock_t {
	pthread_mutex_t mutex;
	GPtrArray *blocks;
	int size;
	int incremental;
} mem_superblock_t;
