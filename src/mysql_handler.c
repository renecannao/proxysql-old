#include "proxysql.h"


void mysql_session_init(mysql_session_t *sess, proxy_mysql_thread_t *handler_thread) {
	// register the connection
	pthread_rwlock_wrlock(&glomysrvs.rwlock);
	g_ptr_array_add(glomysrvs.mysql_connections, sess);
	glomysrvs.mysql_connections_cur+=1;
	pthread_rwlock_unlock(&glomysrvs.rwlock);
	// generic initalization
	sess->server_ptr=NULL;
	sess->master_ptr=NULL;
	sess->slave_ptr=NULL;
	sess->server_myds=NULL;
	sess->master_myds=NULL;
	sess->slave_myds=NULL;
	sess->server_mycpe=NULL;
	sess->master_mycpe=NULL;
	sess->slave_mycpe=NULL;
	sess->mysql_username=NULL;
	sess->mysql_password=NULL;
	sess->mysql_schema_cur=NULL;
	sess->mysql_schema_new=NULL;
	sess->server_bytes_at_cmd.bytes_sent=0;
	sess->server_bytes_at_cmd.bytes_recv=0;
	sess->mysql_server_reconnect=TRUE;
	sess->healthy=1;
	sess->admin=0;
	sess->resultset=g_ptr_array_new();
	sess->timers=calloc(sizeof(timer),TOTAL_TIMERS);
	sess->handler_thread=handler_thread;
	sess->client_myds=mysql_data_stream_init(sess->client_fd, sess);
	sess->client_myds->fd=sess->client_fd;
	sess->fds[0].fd=sess->client_myds->fd;
	sess->fds[0].events=POLLIN|POLLOUT;
	sess->nfds=1;
	sess->query_to_cache=FALSE;
	sess->client_command=COM_END;   // always reset this
	sess->send_to_slave=FALSE;
	memset(&sess->query_info,0,sizeof(mysql_query_metadata_t));
}


void mysql_session_close(mysql_session_t *sess) {
	proxy_debug(PROXY_DEBUG_MYSQL_CONNECTION, 4, "Closing connection on client fd %d (myds %d , sess %p)\n", sess->client_fd, sess->client_myds->fd, sess);
	mysql_data_stream_close(sess->client_myds);
	if (sess->client_myds->fd) { mysql_data_stream_shut_hard(sess->client_myds); }
	if (sess->master_myds) {
		if (sess->master_mycpe) {
			if (sess->master_mycpe->conn) {
				mysql_connpool_detach_connection(&gloconnpool, sess->master_mycpe);
			}
		}
		mysql_data_stream_close(sess->master_myds);
	}
	if (sess->slave_myds) {
		if (sess->slave_mycpe) {
			if (sess->slave_mycpe->conn) {
				mysql_connpool_detach_connection(&gloconnpool, sess->slave_mycpe);
			}
		}
		mysql_data_stream_close(sess->slave_myds);
	}
	while (sess->resultset->len) {
		pkt *p;
		p=g_ptr_array_remove_index(sess->resultset, 0);
		mypkt_free(p,sess,1);
	}	
	g_ptr_array_free(sess->resultset,TRUE);
	free(sess->timers);

	if (sess->mysql_username) { free(sess->mysql_username); sess->mysql_username=NULL; }
	if (sess->mysql_password) { free(sess->mysql_password); sess->mysql_password=NULL; }
	if (sess->mysql_schema_cur) { free(sess->mysql_schema_cur); sess->mysql_schema_cur=NULL; }
	if (sess->mysql_schema_new) { free(sess->mysql_schema_new); sess->mysql_schema_new=NULL; }

	sess->healthy=0;
	init_query_metadata(sess, NULL);
	//free(sess);
	//mysql_con->net.fd=mysql_fd;
	//if (mysql_con) { mysql_close(mysql_con); }
//  mysql_close(mysql_con); <== don't call mysql_close, as the connection is already closed by the connector logic
	// this needs to be fixed. Connectors shouldn't close a server connection . Shutdown should be moved here
//	mysql_thread_end();

	// unregister the connection
	pthread_rwlock_wrlock(&glomysrvs.rwlock);
	g_ptr_array_remove_fast(glomysrvs.mysql_connections, sess);
	glomysrvs.mysql_connections_cur-=1;
	pthread_rwlock_unlock(&glomysrvs.rwlock);
}


inline void client_COM_QUIT(mysql_session_t *sess) {
	proxy_debug(PROXY_DEBUG_MYSQL_COM, 5, "Got COM_QUIT packet\n");
}

inline void client_COM_STATISTICS(mysql_session_t *sess) {
	proxy_debug(PROXY_DEBUG_MYSQL_COM, 5, "Got COM_STATISTICS packet\n");
	if (sess->admin==1) {
	// we shouldn't forward this if we are in admin mode
		sess->healthy=0;
		return;
	}
}

inline void client_COM_INIT_DB(mysql_session_t *sess, pkt *p) {
	if (sess->admin==1) {
	// we shouldn't forward this if we are in admin mode
		sess->healthy=0;
		return;
	}
	sess->mysql_schema_new=calloc(1,p->length-sizeof(mysql_hdr));
	memcpy(sess->mysql_schema_new, p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1);
	proxy_debug(PROXY_DEBUG_MYSQL_COM, 5, "Got COM_INIT_DB packet for schema %s\n", sess->mysql_schema_new);
	if ((sess->mysql_schema_cur) && (strcmp(sess->mysql_schema_new, sess->mysql_schema_cur)==0)) {
		// already on target schema, don't forward
		sess->mysql_query_cache_hit=TRUE;
		// destroy the client's packet
		g_slice_free1(p->length, p->data);
		//--- g_slice_free1(sizeof(pkt), p);
		// create OK packet ...
		create_ok_packet(p,1);
		// .. end send it to client
		g_ptr_array_add(sess->client_myds->output.pkts, p);		
		// reset conn->mysql_schema_new	
		free(sess->mysql_schema_new);
		sess->mysql_schema_new=NULL;
	} else {
		// disconnect the slave
		if (sess->slave_myds) {
			if (sess->slave_mycpe) {
				if (sess->slave_mycpe->conn) {
					mysql_connpool_detach_connection(&gloconnpool, sess->slave_mycpe);
				}
			}
			mysql_data_stream_close(sess->slave_myds);
			sess->slave_fd=0;
			sess->slave_ptr=NULL;
			sess->slave_myds=NULL;
			sess->slave_mycpe=NULL;
		}
	}
}


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
	if (strncasecmp("REMOVE SERVER",  p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1)==0) {
		mysql_server *ms=new_server_master();
		// TESTING : BEGIN
		int i;
		for (i=0; i<glovars.mysql_threads; i++) {
			gpointer admincmd=g_malloc0(20);
			sprintf(admincmd,"%s", "REMOVE SERVER");
			fprintf(stderr,"Sending %s to %d\n", "REMOVE SERVER", i);
			g_async_queue_push(proxyipc.queue[i],admincmd);
			fprintf(stderr,"Sent %s to %d\n", "REMOVE SERVER", i);
			g_async_queue_push(proxyipc.queue[i],ms);
			fprintf(stderr,"Sent %p to %d\n", ms, i);
		}
		char c;
		for (i=0; i<glovars.mysql_threads; i++) {
			fprintf(stderr,"Writing 1 bytes to thread %d on fd %d\n", i, proxyipc.fdOut[i]);
			int r=write(proxyipc.fdOut[i],&c,sizeof(char));
		}
		for (i=0; i<glovars.mysql_threads; i++) {
			gpointer ack;
			fprintf(stderr,"Waiting ACK # %d\n", i);
			ack=g_async_queue_pop(proxyipc.queue[glovars.mysql_threads]);
			g_free(ack);
		}
	    pkt *ok=mypkt_alloc(sess);
		myproto_ok_pkt(ok,1,0,0,2,0);
		g_ptr_array_add(sess->client_myds->output.pkts, ok);
		// TESTING : END
		return;
	}
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
	rc=mysql_pkt_to_sqlite_exec(p, sess);
	mypkt_free(p,sess,1);

	if (rc==-1) {
		sess->healthy=0;
	}
//	sess->healthy=0; // for now, always
	return;
}



inline void compute_query_checksum(mysql_session_t *sess) {
	if (sess->query_info.query_checksum==NULL) {
		sess->query_info.query_checksum=g_checksum_new(G_CHECKSUM_MD5);
		g_checksum_update(sess->query_info.query_checksum, sess->query_info.query, sess->query_info.query_len);
		g_checksum_update(sess->query_info.query_checksum, sess->mysql_username, strlen(sess->mysql_username));
		g_checksum_update(sess->query_info.query_checksum, sess->mysql_schema_cur, strlen(sess->mysql_schema_cur));
	}
}

inline void client_COM_QUERY(mysql_session_t *sess, pkt *p) {

	if (mysql_pkt_get_size(p) > glovars.mysql_max_query_size) {
		// the packet is too big. Ignore any processing
		sess->client_command=COM_END;
	} else {
		init_query_metadata(sess, p);
		sess->resultset_progress=RESULTSET_WAITING;
		sess->resultset_size=0;

/*
if the query is cached:
	destroy the pkg
	get the packets from the cache and send it to the client

if the query is not cached, mark it as to be cached and modify the code on result set

*/
		if (glovars.mysql_query_cache_enabled && glovars.mysql_query_cache_precheck) {
			compute_query_checksum(sess);
			pkt *QCresult=NULL;
			QCresult=fdb_get(&QC, g_checksum_get_string(sess->query_info.query_checksum), sess);
			proxy_debug(PROXY_DEBUG_QUERY_CACHE, 4, "Called GET on QC for checksum %s in precheck\n", g_checksum_get_string(sess->query_info.query_checksum));
			if (QCresult) {
				proxy_debug(PROXY_DEBUG_QUERY_CACHE, 4, "Found QC entry for checksum %s in precheck\n", g_checksum_get_string(sess->query_info.query_checksum));
				g_ptr_array_add(sess->client_myds->output.pkts, QCresult);
				sess->mysql_query_cache_hit=TRUE;
				sess->query_to_cache=FALSE;	 // query already in cache
				mypkt_free(p,sess,1);
				return;
			}			
		}
		process_query_rules(sess);
		if (
			(sess->client_command==COM_QUERY) &&
			( sess->query_info.cache_ttl > 0 )
		) {
			sess->query_to_cache=TRUE;	  // cache the query
			compute_query_checksum(sess);
			pkt *QCresult=NULL;
			QCresult=fdb_get(&QC, g_checksum_get_string(sess->query_info.query_checksum), sess);
			proxy_debug(PROXY_DEBUG_QUERY_CACHE, 4, "Called GET on QC for checksum %s after query preprocessing\n", g_checksum_get_string(sess->query_info.query_checksum));
			if (QCresult) {
				proxy_debug(PROXY_DEBUG_QUERY_CACHE, 4, "Found QC entry for checksum %s after query preprocessing\n", g_checksum_get_string(sess->query_info.query_checksum));
				g_ptr_array_add(sess->client_myds->output.pkts, QCresult);
				sess->mysql_query_cache_hit=TRUE;
				sess->query_to_cache=FALSE;	 // query already in cache
				mypkt_free(p,sess,1);
			} else {
				proxy_debug(PROXY_DEBUG_QUERY_CACHE, 4, "Not found QC entry for checksum %s after query prepocessing\n", g_checksum_get_string(sess->query_info.query_checksum));
				sess->send_to_slave=TRUE;
				if (sess->slave_ptr==NULL) {
					// no slave assigned yet, find one!
					sess->slave_ptr=new_server_slave();
					if (sess->slave_ptr==NULL) {
						// handle error!!
						sess->healthy=0;
						return ;
					}
					sess->slave_mycpe=mysql_connpool_get_connection(&gloconnpool, sess->slave_ptr->address, sess->mysql_username, sess->mysql_password, sess->mysql_schema_cur, sess->slave_ptr->port);
					if (sess->slave_mycpe==NULL) {
						// handle error!!
						sess->healthy=0;
						return ;
					}
					sess->slave_fd=sess->slave_mycpe->conn->net.fd;
					sess->slave_myds=mysql_data_stream_init(sess->slave_fd , sess);
					proxy_debug(PROXY_DEBUG_MYSQL_CONNECTION, 4, "Created new slave_connection fd: %d\n", sess->slave_fd);
				}	
				sess->server_myds=sess->slave_myds;
				sess->server_fd=sess->slave_fd;
				sess->server_mycpe=sess->slave_mycpe;
				sess->server_ptr=sess->slave_ptr;
				sess->server_bytes_at_cmd.bytes_sent=sess->server_myds->bytes_info.bytes_sent;
				sess->server_bytes_at_cmd.bytes_recv=sess->server_myds->bytes_info.bytes_recv;
			}
		}
//				  conn->status &= ~CONNECTION_READING_CLIENT; // NOTE: this is not true for packets >= 16MB , be careful
	}
}

inline void server_COM_QUIT(mysql_session_t *sess, pkt *p, enum MySQL_response_type r) {
	if (r==OK_Packet) {
		proxy_debug(PROXY_DEBUG_MYSQL_COM, 5, "Got OK on COM_QUIT\n");
#ifdef DEBUG_COM
#endif
		g_ptr_array_add(sess->client_myds->output.pkts, p);
	}
}
inline void server_COM_STATISTICS(mysql_session_t *sess, pkt *p) {
	proxy_debug(PROXY_DEBUG_MYSQL_COM, 5, "Got packet on COM_STATISTICS\n");
	g_ptr_array_add(sess->client_myds->output.pkts, p);
	// sync for auto-reconnect
	sess->server_bytes_at_cmd.bytes_sent=sess->server_myds->bytes_info.bytes_sent;
	sess->server_bytes_at_cmd.bytes_recv=sess->server_myds->bytes_info.bytes_recv;
}

inline void server_COM_INIT_DB(mysql_session_t *sess, pkt *p, enum MySQL_response_type r) {
	if (r==OK_Packet) {
		proxy_debug(PROXY_DEBUG_MYSQL_COM, 5, "Got OK on COM_INIT_DB for schema %s\n", sess->mysql_schema_new);
		if (sess->mysql_schema_cur) {
			free(sess->mysql_schema_cur);
			sess->mysql_schema_cur=strdup(sess->mysql_schema_new);
		}
		sess->mysql_schema_cur=g_strdup(sess->mysql_schema_new);
		g_ptr_array_add(sess->client_myds->output.pkts, p);
	}
	if (r==ERR_Packet) {
		proxy_debug(PROXY_DEBUG_MYSQL_COM, 5, "Got ERR on COM_INIT_DB for schema %s\n", sess->mysql_schema_new);
		g_ptr_array_add(sess->client_myds->output.pkts, p);
	}
	if (sess->mysql_schema_new) {
		free(sess->mysql_schema_new);
		sess->mysql_schema_new=NULL;
	}
	// sync for auto-reconnect
	sess->server_bytes_at_cmd.bytes_sent=sess->server_myds->bytes_info.bytes_sent;
	sess->server_bytes_at_cmd.bytes_recv=sess->server_myds->bytes_info.bytes_recv;
}


inline void server_COM_QUERY(mysql_session_t *sess, pkt *p, enum MySQL_response_type r) {
							int i;
					if (r==OK_Packet) {
						// NOTE: we could receive a ROW packet that looks like an OK Packet. Do extra checks!
						if (sess->resultset_progress==RESULTSET_WAITING) {
							// this is really an OK Packet
							sess->resultset_progress=RESULTSET_COMPLETED;
							proxy_debug(PROXY_DEBUG_MYSQL_COM, 5, "Got OK on COM_QUERY\n");
						} else {
							// this is a ROW packet
							 sess->resultset_progress=RESULTSET_ROWS;
						}
					}
					if (r==ERR_Packet) {
						sess->resultset_progress=RESULTSET_COMPLETED;
						proxy_debug(PROXY_DEBUG_MYSQL_COM, 5, "Got ERR on COM_QUERY\n");
					}
					if (r==EOF_Packet) {
						if (sess->resultset_progress==RESULTSET_COLUMN_DEFINITIONS) {
							proxy_debug(PROXY_DEBUG_MYSQL_COM, 6, "Got 1st EOF on COM_QUERY\n");
							sess->resultset_progress=RESULTSET_EOF1;
						} else {
							proxy_debug(PROXY_DEBUG_MYSQL_COM, 6, "Got 2nd EOF on COM_QUERY\n");
							sess->resultset_progress=RESULTSET_COMPLETED;
						}
					}
					if (r==UNKNOWN_Packet) {
						switch (sess->resultset_progress) {
							case RESULTSET_WAITING:
								proxy_debug(PROXY_DEBUG_MYSQL_COM, 6, "Got column count on COM_QUERY\n");
								sess->resultset_progress=RESULTSET_COLUMN_COUNT;
								break;
							case RESULTSET_COLUMN_COUNT:
							case RESULTSET_COLUMN_DEFINITIONS:
								proxy_debug(PROXY_DEBUG_MYSQL_COM, 6, "Got column def on COM_QUERY\n");
								sess->resultset_progress=RESULTSET_COLUMN_DEFINITIONS;
								break;
							case RESULTSET_EOF1:
							case RESULTSET_ROWS:
							  	proxy_debug(PROXY_DEBUG_MYSQL_COM, 7, "Got row on COM_QUERY\n");
								sess->resultset_progress=RESULTSET_ROWS;
								break;
						}
					}
					//g_ptr_array_add(conn->client_myds->output.pkts, p);
					sess->resultset_size+=p->length;
					if (sess->resultset_size < glovars.mysql_max_resultset_size ) { // the resultset is within limit, copy to sess->resultset
						g_ptr_array_add(sess->resultset, p);
					} else { // the resultset went above limit
						sess->query_to_cache=FALSE; // the query cannot be cached, as we are not saving the result
						while(sess->resultset->len) {   // flush the resultset
							pkt *pt;
							pt=g_ptr_array_remove_index(sess->resultset, 0);
							g_ptr_array_add(sess->client_myds->output.pkts, pt);
						}
						g_ptr_array_add(sess->client_myds->output.pkts, p); // copy the new packet directly into the output queue

					}
					if (sess->resultset_progress==RESULTSET_COMPLETED) {
						sess->resultset_progress=RESULTSET_WAITING;

						// we have processed a complete result set, sync sess->server_bytes_at_cmd for auto-reconnect
						sess->server_bytes_at_cmd.bytes_sent=sess->server_myds->bytes_info.bytes_sent;
						sess->server_bytes_at_cmd.bytes_recv=sess->server_myds->bytes_info.bytes_recv;


						// return control to master if we were using a slave
						proxy_debug(PROXY_DEBUG_MYSQL_RW_SPLIT, 5, "Returning control to master. FDs: current: %d master: %d slave: %d\n", sess->server_fd , sess->master_fd , sess->slave_fd);
						if (sess->send_to_slave==TRUE) {
							sess->send_to_slave=FALSE;
							if (sess->master_myds) {
								sess->server_myds=sess->master_myds;
								sess->server_fd=sess->master_fd;
								sess->server_mycpe=sess->master_mycpe;
								sess->server_ptr=sess->master_ptr;
								sess->server_bytes_at_cmd.bytes_sent=sess->server_myds->bytes_info.bytes_sent;
								sess->server_bytes_at_cmd.bytes_recv=sess->server_myds->bytes_info.bytes_recv;
							}
						}

//					  conn->status &= ~CONNECTION_READING_SERVER;
						if (sess->query_to_cache==TRUE) {
							proxy_debug(PROXY_DEBUG_MYSQL_COM, 4, "Query %s needs to be cached\n", g_checksum_get_string(sess->query_info.query_checksum));
							proxy_debug(PROXY_DEBUG_MYSQL_COM, 4, "Resultset size = %d\n", sess->resultset_size);
							// prepare the entry to enter in the query cache
							void *kp=g_strdup(g_checksum_get_string(sess->query_info.query_checksum));
							void *vp=g_malloc(sess->resultset_size);
							//void *vp=g_slice_alloc(conn->resultset_size);
							size_t copied=0;
							for (i=0; i<sess->resultset->len; i++) {
								p=g_ptr_array_index(sess->resultset,i);
								memcpy(vp+copied,p->data,p->length);
								copied+=p->length;
							}
							// insert in the query cache
							proxy_debug(PROXY_DEBUG_MYSQL_COM, 4, "Calling SET on QC , checksum %s, kl %d, vl %d\n", (char *)kp, strlen(kp), sess->resultset_size);
							fdb_set(&QC, kp, strlen(kp), vp, sess->resultset_size, sess->query_info.cache_ttl, FALSE);
							//g_free(kp);
							//g_free(vp);
						}

						if (sess->resultset_size < glovars.mysql_max_resultset_size ) {
						// copy the query in the output queue
						// this happens only it wasn't flushed already
							for (i=0; i<sess->resultset->len; i++) {
								p=g_ptr_array_index(sess->resultset,i);
								g_ptr_array_add(sess->client_myds->output.pkts, p);
							}
							while (sess->resultset->len) {
								p=g_ptr_array_remove_index(sess->resultset, sess->resultset->len-1);
							}
						} else {
							proxy_debug(PROXY_DEBUG_MYSQL_COM, 4, "Query %s was too large ( %d bytes, min %d ) and wasn't stored\n", g_checksum_get_string(sess->query_info.query_checksum), sess->resultset_size , glovars.mysql_max_resultset_size );
						}
					}
}


void process_mysql_server_pkts(mysql_session_t *sess) {
	pkt *p;
	int i;
	if (sess->server_myds==NULL) { // the backend is not initialized, return
		return;
    }
	while(sess->server_myds->input.pkts->len) {
		p=g_ptr_array_remove_index(sess->server_myds->input.pkts, 0);
		enum MySQL_response_type r=mysql_response(p);
		switch (sess->client_command) {
			case COM_QUIT:
				server_COM_QUIT(sess,p,r);
				break;
			case COM_INIT_DB:
				server_COM_INIT_DB(sess,p,r);
				break;
			case COM_STATISTICS:
				server_COM_STATISTICS(sess,p);
				break;
			case COM_QUERY:
				server_COM_QUERY(sess,p,r);
				break;
			default:
				g_ptr_array_add(sess->client_myds->output.pkts, p);

		}
		//debug_print("Moving pkts from %s to %s\n", "server", "client");
	}
}


int process_mysql_client_pkts(mysql_session_t *sess) {
	while(sess->client_myds->input.pkts->len) {
		pkt *p;
		unsigned char c;
//		  p=get_pkt(sess->client_myds->input.pkts);
		p=g_ptr_array_remove_index(sess->client_myds->input.pkts, 0);
		c=*((unsigned char *)p->data+sizeof(mysql_hdr));
		sess->client_command=c;	 // a new packet is read from client, set the COM_
		sess->mysql_query_cache_hit=FALSE;
		sess->send_to_slave=FALSE;
		if ( (sess->admin==0) && ( p->length < glovars.mysql_max_query_size ) ) {
			switch (sess->client_command) {
				case COM_INIT_DB:
				case COM_QUERY:
				case COM_STATISTICS:
					/* if (!transaction) */
					sess->mysql_server_reconnect=TRUE;
					break;
				default:
					sess->mysql_server_reconnect=FALSE;
					break;
			}
		} else {
			sess->mysql_server_reconnect=FALSE;
		}
		switch (sess->client_command) {
			case COM_QUIT:
				client_COM_QUIT(sess);
				mypkt_free(p,sess,1);
	//			mysql_session_close(sess);
				return -1;
				break;
			case COM_STATISTICS:
				client_COM_STATISTICS(sess);
				break;
			case COM_INIT_DB:
					client_COM_INIT_DB(sess, p);
				break;
			case COM_QUERY:
				//client_COM_QUERY(conn, p, regex1, regex2);
				if (sess->admin==0) {
					client_COM_QUERY(sess, p); 
				} else {
					admin_COM_QUERY(sess, p);
				}
				break;
			default:
				if (sess->admin==1) {
					// we received an unknown packet
					// we shouldn't forward this if we are in admin mode
					sess->healthy=0;
				}
				break;
		}
		// if the command will be sent to the server and there is no data queued for it
		// if ( (sess->mysql_query_cache_hit==FALSE) && (queue_data(&sess->server_myds->output.queue)==0) ) { // wrong logic , it breaks if the connection is killed via KILL while idle
		if (sess->healthy==0) {
			authenticate_mysql_client_send_ERR(sess, 1045, "#28000Access denied for user");
			return -1;
		}
		if(sess->mysql_query_cache_hit==FALSE) {
			if (
				(sess->send_to_slave==FALSE) && 
				(sess->master_ptr==NULL) ) { // we don't have a connection to a master
/*
				if (sess->send_to_slave==FALSE)
				if (sess->slave_ptr==NULL) {
*/
					// no master assigned yet, find one!
					sess->master_ptr=new_server_master();
					if (sess->master_ptr==NULL) {
						// handle error!!
						authenticate_mysql_client_send_ERR(sess, 1045, "#28000Access denied for user");
						return -1;
					}
					sess->master_mycpe=mysql_connpool_get_connection(&gloconnpool, sess->master_ptr->address, sess->mysql_username, sess->mysql_password, sess->mysql_schema_cur, sess->master_ptr->port);
					if (sess->master_mycpe==NULL) {
						// handle error!!
						authenticate_mysql_client_send_ERR(sess, 1045, "#28000Access denied for user");
						return -1;
					}
					sess->master_fd=sess->master_mycpe->conn->net.fd;
					sess->master_myds=mysql_data_stream_init(sess->master_fd, sess);
					proxy_debug(PROXY_DEBUG_MYSQL_CONNECTION, 4, "Created new master_connection fd: %d\n", sess->master_fd);
					sess->server_myds=sess->master_myds;
					sess->server_fd=sess->master_fd;
					sess->server_mycpe=sess->master_mycpe;
					sess->server_ptr=sess->master_ptr;
				}	
				//sess->server_bytes_at_cmd.bytes_sent=sess->server_myds->bytes_info.bytes_sent;
				//sess->server_bytes_at_cmd.bytes_recv=sess->server_myds->bytes_info.bytes_recv;
			g_ptr_array_add(sess->server_myds->output.pkts, p);
			if (
				(sess->mysql_server_reconnect==TRUE)
				&& (queue_data(&sess->server_myds->input.queue)==0)
				&& (queue_data(&sess->server_myds->output.queue)==0)
			) {
				sess->server_bytes_at_cmd.bytes_sent=sess->server_myds->bytes_info.bytes_sent;
				sess->server_bytes_at_cmd.bytes_recv=sess->server_myds->bytes_info.bytes_recv;
			}
		//debug_print("Moving pkts from %s to %s\n", "client", "server");
		}
	}
	return 0;
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
		g_slice_free1(sizeof(query_rule_t), qr);
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

