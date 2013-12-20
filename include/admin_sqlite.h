#define ADMIN_SQLITE_TABLE_SERVER_STATUS "CREATE TABLE server_status ( status VARCHAR NOT NULL PRIMARY KEY )"
#define ADMIN_SQLITE_TABLE_SERVERS	"CREATE TABLE servers ( name VARCHAR NOT NULL PRIMARY KEY, hostname VARCHAR NOT NULL , port INT NOT NULL DEFAULT 3306 , read_only INT NOT NULL DEFAULT 1, status VARCHAR NOT NULL DEFAULT ('OFFLINE') REFERENCES server_status(status))"
#define ADMIN_SQLITE_TABLE_USERS "CREATE TABLE users ( username VARCHAR NOT NULL PRIMARY KEY , password VARCHAR , active INT NOT NULL DEFAULT 1)"
#define ADMIN_SQLITE_TABLE_DEBUG_LEVELS "CREATE TABLE debug_levels (module VARCHAR NOT NULL PRIMARY KEY, verbosity INT NOT NULL DEFAULT 0)"
#define ADMIN_SQLITE_TABLE_GLOBAL_VARIABLES "CREATE TABLE global_variables ( name VARCHAR NOT NULL PRIMARY KEY , value VARCHAR NOT NULL )"
#define ADMIN_SQLITE_TABLE_QUERY_RULES "CREATE TABLE query_rules (rule_id INT NOT NULL PRIMARY KEY, flagIN INT NOT NULL DEFAULT 0, match_pattern VARCHAR NOT NULL, negate_match_pattern INT NOT NULL DEFAULT 0, flagOUT INT NOT NULL DEFAULT 0, replace_pattern VARCHAR, caching_ttl INT NOT NULL DEFAULT 0)"

struct _admin_sqlite_table_def_t {
	char *table_name;
	char *table_def;
};


void mysql_pkt_err_from_sqlite(pkt *, const char *);
int mysql_pkt_to_sqlite_exec(pkt *, mysql_session_t *);
void sqlite3_exec_exit_on_failure(sqlite3 *, const char *);
void sqlite3_flush_debug_levels_mem_to_db(int);
int sqlite3_flush_debug_levels_db_to_mem();
void sqlite3_flush_users_mem_to_db(int, int);
int sqlite3_flush_users_db_to_mem();
void admin_init_sqlite3();
int sqlite3_flush_query_rules_db_to_mem();
