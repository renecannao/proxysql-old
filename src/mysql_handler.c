#include "proxysql.h"


inline pkt * admin_version_comment_pkt(mysql_session_t *sess) {
	pkt *p;
	p=mypkt_alloc(sess);
	// hardcoded, we send " (ProxySQL) "
	p->length=81;
	p->data=g_slice_alloc0(p->length);
	memcpy(p->data,"\x01\x00\x00\x01\x01\x27\x00\x00\x02\x03\x64\x65\x66\x00\x00\x00\x11\x40\x40\x76\x65\x72\x73\x69\x6f\x6e\x5f\x63\x6f\x6d\x6d\x65\x6e\x74\x00\x0c\x21\x00\x18\x00\x00\x00\xfd\x00\x00\x1f\x00\x00\x05\x00\x00\x03\xfe\x00\x00\x02\x00\x0b\x00\x00\x04\x0a\x28\x50\x72\x6f\x78\x79\x53\x51\x4c\x29\x05\x00\x00\x05\xfe\x00\x00\x02\x00",p->length);
	return p;
}

inline void admin_COM_QUERY(mysql_session_t *sess, pkt *p) {
	int rc;
	// enter admin mode
	// configure the session to not send data to servers using a hack: pretend the result set is cached
	sess->mysql_query_cache_hit=TRUE;
	sess->query_to_cache=FALSE;
	if (strncasecmp("FLUSH DEBUG",  p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1)==0) {
		int affected_rows=sqlite3_flush_debug_levels_db_to_mem();
	    pkt *ok=mypkt_alloc(sess);
		myproto_ok_pkt(ok,1,affected_rows,0,2,0);
		g_ptr_array_add(sess->client_myds->output.pkts, ok);
		return;
	}
	if (strncasecmp("SHOW TABLES", p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1)==0) {
		char *str="SELECT name AS tables FROM sqlite_master WHERE type='table'";
		g_slice_free1(p->length, p->data);
		int l=strlen(str);
		p->data=g_slice_alloc0(l+sizeof(mysql_hdr)+1);
		p->length=l+sizeof(mysql_hdr)+1;
		memset(p->data+sizeof(mysql_hdr), COM_QUERY, 1);
		memcpy(p->data+sizeof(mysql_hdr)+1,str,l);
	}
	{
		static char *strA="SHOW CREATE TABLE ";
		static char *strB="SELECT name AS 'table' , sql AS 'Create Table' FROM sqlite_master WHERE type='table' AND name='%s'";
		int strAl=strlen(strA);	
		if (strncasecmp("SHOW CREATE TABLE ", p->data+sizeof(mysql_hdr)+1, strAl)==0) {
			int strBl=strlen(strB);
			int tblnamelen=p->length-sizeof(mysql_hdr)-1-strAl;
			int l=strBl+tblnamelen-2;
			char *buff=g_malloc0(l);
			snprintf(buff,l,strB,p->data+sizeof(mysql_hdr)+1+strAl);
			buff[l-1]='\'';
			g_slice_free1(p->length, p->data);
			p->data=g_slice_alloc0(l+sizeof(mysql_hdr)+1);
			p->length=l+sizeof(mysql_hdr)+1;
			memset(p->data+sizeof(mysql_hdr), COM_QUERY, 1);
			memcpy(p->data+sizeof(mysql_hdr)+1,buff,l);
			g_free(buff);
		}
	}
	if (strncasecmp("FLUSH USERS",  p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1)==0) {
		int affected_rows=sqlite3_flush_users_db_to_mem();
	    pkt *ok=mypkt_alloc(sess);
		myproto_ok_pkt(ok,1,affected_rows,0,2,0);
		g_ptr_array_add(sess->client_myds->output.pkts, ok);
		return;
	}
	if (strncasecmp("FLUSH QUERY RULES",  p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1)==0) {
		int affected_rows=sqlite3_flush_query_rules_db_to_mem();
	    pkt *ok=mypkt_alloc(sess);
		myproto_ok_pkt(ok,1,affected_rows,0,2,0);
		g_ptr_array_add(sess->client_myds->output.pkts, ok);
		return;
	}
	if (strncasecmp("FLUSH HOSTGROUPS",  p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1)==0) {
		int affected_rows=sqlite3_flush_servers_db_to_mem(0);
		int warnings=force_remove_servers();
		if ( affected_rows>=0 ) {
		    pkt *ok=mypkt_alloc(sess);
			myproto_ok_pkt(ok,1,affected_rows,0,2,warnings);
			g_ptr_array_add(sess->client_myds->output.pkts, ok);
		} else {
			// TODO: send some error
		}
		return;
	}
//	if (strncasecmp("REMOVE SERVERS",  p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1)==0) {
//	}
	if (strncmp("select @@version_comment limit 1", p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1)==0) {
		// mysql client in interactive mode sends "select @@version_comment limit 1" : we treat this as a special case

		// drop the packet from client
		mypkt_free(p,sess,1);

		// prepare a new packet to send to the client
		pkt *np=NULL;
		np=admin_version_comment_pkt(sess);
		g_ptr_array_add(sess->client_myds->output.pkts, np);
		return;
	}
	if (strncasecmp("SHUTDOWN",  p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1)==0) {
		glovars.shutdown=1;
	    pkt *ok=mypkt_alloc(sess);
		myproto_ok_pkt(ok,1,0,0,2,0);
		g_ptr_array_add(sess->client_myds->output.pkts, ok);
		return;
	}
	if (strncasecmp("DUMP RUNTIME HOSTGROUPS",  p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1)==0) {
		int affected_rows=sqlite3_dump_runtime_hostgroups();
	    pkt *ok=mypkt_alloc(sess);
		myproto_ok_pkt(ok,1,affected_rows,0,2,0);
		g_ptr_array_add(sess->client_myds->output.pkts, ok);
		return;
	}
	if (strncasecmp("DUMP RUNTIME QUERY RULES",  p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1)==0) {
		int affected_rows=sqlite3_dump_runtime_query_rules();
	    pkt *ok=mypkt_alloc(sess);
		myproto_ok_pkt(ok,1,affected_rows,0,2,0);
		g_ptr_array_add(sess->client_myds->output.pkts, ok);
		return;
	}
	if (strncasecmp("DUMP RUNTIME QUERY CACHE",  p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1)==0) {
		int affected_rows=sqlite3_dump_runtime_query_cache();
	    pkt *ok=mypkt_alloc(sess);
		myproto_ok_pkt(ok,1,affected_rows,0,2,0);
		g_ptr_array_add(sess->client_myds->output.pkts, ok);
		return;
	}
	rc=mysql_pkt_to_sqlite_exec(p, sess);
	mypkt_free(p,sess,1);

	if (rc==-1) {
		sess->healthy=0;
	}
//	sess->healthy=0; // for now, always
	return;
}


int force_remove_servers() { 
	int i;
	int warnings=0;
	// temporary change poll_timeout
	int default_mysql_poll_timeout=glovars.mysql_poll_timeout;
	glovars.mysql_poll_timeout=glovars.mysql_poll_timeout_maintenance;
	for (i=0; i<glovars.mysql_threads; i++) {
		gpointer admincmd=g_malloc0(20);
		sprintf(admincmd,"%s", "REMOVE SERVER");
		proxy_debug(PROXY_DEBUG_IPC, 3, "Sending REMOVE SERVER to thread #%d\n", i);
		g_async_queue_push(proxyipc.queue[i],admincmd);
	}
	char c;
	for (i=0; i<glovars.mysql_threads; i++) {
		proxy_debug(PROXY_DEBUG_IPC, 4, "Writing 1 bytes to thread #%d on fd %d\n", i, proxyipc.fdOut[i]);
		int r=write(proxyipc.fdOut[i],&c,sizeof(char));
	}
	for (i=0; i<glovars.mysql_threads; i++) {
		gpointer ack;
		proxy_debug(PROXY_DEBUG_IPC, 4, "Waiting ACK on thread #%d\n", i);
		ack=g_async_queue_pop(proxyipc.queue[glovars.mysql_threads]);
		int w=atoi(ack);
		warnings+=w;
		g_free(ack);
	}
	// we are done, all threads disabled the removed hosts!

	// reconfigure the correct poll() timeout
	default_mysql_poll_timeout=glovars.mysql_poll_timeout=default_mysql_poll_timeout;
//		// send OK pkt
//		pkt *ok=mypkt_alloc(sess);
//		myproto_ok_pkt(ok,1,0,0,2,0);
//		g_ptr_array_add(sess->client_myds->output.pkts, ok);
	return warnings;
}


void reset_query_rule(query_rule_t *qr) {
	if (qr->regex) {
		g_regex_unref(qr->regex);
	}
	if (qr->username) {
		g_free(qr->username);
	}
	if (qr->schemaname) {
		g_free(qr->schemaname);
	}
	if (qr->match_pattern) {
		g_free(qr->match_pattern);
	}
	if (qr->replace_pattern) {
		g_free(qr->replace_pattern);
	}
	if (qr->invalidate_cache_pattern) {
		g_free(qr->invalidate_cache_pattern);
	}
	g_slice_free1(sizeof(query_rule_t), qr);
}

void reset_query_rules() {
	proxy_debug(PROXY_DEBUG_QUERY_CACHE, 5, "Resetting query rules\n");
	if (gloQR.query_rules == NULL) {
		proxy_debug(PROXY_DEBUG_QUERY_CACHE, 5, "Initializing query rules\n");
		gloQR.query_rules=g_ptr_array_new();
		return;
	}
	proxy_debug(PROXY_DEBUG_QUERY_CACHE, 5, "%d query rules to reset\n", gloQR.query_rules->len);
	while ( gloQR.query_rules->len ) {
		query_rule_t *qr = g_ptr_array_remove_index_fast(gloQR.query_rules,0);
		reset_query_rule(qr);
	}
}

inline void init_gloQR() {
	pthread_rwlock_init(&gloQR.rwlock, NULL);
	gloQR.query_rules=NULL;
	reset_query_rules();
}


void init_query_metadata(mysql_session_t *sess, pkt *p) {
	sess->query_info.p=p;
	if (sess->query_info.query_checksum) {
		g_checksum_free(sess->query_info.query_checksum);
		sess->query_info.query_checksum=NULL;
	}
	sess->query_info.flagOUT=0;
	sess->query_info.rewritten=0;
	sess->query_info.cache_ttl=0;
	sess->query_info.destination_hostgroup=0;
	sess->query_info.audit_log=0;
	sess->query_info.performance_log=0;
	sess->query_info.mysql_query_cache_hit=0;
	if (p) {
		sess->query_info.query=p->data+sizeof(mysql_hdr)+1;
		sess->query_info.query_len=p->length-sizeof(mysql_hdr)-1;	
	} else {
		sess->query_info.query=NULL;
		sess->query_info.query_len=0;
	}
}

void process_query_rules(mysql_session_t *sess) {
	int i;
	int flagIN=0;
	gboolean rc;
	GMatchInfo *match_info;
	pthread_rwlock_rdlock(&gloQR.rwlock);
	for (i=0;i<gloQR.query_rules->len;i++) {
		query_rule_t *qr=g_ptr_array_index(gloQR.query_rules, i);
		proxy_debug(PROXY_DEBUG_QUERY_CACHE, 6, "Processing rule %d\n", qr->rule_id);
		if (qr->flagIN != flagIN) {
			proxy_debug(PROXY_DEBUG_QUERY_CACHE, 5, "query rule %d has no matching flagIN\n", qr->rule_id);
			continue;
		}
		if (qr->username) {
			if (strcmp(qr->username,sess->mysql_username)!=0) {
				proxy_debug(PROXY_DEBUG_QUERY_CACHE, 5, "query rule %d has no matching username\n", qr->rule_id);
				continue;
			}
		}
		if (qr->schemaname) {
			if (strcmp(qr->schemaname,sess->mysql_schema_cur)!=0) {
				proxy_debug(PROXY_DEBUG_QUERY_CACHE, 5, "query rule %d has no matching schema\n", qr->rule_id);
				continue;
			}
		}
		rc = g_regex_match_full (qr->regex, sess->query_info.query , sess->query_info.query_len, 0, 0, &match_info, NULL);
		if (
			(rc==TRUE && qr->negate_match_pattern==1) || ( rc==FALSE && qr->negate_match_pattern==0 )
		) {
			proxy_debug(PROXY_DEBUG_QUERY_CACHE, 5, "query rule %d has no matching pattern\n", qr->rule_id);
			g_match_info_free(match_info);
			continue;
		}
		// if we arrived here, we have a match
		__sync_fetch_and_add(&qr->hits,1);
		if (qr->replace_pattern) {
			proxy_debug(PROXY_DEBUG_QUERY_CACHE, 5, "query rule %d on match_pattern \"%s\" has a replace_pattern \"%s\" to apply\n", qr->rule_id, qr->match_pattern, qr->replace_pattern);
			GError *error=NULL;
			char *new_query;
			new_query=g_regex_replace(qr->regex, sess->query_info.query , sess->query_info.query_len, 0, qr->replace_pattern, 0, &error);
			if (error) {
				proxy_debug(PROXY_DEBUG_QUERY_CACHE, 3, "g_regex_replace() on query rule %d generated error %d\n", qr->rule_id, error->message);
				g_error_free(error);
				if (new_query) {
					g_free(new_query);
				}
				g_match_info_free(match_info);
				continue;
			}
			sess->query_info.rewritten=1;
			if (sess->query_info.query_checksum) { 
				g_checksum_free(sess->query_info.query_checksum); // remove checksum, as it may needs to be computed again
				sess->query_info.query_checksum=NULL;
			}
			mysql_new_payload_select(sess->query_info.p, new_query, -1);
			pkt *p=sess->query_info.p;
			sess->query_info.query=p->data+sizeof(mysql_hdr)+1;
			sess->query_info.query_len=p->length-sizeof(mysql_hdr)-1;
			g_free(new_query);
		}
		if (qr->flagOUT) {
			proxy_debug(PROXY_DEBUG_QUERY_CACHE, 5, "query rule %d has changed flagOUT\n", qr->rule_id);
			flagIN=qr->flagOUT;
			sess->query_info.flagOUT=flagIN;
		}
		if (qr->cache_ttl) {
			proxy_debug(PROXY_DEBUG_QUERY_CACHE, 5, "query rule %d has non-zero cache_ttl: %d. Query will%s hit the cache\n", qr->rule_id, qr->cache_ttl, (qr->cache_ttl < 0 ? " NOT" : "" ));
			sess->query_info.cache_ttl=qr->cache_ttl;
		}
		g_match_info_free(match_info);
		sess->query_info.destination_hostgroup=0; // default
		if (qr->destination_hostgroup>0) {
			proxy_debug(PROXY_DEBUG_QUERY_CACHE, 5, "query rule %d has changed destination_hostgroup %d\n", qr->rule_id, qr->destination_hostgroup);
			sess->query_info.destination_hostgroup=qr->destination_hostgroup;
		}
		if (qr->audit_log==1) {
			proxy_debug(PROXY_DEBUG_QUERY_CACHE, 5, "query rule %d has set audit_log\n", qr->rule_id);
			sess->query_info.audit_log=qr->audit_log;
		}
		if (qr->performance_log==1) {
			proxy_debug(PROXY_DEBUG_QUERY_CACHE, 5, "query rule %d has set performance_log\n", qr->rule_id);
			sess->query_info.performance_log=qr->performance_log;
		}
		if (sess->query_info.cache_ttl) {
			goto exit_process_query_rules;
		}
	}
	exit_process_query_rules:
	proxy_debug(PROXY_DEBUG_QUERY_CACHE, 6, "End processing query rules\n");
	pthread_rwlock_unlock(&gloQR.rwlock);
	// if the query reached this point with cache_ttl==0 , we set it to the default
	if (sess->query_info.cache_ttl==0) {
		proxy_debug(PROXY_DEBUG_QUERY_CACHE, 6, "Query has no caching TTL, setting the default\n");
		sess->query_info.cache_ttl=glovars.mysql_query_cache_default_timeout;
	}
	// if the query reached this point with cache_ttl==-1 , we set it to 0
	if (sess->query_info.cache_ttl==-1) {
		proxy_debug(PROXY_DEBUG_QUERY_CACHE, 6, "Query won't be cached\n");
		sess->query_info.cache_ttl=0;
	}
	// if the query is flagged to be cached but mysql_query_cache_enabled=0 , the query needs to be flagged to NOT be cached
	if (glovars.mysql_query_cache_enabled==FALSE) {
		sess->query_info.cache_ttl=0;
	}
}


mysql_server * find_server_ptr(const char *address, const uint16_t port) {
	mysql_server *ms=NULL;
	int i;
//	if (lock) pthread_rwlock_wrlock(&glomysrvs.rwlock);
	for (i=0;i<glomysrvs.servers->len;i++) {
		mysql_server *mst=g_ptr_array_index(glomysrvs.servers,i);
		if (mst->port==port && (strcmp(mst->address,address)==0)) {
			proxy_debug(PROXY_DEBUG_MYSQL_SERVER, 6, "MySQL server %s:%d found in servers list\n", address, port);
			i=glomysrvs.servers->len;
			ms=mst;
		}
	}
//	if (lock) pthread_rwlock_unlock(&glomysrvs.rwlock);
	return ms;
}


mysql_server * mysql_server_entry_create(const char *address, const uint16_t port, int read_only, enum mysql_server_status status) {
	mysql_server *ms=g_slice_alloc0(sizeof(mysql_server));
	ms->address=g_strdup(address);
	ms->port=port;
	ms->read_only=read_only;
	ms->status=status;
	return ms;
}

inline void mysql_server_entry_add(mysql_server *ms) {
	g_ptr_array_add(glomysrvs.servers,ms);
}

void mysql_server_entry_add_hostgroup(mysql_server *ms, int hostgroup_id) {
	if (hostgroup_id >= glovars.mysql_hostgroups) {
		proxy_debug(PROXY_DEBUG_MYSQL_SERVER, 4, "Server %s:%d not inserted in hostgroup %d as this is an invalid hostgroup\n", ms->address, ms->port, hostgroup_id);
		return;
	}
	GPtrArray *hg=g_ptr_array_index(glomysrvs.mysql_hostgroups, hostgroup_id);
	proxy_debug(PROXY_DEBUG_MYSQL_SERVER, 5, "Adding server %s:%d in hostgroup %d\n", ms->address, ms->port, hostgroup_id);
	g_ptr_array_add(hg,ms);	
}

mysql_server * mysql_server_random_entry_from_hostgroup__lock(int hostgroup_id) {
	mysql_server *ms;
	pthread_rwlock_wrlock(&glomysrvs.rwlock);
	ms=mysql_server_random_entry_from_hostgroup__nolock(hostgroup_id);
	pthread_rwlock_unlock(&glomysrvs.rwlock);
	return ms;
}

mysql_server * mysql_server_random_entry_from_hostgroup__nolock(int hostgroup_id) {
	assert(hostgroup_id < glovars.mysql_hostgroups);
	GPtrArray *hg=g_ptr_array_index(glomysrvs.mysql_hostgroups, hostgroup_id);
	if (hg->len==0) {
		proxy_debug(PROXY_DEBUG_MYSQL_SERVER, 3, "Server not found from hostgroup %d\n", hostgroup_id);
		return NULL;
	}
	int i=rand()%hg->len;
	mysql_server *ms;
	ms=g_ptr_array_index(hg,i);
	proxy_debug(PROXY_DEBUG_MYSQL_SERVER, 5, "Returning server %s:%d from hostgroup %d\n", ms->address, ms->port, hostgroup_id);
	return ms;
}

int	mysql_session_create_backend_for_hostgroup(mysql_session_t *sess, int hostgroup_id) {
	assert(hostgroup_id < glovars.mysql_hostgroups);
	mysql_server *ms=NULL;
	ms=mysql_server_random_entry_from_hostgroup__lock(hostgroup_id);
	mysql_backend_t *mybe=g_ptr_array_index(sess->mybes,hostgroup_id);
	mybe->server_ptr=ms;
	if (ms==NULL) {
		// this is a severe condition, needs to be handled
		return 0;
	}
	mybe->server_mycpe=mysql_connpool_get_connection(&gloconnpool, mybe->server_ptr->address, sess->mysql_username, sess->mysql_password, sess->mysql_schema_cur, mybe->server_ptr->port);
	if (mybe->server_mycpe==NULL) {
		// handle error!!
		authenticate_mysql_client_send_ERR(sess, 1045, "#28000Access denied for user");
		// this is a severe condition, needs to be handled
		return -1;
	}
	mybe->fd=mybe->server_mycpe->conn->net.fd;
	//mybe->server_myds=mysql_data_stream_init(mybe->fd, sess);
	mybe->server_myds=mysql_data_stream_new(mybe->fd, sess);
	proxy_debug(PROXY_DEBUG_MYSQL_CONNECTION, 4, "Created new connection for sess %p , hostgroup %d , fd %d\n", sess , hostgroup_id , mybe->fd);
	return 1;
}
