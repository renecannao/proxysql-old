
#define CONNECTION_READING_CLIENT	1
#define CONNECTION_WRITING_CLIENT	2
#define CONNECTION_READING_SERVER	4
#define CONNECTION_WRITING_SERVER	8

#define TOTAL_TIMERS 7



EXTERN unsigned long long glotimers[TOTAL_TIMERS];

EXTERN global_variables glovars;
EXTERN global_mysql_servers glomysrvs;

EXTERN fdb_hashes_group_t QC;

EXTERN long long glotimenew;
EXTERN long long glotimeold;
EXTERN myConnPools *gloconnpool;

EXTERN mem_superblock_t conn_queue_pool;
EXTERN shared_trash_stack_t myds_pool;

int init_global_variables(GKeyFile *);
mysql_server * new_server_master();
void main_opts(const GOptionEntry *, gint *, gchar ***, gchar *);
