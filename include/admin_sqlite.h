#define ADMIN_SQLITE_TABLE_SERVER_STATUS "CREATE TABLE server_status ( status INT NOT NULL PRIMARY KEY, status_desc VARCHAR NOT NULL, UNIQUE(status) )"
//#define ADMIN_SQLITE_TABLE_SERVERS "CREATE TABLE servers ( name VARCHAR NOT NULL PRIMARY KEY, hostname VARCHAR NOT NULL , port INT NOT NULL DEFAULT 3306 , read_only INT NOT NULL DEFAULT 1, status VARCHAR NOT NULL DEFAULT ('OFFLINE') REFERENCES server_status(status), hostgroup INT NOT NULL DEFAULT 0)"
#define ADMIN_SQLITE_TABLE_SERVERS "CREATE TABLE servers ( hostname VARCHAR NOT NULL , port INT NOT NULL DEFAULT 3306 , read_only INT NOT NULL DEFAULT 1, status VARCHAR NOT NULL DEFAULT ('OFFLINE') REFERENCES server_status(status) , PRIMARY KEY(hostname, port) )"
#define ADMIN_SQLITE_TABLE_HOSTGROUPS "CREATE TABLE hostgroups ( hostgroup_id INT NOT NULL DEFAULT 0, hostname VARCHAR NOT NULL , port INT NOT NULL DEFAULT 3306, FOREIGN KEY (hostname, port) REFERENCES servers (hostname, port) , PRIMARY KEY (hostgroup_id, hostname, port) )"
#define ADMIN_SQLITE_TABLE_USERS "CREATE TABLE users ( username VARCHAR NOT NULL PRIMARY KEY , password VARCHAR , active INT NOT NULL DEFAULT 1)"
#define ADMIN_SQLITE_TABLE_DEBUG_LEVELS "CREATE TABLE debug_levels (module VARCHAR NOT NULL PRIMARY KEY, verbosity INT NOT NULL DEFAULT 0)"
#define ADMIN_SQLITE_TABLE_GLOBAL_VARIABLES "CREATE TABLE global_variables ( name VARCHAR NOT NULL PRIMARY KEY , value VARCHAR NOT NULL )"
//#define ADMIN_SQLITE_TABLE_QUERY_RULES "CREATE TABLE query_rules (rule_id INT NOT NULL PRIMARY KEY, username VARCHAR, schemaname VARCHAR, flagIN INT NOT NULL DEFAULT 0, match_pattern VARCHAR NOT NULL, negate_match_pattern INT NOT NULL DEFAULT 0, flagOUT INT NOT NULL DEFAULT 0, replace_pattern VARCHAR, destination_hostgroup INT NOT NULL DEFAULT 0, audit_log INT NOT NULL DEFAULT 0, performance_log INT NOT NULL DEFAULT 0, caching_ttl INT NOT NULL DEFAULT 0)"
//#define ADMIN_SQLITE_TABLE_QUERY_RULES "CREATE TABLE query_rules (rule_id INT NOT NULL PRIMARY KEY, username VARCHAR, schemaname VARCHAR, flagIN INT NOT NULL DEFAULT 0, match_pattern VARCHAR NOT NULL, negate_match_pattern INT NOT NULL DEFAULT 0, flagOUT INT NOT NULL DEFAULT 0, replace_pattern VARCHAR, destination_hostgroup INT NOT NULL DEFAULT 0 REFERENCES hostgroups(hostgroup_id), audit_log INT NOT NULL DEFAULT 0, performance_log INT NOT NULL DEFAULT 0, cache_tag INT NOT NULL DEFAULT 0, invalidate_cache_tag INT NOT NULL DEFAULT 0, invalidate_cache_pattern VARCHAR, cache_ttl INT NOT NULL DEFAULT 0)"
#define ADMIN_SQLITE_TABLE_QUERY_RULES "CREATE TABLE query_rules (rule_id INT NOT NULL PRIMARY KEY, active INT NOT NULL DEFAULT 0, username VARCHAR, schemaname VARCHAR, flagIN INT NOT NULL DEFAULT 0, match_pattern VARCHAR NOT NULL, negate_match_pattern INT NOT NULL DEFAULT 0, flagOUT INT NOT NULL DEFAULT 0, replace_pattern VARCHAR, destination_hostgroup INT NOT NULL DEFAULT 0, audit_log INT NOT NULL DEFAULT 0, performance_log INT NOT NULL DEFAULT 0, cache_tag INT NOT NULL DEFAULT 0, invalidate_cache_tag INT NOT NULL DEFAULT 0, invalidate_cache_pattern VARCHAR, cache_ttl INT NOT NULL DEFAULT 0)"

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
int sqlite3_flush_servers_db_to_mem();
void sqlite3_flush_servers_mem_to_db(int);
int sqlite3_flush_query_rules_db_to_mem();
