#include <glib.h>

typedef struct _myConnPools {
	pthread_mutex_t mutex;
	GPtrArray *connpools;
	gboolean enabled;
} myConnPools;

//gboolean reconnect_server_on_shut_fd(mysql_session_t *, mysql_cp_entry_t **);
gboolean reconnect_server_on_shut_fd(mysql_session_t *);
myConnPools * mysql_connpool_init();
void * mysql_connpool_purge_thread();
mysql_connpool *find_mysql_connpool_find(myConnPools *, const char *, const char *, const char *, const char *, unsigned int);
mysql_connpool *create_mysql_connpool_create(myConnPools *, const char *, const char *, const char *, const char *, unsigned int);
mysql_cp_entry_t *mysql_connpool_get_connection(myConnPools *, const char *, const char *, const char *, const char *, unsigned int);
void mysql_connpool_detach_connection(myConnPools *, mysql_cp_entry_t *);
