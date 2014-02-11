
inline void admin_COM_QUERY(mysql_session_t *, pkt *);
int force_remove_servers();
inline pkt * admin_version_comment_pkt(mysql_session_t *);
void reset_query_rule(query_rule_t *);
void reset_query_rules();
inline void init_gloQR();
void init_query_metadata(mysql_session_t *, pkt *);
void process_query_rules(mysql_session_t *);
mysql_server * find_server_ptr(const char *, const uint16_t);
mysql_server * mysql_server_entry_create(const char *, const uint16_t, int, enum mysql_server_status);
inline void mysql_server_entry_add(mysql_server *);
void mysql_server_entry_add_hostgroup(mysql_server *, int);
MSHGE * mysql_server_random_entry_from_hostgroup__lock(int);
MSHGE * mysql_server_random_entry_from_hostgroup__nolock(int);
int mysql_session_create_backend_for_hostgroup(mysql_session_t *, int);
