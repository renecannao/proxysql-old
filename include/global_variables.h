
#define CONNECTION_READING_CLIENT	1
#define CONNECTION_WRITING_CLIENT	2
#define CONNECTION_READING_SERVER	4
#define CONNECTION_WRITING_SERVER	8

#define TOTAL_TIMERS 7

#define SQLITE_ADMINDB  "proxysql.db"


EXTERN unsigned long long glotimers[TOTAL_TIMERS];

EXTERN global_variables glovars;
EXTERN global_mysql_servers glomysrvs;

EXTERN fdb_hashes_group_t QC;
//EXTERN int QC_version;

EXTERN global_query_rules_t gloQR;

EXTERN long long glotimenew;
EXTERN long long glotimeold;
EXTERN myConnPools gloconnpool;

EXTERN mem_superblock_t conn_queue_pool;
EXTERN shared_trash_stack_t myds_pool;

EXTERN sqlite3 *sqlite3configdb;

EXTERN ProxyIPC proxyipc;

EXTERN int gdbg;	// global debug
EXTERN debug_level *gdbg_lvl;	// global debug levels

//EXTERN admin_sqlite_table_def_t *table_defs;

int init_global_variables(GKeyFile *);
mysql_server * new_server_master();
mysql_server * new_server_slave();
void process_global_variables_from_file(GKeyFile *);
void main_opts(const GOptionEntry *, gint *, gchar ***, gchar *);

void pre_variable_mysql_threads(global_variable_entry_t *);
void post_variable_core_dump_file_size(global_variable_entry_t *);
void post_variable_net_buffer_size(global_variable_entry_t *);
