#include "proxysql.h"


void mysql_pkt_err_from_sqlite(pkt *p, const char *s) {
	int l=strlen(s)+1+6;
	char *b=malloc(l);
	//if (b==NULL) { exit(EXIT_FAILURE); }
	assert(b!=NULL);
	b[l-1]='\0';
	// because we don't know the error from SQL we send ER_UNKNOWN_COM_ERROR
	sprintf(b,"%s%s","#08S01",s); 
	create_err_packet(p, 1, 1047, b);
	free(b);
}

int mysql_pkt_to_sqlite_exec(pkt *p, mysql_session_t *sess) {
//	sqlite3 *db;
	sqlite3 *db=sqlite3configdb;
	int rc;
	sqlite3_stmt *statement;
	void *query=p->data+sizeof(mysql_hdr)+1;
	int length=p->length-sizeof(mysql_hdr)-1;
	char *query_copy=NULL;
	query_copy=malloc(length+1);
//	if (query_copy==NULL) { exit(EXIT_FAILURE); }
	assert(query_copy!=NULL);
	query_copy[length]='\0';
	memcpy(query_copy,query,length);
	proxy_debug(PROXY_DEBUG_SQLITE, 6, "SQLITE: running query \"%s\"\n", query_copy);
	if(sqlite3_prepare_v2(db, query_copy, -1, &statement, 0) != SQLITE_OK) {
		pkt *ep=mypkt_alloc(sess);
		mysql_pkt_err_from_sqlite(ep,sqlite3_errmsg(db));
		g_ptr_array_add(sess->client_myds->output.pkts, ep);
		proxy_debug(PROXY_DEBUG_SQLITE, 1, "SQLITE: Error on sqlite3_prepare_v2() running query \"%s\" : %s\n", query_copy, sqlite3_errmsg(db));
		free(query_copy);
		return 0;	
	}
	int cols = sqlite3_column_count(statement);
	if (cols==0) {
		// not a SELECT
		proxy_debug(PROXY_DEBUG_SQLITE, 6, "SQLITE: not a SELECT\n");
		int rc;
		pkt *p=mypkt_alloc(sess);
		rc=sqlite3_step(statement);
		if (rc==SQLITE_DONE) {
			int affected_rows=sqlite3_changes(db);
			proxy_debug(PROXY_DEBUG_SQLITE, 6, "SQLITE: %d rows affected\n", affected_rows);
			myproto_ok_pkt(p,1,affected_rows,0,2,0);
		} else {
			mysql_pkt_err_from_sqlite(p,sqlite3_errmsg(db));
			proxy_debug(PROXY_DEBUG_SQLITE, 1, "SQLITE: Error on sqlite3_step() running query \"%s\" : %s\n", query_copy, sqlite3_errmsg(db));
		}
		g_ptr_array_add(sess->client_myds->output.pkts, p);
		sqlite3_finalize(statement);
		free(query_copy);
		return 0;
	}
	{
		pkt *p=mypkt_alloc(sess);
		myproto_column_count(p,1,cols);
		g_ptr_array_add(sess->client_myds->output.pkts, p);	
	}
	int col;
	for(col = 0; col < cols; col++) {
		// add empty spaces
		pkt *p=NULL;
		g_ptr_array_add(sess->client_myds->output.pkts, p);
	}
	{
		pkt *p=mypkt_alloc(sess);
		myproto_eof(p,2+cols,0,34);
		g_ptr_array_add(sess->client_myds->output.pkts, p);
	}
	int result = 0;
	int rownum = 0;
	int *maxcolsizes=g_slice_alloc0(sizeof(int)*cols);
	while ((result=sqlite3_step(statement))==SQLITE_ROW) {
		char **row=g_slice_alloc0(sizeof(char *)*cols);
		int *len=g_slice_alloc0(sizeof(int)*cols);
		int rowlen=0;
		proxy_debug(PROXY_DEBUG_SQLITE, 6, "SQLite row %d :", rownum);
		for(col = 0; col < cols; col++) {
			int t=sqlite3_column_type(statement, col);
			row[col]=(char *)sqlite3_column_text(statement, col);
			int l=sqlite3_column_bytes(statement, col);
			if (l==0) { // NULL
				l=1;
			} else {
				l+=lencint(l);
			}
			rowlen+=l;
			proxy_debug(PROXY_DEBUG_SQLITE, 6, "Col%d (%d,%d,%s) ", col, t, l, row[col]);
		}
		pkt *p=mypkt_alloc(sess);
		p->length=sizeof(mysql_hdr)+rowlen;
		p->data=g_slice_alloc(p->length);
		mysql_hdr hdr;
		hdr.pkt_length=rowlen;
		hdr.pkt_id=cols+3+rownum;
		memcpy(p->data,&hdr,sizeof(mysql_hdr));
		int i=sizeof(mysql_hdr);
		for(col = 0; col < cols; col++) {
			row[col]=(char *)sqlite3_column_text(statement, col);
			int l=sqlite3_column_bytes(statement, col);
			i+=writeencstrnull(p->data+i,row[col]);
		}
		proxy_debug(PROXY_DEBUG_SQLITE, 6, ". %d cols , %d bytes\n", cols, rowlen);
		g_slice_free1(sizeof(char *)*cols,row);
		g_slice_free1(sizeof(int)*cols,len);
		rownum++;
		g_ptr_array_add(sess->client_myds->output.pkts, p);
	}
	for(col = 0; col < cols; col++) {
		pkt *p=mypkt_alloc(sess);
		const char *s=sqlite3_column_name(statement, col);
		myproto_column_def(p, col+2, "", "", "", s, "", 100, MYSQL_TYPE_VARCHAR, 0, 0);
		// this is a trick: insert at the end, and remove fast from the position we want to insert
		g_ptr_array_add(sess->client_myds->output.pkts, p);
		g_ptr_array_remove_index_fast(sess->client_myds->output.pkts,col+1);
	}
	{
		pkt *p=mypkt_alloc(sess);
		myproto_eof(p,3+cols+rownum,0,34);
		g_ptr_array_add(sess->client_myds->output.pkts, p);
	}
	
	g_slice_free1(sizeof(int)*cols,maxcolsizes);
	sqlite3_finalize(statement);
	free(query_copy);
	return 0;


}

void sqlite3_exec_exit_on_failure(sqlite3 *db, const char *str) {
	char *err=NULL;
	sqlite3_exec(db, str, NULL, 0, &err);
	if(err!=NULL) {
		proxy_debug(PROXY_DEBUG_SQLITE, 1, "SQLITE: Error on %s : %s\n",str, err); 
		//exit(EXIT_FAILURE);
		assert(err==NULL);
	}
}

void sqlite3_flush_debug_levels_mem_to_db(int replace) {
	int i;
	char *a=NULL;
//	if (delete) {
//	}
	sqlite3_exec_exit_on_failure(sqlite3configdb,"DELETE FROM debug_levels WHERE verbosity=0");
	if (replace) {
		a="REPLACE INTO debug_levels(module,verbosity) VALUES(\"%s\",%d)";
	} else {
		a="INSERT OR IGNORE INTO debug_levels(module,verbosity) VALUES(\"%s\",%d)";
	}
	int l=strlen(a)+100;
	for (i=0;i<PROXY_DEBUG_UNKNOWN;i++) {
		char *buff=g_malloc0(l);
		sprintf(buff,a, gdbg_lvl[i].name, gdbg_lvl[i].verbosity);
		proxy_debug(PROXY_DEBUG_SQLITE, 3, "SQLITE: %s\n",buff);
		sqlite3_exec_exit_on_failure(sqlite3configdb,buff);
		g_free(buff);
	}
}

int sqlite3_flush_debug_levels_db_to_mem() {
	int i;
	char *query="SELECT verbosity FROM debug_levels WHERE module=\"%s\"";
	int l=strlen(query)+100;
	int rownum=0;
	int result;
	for (i=0;i<PROXY_DEBUG_UNKNOWN;i++) {
		sqlite3_stmt *statement;
		char *buff=g_malloc0(l);
		sprintf(buff,query,gdbg_lvl[i].name);
		if(sqlite3_prepare_v2(sqlite3configdb, buff, -1, &statement, 0) != SQLITE_OK) {
			proxy_debug(PROXY_DEBUG_SQLITE, 1, "SQLITE: Error on sqlite3_prepare_v2() running query \"%s\" : %s\n", buff, sqlite3_errmsg(sqlite3configdb));
			sqlite3_finalize(statement);
			g_free(buff);
			return 0;
		}
		while ((result=sqlite3_step(statement))==SQLITE_ROW) {
			gdbg_lvl[i].verbosity=sqlite3_column_int(statement,0);
			rownum++;
		}
		sqlite3_finalize(statement);
		g_free(buff);
	}
	return rownum;
}

void sqlite3_flush_users_mem_to_db(int replace, int active) {
//	if (delete) {
//		sqlite3_exec_exit_on_failure(sqlite3configdb,"DELETE FROM users");
//	}
	char *a=NULL;
	if (replace) {
		a="REPLACE INTO users(username,password,active) VALUES(\"%s\",\"%s\",%d)";
	} else {
		a="INSERT OR IGNORE INTO users(username,password,active) VALUES(\"%s\",\"%s\",%d)";
	}
	int i;
	pthread_rwlock_rdlock(&glovars.rwlock_usernames);
	for (i=0;i<glovars.mysql_users_name->len;i++) {
		int l=strlen(a)+strlen(g_ptr_array_index(glovars.mysql_users_name,i))+strlen(g_ptr_array_index(glovars.mysql_users_pass,i));
		char *buff=g_malloc0(l);
		memset(buff,0,l);
		sprintf(buff,a, g_ptr_array_index(glovars.mysql_users_name,i), g_ptr_array_index(glovars.mysql_users_pass,i), active);
		proxy_debug(PROXY_DEBUG_SQLITE, 3, "SQLITE: %s\n",buff);
		sqlite3_exec_exit_on_failure(sqlite3configdb,buff);
		g_free(buff);
	}
	pthread_rwlock_unlock(&glovars.rwlock_usernames);
}


int sqlite3_flush_users_db_to_mem() {
	sqlite3_stmt *statement;
	char *query="SELECT username, password FROM users WHERE active=1";
	if(sqlite3_prepare_v2(sqlite3configdb, query, -1, &statement, 0) != SQLITE_OK) {
		proxy_debug(PROXY_DEBUG_SQLITE, 1, "SQLITE: Error on sqlite3_prepare_v2() running query \"%s\" : %s\n", query, sqlite3_errmsg(sqlite3configdb));
		sqlite3_finalize(statement);
		return 0;
	}
	pthread_rwlock_wrlock(&glovars.rwlock_usernames);
	// remove all users
	while (glovars.mysql_users_name->len) {
		char *p=g_ptr_array_remove_index_fast(glovars.mysql_users_name,0);
		g_hash_table_remove(glovars.usernames,p);
		proxy_debug(PROXY_DEBUG_MYSQL_AUTH, 6, "Removing user %s\n", p);
		g_free(p);
	}
	// remove all passwords
	while (glovars.mysql_users_pass->len) {
		char *p=g_ptr_array_remove_index_fast(glovars.mysql_users_pass,0);
		g_free(p);
	}
	int rownum = 0;
	int result = 0;
	while ((result=sqlite3_step(statement))==SQLITE_ROW) {
		gpointer user=g_strdup(sqlite3_column_text(statement,0));
		gpointer pass=g_strdup(sqlite3_column_text(statement,1));
		g_ptr_array_add(glovars.mysql_users_name,user);
		g_ptr_array_add(glovars.mysql_users_pass,pass);
		g_hash_table_insert(glovars.usernames, user, pass);
		proxy_debug(PROXY_DEBUG_MYSQL_AUTH, 6, "Adding user %s , password %s\n", user, pass);
		rownum++;
	}
	pthread_rwlock_unlock(&glovars.rwlock_usernames);
	sqlite3_finalize(statement);
	return rownum;
}


static admin_sqlite_table_def_t table_defs[] = 
//static table_defs[] =
{
	{ "server_status" , ADMIN_SQLITE_TABLE_SERVER_STATUS },
	{ "servers" , ADMIN_SQLITE_TABLE_SERVERS },
	{ "users" , ADMIN_SQLITE_TABLE_USERS },
	{ "global_variables" , ADMIN_SQLITE_TABLE_GLOBAL_VARIABLES },
	{ "debug_levels" , ADMIN_SQLITE_TABLE_DEBUG_LEVELS },
	{ "query_rules" , ADMIN_SQLITE_TABLE_QUERY_RULES }
};

void admin_init_sqlite3() {
	int i;
	char *s[4];
	s[0]="PRAGMA journal_mode = WAL";
	s[1]="PRAGMA synchronous = NORMAL";
	//s[2]="PRAGMA locking_mode = EXCLUSIVE";
	s[2]="PRAGMA locking_mode = NORMAL";
	s[3]="PRAGMA foreign_keys = ON";
//  proxy_debug(PROXY_DEBUG_SQLITE, 3, "SQLITE:     
//    sqlite3_exec_exit_on_failure(sqlite3configdb, "PRAGMA journal_mode = WAL");
//  pragma_exit_on_failure(sqlite3configdb, "PRAGMA journal_mode = OFF");
//    sqlite3_exec_exit_on_failure(sqlite3configdb, "PRAGMA synchronous = NORMAL");
//  pragma_exit_on_failure(sqlite3configdb, "PRAGMA synchronous = 0");
//    sqlite3_exec_exit_on_failure(sqlite3configdb, "PRAGMA locking_mode = EXCLUSIVE");
//    sqlite3_exec_exit_on_failure(sqlite3configdb, "PRAGMA foreign_keys = ON");
//  pragma_exit_on_failure(sqlite3configdb, "PRAGMA PRAGMA wal_autocheckpoint=10000");

	i = sqlite3_open_v2(SQLITE_ADMINDB, &sqlite3configdb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX , NULL);
	if(i){
		proxy_debug(PROXY_DEBUG_SQLITE, 1, "SQLITE: Error on sqlite3_open(): %s\n", sqlite3_errmsg(sqlite3configdb));
		//exit(EXIT_FAILURE);
		assert(i==0);
	}
	for (i=0;i<4;i++) {
		proxy_debug(PROXY_DEBUG_SQLITE, 3, "SQLITE: %s\n", s[i]);
		sqlite3_exec_exit_on_failure(sqlite3configdb, s[i]);
	}
	char *q1="SELECT COUNT(*) FROM sqlite_master WHERE type=\"table\" AND name=\"%s\" AND sql=\"%s\"";
	for (i=0;i<sizeof(table_defs)/sizeof(admin_sqlite_table_def_t);i++) {
		admin_sqlite_table_def_t *table_def=table_defs+i;
		proxy_debug(PROXY_DEBUG_SQLITE, 6, "SQLITE: checking definition of table %s against \"%s\"\n" , table_def->table_name , table_def->table_def);
		int l=strlen(q1)+strlen(table_def->table_name)+strlen(table_def->table_def)+1;
		sqlite3_stmt *statement;
		char *buff=g_malloc0(l);
		sprintf(buff, q1, table_def->table_name , table_def->table_def);
		if(sqlite3_prepare_v2(sqlite3configdb, buff, -1, &statement, 0) != SQLITE_OK) {
			proxy_debug(PROXY_DEBUG_SQLITE, 1, "SQLITE: Error on sqlite3_prepare_v2() running query \"%s\" : %s\n", buff, sqlite3_errmsg(sqlite3configdb));
			sqlite3_finalize(statement);
			g_free(buff);
			assert(0);
		}
		int count=0;
		int result=0;
		while ((result=sqlite3_step(statement))==SQLITE_ROW) {
			count+=sqlite3_column_int(statement,0);
		}
		sqlite3_finalize(statement);
		char *q2=g_malloc0(l);
		if (count==0) {
			proxy_debug(PROXY_DEBUG_SQLITE, 1, "SQLITE: Table %s does not exist or is corrupted. Creating!\n", table_def->table_name);
			char *q2="DROP TABLE IF EXISTS %s";
			l=strlen(q2)+strlen(table_def->table_name)+1;
			buff=g_malloc0(l);
			sprintf(buff,q2,table_def->table_name);
			proxy_debug(PROXY_DEBUG_SQLITE, 5, "SQLITE: dropping table: %s\n", buff);
			sqlite3_exec_exit_on_failure(sqlite3configdb, buff);
			g_free(buff);
			proxy_debug(PROXY_DEBUG_SQLITE, 5, "SQLITE: creating table: %s\n", table_def->table_def);
			sqlite3_exec_exit_on_failure(sqlite3configdb, table_def->table_def);
		}
	}
}

int sqlite3_flush_query_rules_db_to_mem() {
	int i;
	proxy_debug(PROXY_DEBUG_SQLITE, 1, "Loading query rules from db");
	sqlite3_stmt *statement;
	char *query="SELECT rule_id, flagIN, match_pattern, negate_match_pattern , flagOUT, replace_pattern, caching_ttl FROM query_rules ORDER BY rule_id";
	if(sqlite3_prepare_v2(sqlite3configdb, query, -1, &statement, 0) != SQLITE_OK) {
		proxy_debug(PROXY_DEBUG_SQLITE, 1, "SQLITE: Error on sqlite3_prepare_v2() running query \"%s\" : %s\n", query, sqlite3_errmsg(sqlite3configdb));
		sqlite3_finalize(statement);
		proxy_error("Error loading query rules");
		assert(0);
	}
	pthread_rwlock_wrlock(&gloQR.rwlock);
	// remove all QC rules
	reset_query_rules();
	int rownum = 0;
	int result = 0;
	while ((result=sqlite3_step(statement))==SQLITE_ROW) {
		query_rule_t *qr=g_slice_alloc0(sizeof(query_rule_t));
		qr->rule_id=sqlite3_column_int(statement,0);
		qr->flagIN=sqlite3_column_int(statement,1);
		//some sanity check
		if (qr->flagIN < 0) {
			proxy_error("Out of range value for flagIN (%d) on rule_id %d\n", qr->flagIN, qr->rule_id);
			qr->flagIN=0;
		}
		qr->match_pattern=g_strdup(sqlite3_column_text(statement,2));
		qr->negate_match_pattern=sqlite3_column_int(statement,3);
		//some sanity check
		if (qr->negate_match_pattern > 1) {
			proxy_error("Out of range value for negate_match_pattern (%d) on rule_id %d\n", qr->negate_match_pattern, qr->rule_id);
			qr->negate_match_pattern=1;
		}
		if (qr->negate_match_pattern < 0) {
			proxy_error("Out of range value for negate_match_pattern (%d) on rule_id %d\n", qr->negate_match_pattern, qr->rule_id);
			qr->negate_match_pattern=0;
		}
		qr->flagOUT=sqlite3_column_int(statement,4);
		//some sanity check
		if (qr->flagOUT < 0) {
			proxy_error("Out of range value for flagOUT (%d) on rule_id %d\n", qr->flagOUT, qr->rule_id);
			qr->flagOUT=0;
		}
		qr->replace_pattern=g_strdup(sqlite3_column_text(statement,5));
		qr->caching_ttl=sqlite3_column_int(statement,6);
		//some sanity check
		if (qr->caching_ttl < -1) {
			proxy_error("Out of range value for caching_ttl (%d) on rule_id %d\n", qr->caching_ttl, qr->rule_id);
			qr->caching_ttl=-1;
		}
		proxy_debug(PROXY_DEBUG_QUERY_CACHE, 4, "Adding query rules with id %d : flagIN %d ; match_pattern \"%s\" ; negate_match_pattern %d ; flagOUT %d ; replace_pattern \"%s\" ; caching_ttl %d\n", qr->rule_id, qr->flagIN , qr->match_pattern , qr->negate_match_pattern , qr->flagOUT , qr->replace_pattern , qr->caching_ttl);
		qr->regex=g_regex_new(qr->match_pattern, G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0, NULL);
		g_ptr_array_add(gloQR.query_rules, qr);
		rownum++;
	}
	pthread_rwlock_unlock(&gloQR.rwlock);
	sqlite3_finalize(statement);
	return rownum;
};
