#include "proxysql.h"


void mysql_session_init(mysql_session_t *sess) {
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
	sess->query_checksum=NULL;
	sess->server_bytes_at_cmd.bytes_sent=0;
	sess->server_bytes_at_cmd.bytes_recv=0;
	sess->mysql_server_reconnect=TRUE;
	sess->healthy=1;
	sess->admin=0;
	sess->resultset=g_ptr_array_new();
	sess->timers=calloc(sizeof(timer),TOTAL_TIMERS);
	// these two are the only regex currently supported . They needs to be extended
	sess->regex[0] = g_regex_new ("^SELECT ", G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0, NULL);
	sess->regex[1] = g_regex_new ("\\s+FOR\\s+UPDATE\\s*$", G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0, NULL);
	sess->free_pkts.stack=NULL;
	sess->free_pkts.blocks=g_ptr_array_new();
}


void mysql_session_close(mysql_session_t *sess) {
#ifdef DEBUG_mysql_conn
        debug_print("Closing connection on client fd %d (myds %d , sess %p)\n", sess->client_fd, sess->client_myds->fd, sess);
#endif
	mysql_data_stream_close(sess->client_myds);
	if (sess->client_myds->fd) { mysql_data_stream_shut_hard(sess->client_myds); }
	if (sess->master_myds) {
		if (sess->master_mycpe) {
			if (sess->master_mycpe->conn) {
				mysql_connpool_detach_connection(gloconnpool, sess->master_mycpe);
			}
		}
		mysql_data_stream_close(sess->master_myds);
	}
	if (sess->slave_myds) {
		if (sess->slave_mycpe) {
			if (sess->slave_mycpe->conn) {
				mysql_connpool_detach_connection(gloconnpool, sess->slave_mycpe);
			}
		}
		mysql_data_stream_close(sess->slave_myds);
	}
	while (sess->resultset->len) {
		pkt *p;
		p=g_ptr_array_remove_index(sess->resultset, 0);
		g_slice_free1(p->length, p->data);
#ifdef PKTALLOC
#ifdef DEBUG_pktalloc
		debug_print("%s\n", "mypkt_free");
#endif
		mypkt_free(p,sess);
#else
		g_slice_free1(sizeof(pkt),p);
#endif
	}	
	g_ptr_array_free(sess->resultset,TRUE);
	free(sess->timers);

	g_regex_unref(sess->regex[0]);
	g_regex_unref(sess->regex[1]);

	if (sess->mysql_username) { free(sess->mysql_username); sess->mysql_username=NULL; }
	if (sess->mysql_password) { free(sess->mysql_password); sess->mysql_password=NULL; }
	if (sess->mysql_schema_cur) { free(sess->mysql_schema_cur); sess->mysql_schema_cur=NULL; }
	if (sess->mysql_schema_new) { free(sess->mysql_schema_new); sess->mysql_schema_new=NULL; }
	if (sess->query_checksum) { g_checksum_free(sess->query_checksum); }

	while (sess->free_pkts.blocks->len) {
		void *p=g_ptr_array_remove_index_fast(sess->free_pkts.blocks, 0);
		free(p);
	}
	g_ptr_array_free(sess->free_pkts.blocks,TRUE);
	sess->healthy=0;
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
#ifdef DEBUG_COM
	debug_print("Got COM_%s packet\n", "QUIT");
#endif
}

inline void client_COM_STATISTICS(mysql_session_t *sess) {
#ifdef DEBUG_COM
	debug_print("Got COM_%s packet\n", "STATISTICS");
#endif
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
#ifdef DEBUG_COM
	debug_print("Got COM_%s packet for schema %s\n", "INIT_DB", sess->mysql_schema_new);
#endif
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
					mysql_connpool_detach_connection(gloconnpool, sess->slave_mycpe);
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
#ifdef PKTALLOC
#ifdef DEBUG_pktalloc
	debug_print("%s\n", "mypkt_alloc");
#endif
	p=mypkt_alloc(sess);
#else
	np=g_slice_alloc(sizeof(pkt));
#endif
	// hardcoded, we send " (ProxySQL) "
	p->length=81;
	p->data=g_slice_alloc0(p->length);
	memcpy(p->data,"\x01\x00\x00\x01\x01\x27\x00\x00\x02\x03\x64\x65\x66\x00\x00\x00\x11\x40\x40\x76\x65\x72\x73\x69\x6f\x6e\x5f\x63\x6f\x6d\x6d\x65\x6e\x74\x00\x0c\x21\x00\x18\x00\x00\x00\xfd\x00\x00\x1f\x00\x00\x05\x00\x00\x03\xfe\x00\x00\x02\x00\x0b\x00\x00\x04\x0a\x28\x50\x72\x6f\x78\x79\x53\x51\x4c\x29\x05\x00\x00\x05\xfe\x00\x00\x02\x00",p->length);
	return p;
}

inline void admin_COM_QUERY(mysql_session_t *sess, pkt *p) {
	// enter admin mode
	// configure the session to not send data to servers
	sess->mysql_query_cache_hit=TRUE;
	sess->query_to_cache=FALSE;
	if (strncmp("select @@version_comment limit 1", p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1)==0) {
		// mysql client in interactive mode sends "select @@version_comment limit 1" : we treat this as a special case

		// drop the packet from client
		g_slice_free1(p->length, p->data);
#ifdef PKTALLOC
#ifdef DEBUG_pktalloc
		debug_print("%s\n", "mypkt_free");
#endif
		mypkt_free(p,sess);
#else
		g_slice_free1(sizeof(pkt), p);
#endif

		// prepare a new packet to send to the client
		pkt *np=NULL;
		np=admin_version_comment_pkt(sess);
		g_ptr_array_add(sess->client_myds->output.pkts, np);
		return;
	}
	sess->healthy=0;
	return;
}

inline void client_COM_QUERY(mysql_session_t *sess, pkt *p) {

//	MD5(p->data+sizeof(mysql_hdr)+1,p->length-sizeof(mysql_hdr)-1,md);
	if (mysql_pkt_get_size(p) > glovars.mysql_max_query_size) {
		// the packet is too big. Ignore any processing
		sess->client_command=COM_END;
	} else {
		sess->resultset_progress=RESULTSET_WAITING;
		sess->resultset_size=0;
		if (sess->query_checksum) {
			g_checksum_free(sess->query_checksum);
		}
		sess->query_checksum=g_checksum_new(G_CHECKSUM_MD5);
		g_checksum_update(sess->query_checksum, p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1);

/*
if the query is cached:
	destroy the pkg
	get the packets from the cache and send it to the client

if the query is not cached, mark it as to be cached and modify the code on result set

*/
#ifdef DEBUG_COM
		debug_print("Got COM_%s packet , MD5 %s , Query %s\n", "QUERY", g_checksum_get_string(sess->query_checksum) , (char *)p->data+sizeof(mysql_hdr)+1);
#endif
		if (
			(sess->client_command==COM_QUERY) &&
			query_is_cachable(sess, p->data+sizeof(mysql_hdr)+1, p->length-sizeof(mysql_hdr)-1)
		) {
			sess->query_to_cache=TRUE;	  // cache the query
//					  void *data=NULL;
//					  unsigned long length=0;
			pkt *QCresult=NULL;
			QCresult=fdb_get(&QC, g_checksum_get_string(sess->query_checksum), sess);
//					  g_slice_free1(length,data);
//					  length=0;
#ifdef DEBUG_COM
			debug_print("Called GET on QC , checksum %s\n", g_checksum_get_string(sess->query_checksum));
#endif
			if (QCresult) {
//						  result->length=length;
//						  result->data=data;
				g_ptr_array_add(sess->client_myds->output.pkts, QCresult);
				sess->mysql_query_cache_hit=TRUE;
				sess->query_to_cache=FALSE;	 // query already in cache
				g_slice_free1(p->length, p->data);
#ifdef PKTALLOC
#ifdef DEBUG_pktalloc
				debug_print("%s\n", "mypkt_free");
#endif
				mypkt_free(p,sess);
#else
				g_slice_free1(sizeof(pkt), p);
#endif
			} else {
				sess->send_to_slave=TRUE;
				if (sess->slave_ptr==NULL) {
					// no slave assigned yet, find one!
					sess->slave_ptr=new_server_slave();
					if (sess->slave_ptr==NULL) {
						// handle error!!
						sess->healthy=0;
						return ;
					}
					sess->slave_mycpe=mysql_connpool_get_connection(gloconnpool, sess->slave_ptr->address, sess->mysql_username, sess->mysql_password, sess->mysql_schema_cur, sess->slave_ptr->port);
					if (sess->slave_mycpe==NULL) {
						// handle error!!
						sess->healthy=0;
						return ;
					}
					sess->slave_fd=sess->slave_mycpe->conn->net.fd;
					sess->slave_myds=mysql_data_stream_init(sess->slave_fd , sess);
#ifdef DEBUG_mysql_conn
					debug_print("Created new slave_connection fd: %d\n", sess->slave_fd);
#endif
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
#ifdef DEBUG_COM
		debug_print("%s\n" , "Got OK on COM_QUIT");
#endif
		g_ptr_array_add(sess->client_myds->output.pkts, p);
	}
}
inline void server_COM_STATISTICS(mysql_session_t *sess, pkt *p) {
#ifdef DEBUG_COM
	debug_print("%s\n" , "Got packet on COM_STATISTICS");
#endif
	g_ptr_array_add(sess->client_myds->output.pkts, p);
	// sync for auto-reconnect
	sess->server_bytes_at_cmd.bytes_sent=sess->server_myds->bytes_info.bytes_sent;
	sess->server_bytes_at_cmd.bytes_recv=sess->server_myds->bytes_info.bytes_recv;
}

inline void server_COM_INIT_DB(mysql_session_t *sess, pkt *p, enum MySQL_response_type r) {
	if (r==OK_Packet) {
#ifdef DEBUG_COM
		debug_print("Got %s on COM_INIT_DB for schema %s\n", "OK", sess->mysql_schema_new);
#endif
		if (sess->mysql_schema_cur) {
			free(sess->mysql_schema_cur);
			sess->mysql_schema_cur=strdup(sess->mysql_schema_new);
		}
		sess->mysql_schema_cur=g_strdup(sess->mysql_schema_new);
		g_ptr_array_add(sess->client_myds->output.pkts, p);
	}
	if (r==ERR_Packet) {
#ifdef DEBUG_COM
		debug_print("Got %s on COM_INIT_DB for schema %s\n", "ERR", sess->mysql_schema_new);
#endif
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
#ifdef DEBUG_COM
							debug_print("%s\n" , "Got OK on COM_QUERY");
#endif
						} else {
							// this is a ROW packet
							 sess->resultset_progress=RESULTSET_ROWS;
						}
					}
					if (r==ERR_Packet) {
						sess->resultset_progress=RESULTSET_COMPLETED;
#ifdef DEBUG_COM
						debug_print("%s\n" , "Got ERR on COM_QUERY");
#endif
					}
					if (r==EOF_Packet) {
						if (sess->resultset_progress==RESULTSET_COLUMN_DEFINITIONS) {
#ifdef DEBUG_COM
							debug_print("%s\n" , "Got 1st EOF on COM_QUERY");
#endif
							sess->resultset_progress=RESULTSET_EOF1;
						} else {
#ifdef DEBUG_COM
							debug_print("%s\n" , "Got 2nd EOF on COM_QUERY");
#endif
							sess->resultset_progress=RESULTSET_COMPLETED;
						}
					}
					if (r==UNKNOWN_Packet) {
						switch (sess->resultset_progress) {
							case RESULTSET_WAITING:
#ifdef DEBUG_COM
								debug_print("%s\n" , "Got column count on COM_QUERY");
#endif
								sess->resultset_progress=RESULTSET_COLUMN_COUNT;
								break;
							case RESULTSET_COLUMN_COUNT:
							case RESULTSET_COLUMN_DEFINITIONS:
#ifdef DEBUG_COM
								debug_print("%s\n" , "Got column def on COM_QUERY");
#endif
								sess->resultset_progress=RESULTSET_COLUMN_DEFINITIONS;
								break;
							case RESULTSET_EOF1:
							case RESULTSET_ROWS:
#ifdef DEBUG_COM
//							  debug_print("%s\n" , "Got row on COM_QUERY");
#endif
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
#ifdef DEBUG_mysql_rw_split
						debug_print("Returning control to master. FDs: current: %d master: %d slave: %d\n", sess->server_fd , sess->master_fd , sess->slave_fd);
#endif
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
#ifdef DEBUG_COM
							debug_print("Query %s needs to be cached\n", g_checksum_get_string(sess->query_checksum));
#endif
#ifdef DEBUG_COM
							debug_print("Resultset size = %d\n", sess->resultset_size);
#endif
							// prepare the entry to enter in the query cache
							void *kp=g_strdup(g_checksum_get_string(sess->query_checksum));
							void *vp=g_malloc(sess->resultset_size);
							//void *vp=g_slice_alloc(conn->resultset_size);
							size_t copied=0;
							for (i=0; i<sess->resultset->len; i++) {
								p=g_ptr_array_index(sess->resultset,i);
								memcpy(vp+copied,p->data,p->length);
								copied+=p->length;
							}
							// insert in the query cache
#ifdef DEBUG_COM
							debug_print("Calling SET on QC , checksum %s, kl %d, vl %d\n", (char *)kp, strlen(kp), sess->resultset_size);
#endif
							fdb_set(&QC,kp,strlen(kp),vp,sess->resultset_size,0,FALSE);
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
#ifdef DEBUG_COM
							debug_print("Query %s was too large ( %d bytes, min %d ) and wasn't stored\n", g_checksum_get_string(sess->query_checksum), sess->resultset_size , glovars.mysql_max_resultset_size );
#endif
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
				g_slice_free1(p->length, p->data);
#ifdef PKTALLOC
#ifdef DEBUG_pktalloc
				debug_print("%s\n", "mypkt_free");
#endif
				mypkt_free(p,sess);
#else
				g_slice_free1(sizeof(pkt), p);
#endif
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
					sess->master_mycpe=mysql_connpool_get_connection(gloconnpool, sess->master_ptr->address, sess->mysql_username, sess->mysql_password, sess->mysql_schema_cur, sess->master_ptr->port);
					if (sess->master_mycpe==NULL) {
						// handle error!!
						authenticate_mysql_client_send_ERR(sess, 1045, "#28000Access denied for user");
						return -1;
					}
					sess->master_fd=sess->master_mycpe->conn->net.fd;
					sess->master_myds=mysql_data_stream_init(sess->master_fd, sess);
#ifdef DEBUG_mysql_conn
					debug_print("Created new master_connection fd: %d\n", sess->master_fd);
#endif
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
