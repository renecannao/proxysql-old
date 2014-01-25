#include "proxysql.h"



static void sync_server_bytes_at_cmd(mysql_session_t *sess) {
	if (sess->server_myds) {
		sess->server_bytes_at_cmd.bytes_sent=sess->server_myds->bytes_info.bytes_sent;
		sess->server_bytes_at_cmd.bytes_recv=sess->server_myds->bytes_info.bytes_recv;
	} else {
		sess->server_bytes_at_cmd.bytes_sent=0;
		sess->server_bytes_at_cmd.bytes_recv=0;	
	}
}

static inline void compute_query_checksum(mysql_session_t *sess) {
	if (sess->query_info.query_checksum==NULL) {
		sess->query_info.query_checksum=g_checksum_new(G_CHECKSUM_MD5);
		g_checksum_update(sess->query_info.query_checksum, sess->query_info.query, sess->query_info.query_len);
		g_checksum_update(sess->query_info.query_checksum, sess->mysql_username, strlen(sess->mysql_username));
		g_checksum_update(sess->query_info.query_checksum, sess->mysql_schema_cur, strlen(sess->mysql_schema_cur));
	}
}



static int get_result_from_mysql_query_cache(mysql_session_t *sess, pkt *p) {
	compute_query_checksum(sess);
	pkt *QCresult=NULL;
	QCresult=fdb_get(&QC, g_checksum_get_string(sess->query_info.query_checksum), sess);
	proxy_debug(PROXY_DEBUG_QUERY_CACHE, 4, "Called GET on QC for checksum %s in precheck\n", g_checksum_get_string(sess->query_info.query_checksum));
	if (QCresult) {
		proxy_debug(PROXY_DEBUG_QUERY_CACHE, 4, "Found QC entry for checksum %s in precheck\n", g_checksum_get_string(sess->query_info.query_checksum));
		g_ptr_array_add(sess->client_myds->output.pkts, QCresult);
		sess->mysql_query_cache_hit=TRUE;
		sess->query_to_cache=FALSE;	// query already in cache
		mypkt_free(p,sess,1);
		return 0;
	}
	return -1;
}



static inline void server_COM_QUIT(mysql_session_t *sess, pkt *p, enum MySQL_response_type r) {
	if (r==OK_Packet) {
		proxy_debug(PROXY_DEBUG_MYSQL_COM, 5, "Got OK on COM_QUIT\n");
#ifdef DEBUG_COM
#endif
		g_ptr_array_add(sess->client_myds->output.pkts, p);
	}
}
static inline void server_COM_STATISTICS(mysql_session_t *sess, pkt *p) {
	proxy_debug(PROXY_DEBUG_MYSQL_COM, 5, "Got packet on COM_STATISTICS\n");
	g_ptr_array_add(sess->client_myds->output.pkts, p);
	// sync for auto-reconnect
	sync_server_bytes_at_cmd(sess);
}

static inline void server_COM_INIT_DB(mysql_session_t *sess, pkt *p, enum MySQL_response_type r) {
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
	sync_server_bytes_at_cmd(sess);
}


static inline void server_COM_QUERY(mysql_session_t *sess, pkt *p, enum MySQL_response_type r) {
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
						while(sess->resultset->len) {	 // flush the resultset
							pkt *pt;
							pt=g_ptr_array_remove_index(sess->resultset, 0);
							g_ptr_array_add(sess->client_myds->output.pkts, pt);
						}
						g_ptr_array_add(sess->client_myds->output.pkts, p); // copy the new packet directly into the output queue

					}
					if (sess->resultset_progress==RESULTSET_COMPLETED) {
						sess->resultset_progress=RESULTSET_WAITING;

						// we have processed a complete result set, sync sess->server_bytes_at_cmd for auto-reconnect
						sync_server_bytes_at_cmd(sess);


						// return control to master if we were using a slave
						proxy_debug(PROXY_DEBUG_MYSQL_RW_SPLIT, 5, "Returning control to master. FDs: current: %d master: %d slave: %d\n", sess->server_fd , sess->master_fd , sess->slave_fd);
						if (sess->send_to_slave==TRUE) {
							sess->send_to_slave=FALSE;
/* obsoleted by hostgroup : BEGIN
							if (sess->master_myds) {
								sess->server_myds=sess->master_myds;
								sess->server_fd=sess->master_fd;
								sess->server_mycpe=sess->master_mycpe;
								sess->server_ptr=sess->master_ptr;
								sess->server_bytes_at_cmd.bytes_sent=sess->server_myds->bytes_info.bytes_sent;
								sess->server_bytes_at_cmd.bytes_recv=sess->server_myds->bytes_info.bytes_recv;
							}
obsoleted by hostgroup : END */
						}

//						conn->status &= ~CONNECTION_READING_SERVER;
						if (sess->query_to_cache==TRUE) {
							proxy_debug(PROXY_DEBUG_QUERY_CACHE, 4, "Query %s needs to be cached\n", g_checksum_get_string(sess->query_info.query_checksum));
							proxy_debug(PROXY_DEBUG_QUERY_CACHE, 4, "Resultset size = %d\n", sess->resultset_size);
							// prepare the entry to enter in the query cache
							int kl=strlen(g_checksum_get_string(sess->query_info.query_checksum));
							if ((kl+sess->resultset_size+sizeof(fdb_hash_entry)+sizeof(fdb_hash_entry *)) > fdb_hashes_group_free_mem(&QC)) {
								// there is no free memory
										__sync_fetch_and_add(&QC.cntSetERR,1);
								proxy_debug(PROXY_DEBUG_QUERY_CACHE, 4, "Query %s not cached because the QC is full\n", g_checksum_get_string(sess->query_info.query_checksum));
							} else {
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
								proxy_debug(PROXY_DEBUG_QUERY_CACHE, 4, "Calling SET on QC , checksum %s, kl %d, vl %d\n", (char *)kp, kl, sess->resultset_size);
								fdb_set(&QC, kp, kl, vp, sess->resultset_size, sess->query_info.cache_ttl, FALSE);
								//g_free(kp);
								//g_free(vp);
							}
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


static void process_server_pkts(mysql_session_t *sess) {
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



static int conn_poll(mysql_session_t *sess) {
	int r;
	struct pollfd *fds=sess->fds;
	fds[0].events=0;
	if ((sess->status & CONNECTION_READING_CLIENT) == CONNECTION_READING_CLIENT) {
		if (sess->client_myds->fd > 0 && queue_available(&sess->client_myds->input.queue)) fds[0].events|=POLLIN;
	}
	if ((sess->status & CONNECTION_WRITING_CLIENT) == CONNECTION_WRITING_CLIENT) {
		if (sess->client_myds->fd > 0 && ( queue_data(&sess->client_myds->output.queue) || sess->client_myds->output.partial || sess->client_myds->output.pkts->len ) ) fds[0].events|=POLLOUT;
	}
	if (sess->nfds>1) {
		fds[1].events=0;
		if ((sess->status & CONNECTION_READING_SERVER) == CONNECTION_READING_SERVER) {
			if (sess->server_myds->fd > 0 && queue_available(&sess->server_myds->input.queue)) fds[1].events|=POLLIN;
		}
		if ((sess->status & CONNECTION_WRITING_SERVER) == CONNECTION_WRITING_SERVER) {
			if (sess->server_myds->fd > 0 && ( queue_data(&sess->server_myds->output.queue) || sess->server_myds->output.partial || sess->server_myds->output.pkts->len ) ) fds[1].events|=POLLOUT;
		}
	}
	proxy_debug(PROXY_DEBUG_POLL, 4, "calling poll: fd %d events %d , fd %d events %d\n" , sess->fds[0].fd , sess->fds[0].events, sess->fds[1].fd , sess->fds[1].events);
//	r=poll(fds,sess->nfds,glovars.mysql_poll_timeout);
	return r;
}

static void read_from_net_2(mysql_session_t *sess) {
	// read_from_net for both sockets
	if ((sess->client_myds->fd > 0) && ((sess->fds[0].revents & POLLIN) == POLLIN)) {
		proxy_debug(PROXY_DEBUG_NET, 4, "Calling read_from_net for client\n");
		sess->client_myds->read_from_net(sess->client_myds);
	}
	if (
		(sess->server_myds!=NULL) && // the backend is initialized
		(sess->server_myds->fd > 0) &&
		((sess->fds[1].revents & POLLIN) == POLLIN)) {
		proxy_debug(PROXY_DEBUG_NET, 4, "Calling read_from_net for server\n");
		sess->server_myds->read_from_net(sess->server_myds);
	}
}

static void write_to_net_2(mysql_session_t *sess, int ignore_revents) {
	// write_to_net for both sockets
	if ((sess->client_myds->fd > 0) && ( ignore_revents || ((sess->fds[0].revents & POLLOUT) == POLLOUT) ) ) {
		proxy_debug(PROXY_DEBUG_NET, 4, "Calling write_to_net for client\n");
		sess->client_myds->write_to_net(sess->client_myds);
			// if I wrote everything to client, start reading from client
//			if ((queue_data(&conn->client_myds->output.queue)==0) && (conn->client_myds->output.pkts->len==0)) {
//				conn->status |= CONNECTION_READING_CLIENT;	
//			}
	}

	if (
		(sess->server_myds!=NULL) && // the backend is initialized
		(sess->server_myds->fd > 0)
		&& ( ignore_revents || ((sess->fds[1].revents & POLLOUT) == POLLOUT) ) ) {
		proxy_debug(PROXY_DEBUG_NET, 4, "Calling write_to_net for server\n");
		sess->server_myds->write_to_net(sess->server_myds);
			// if I wrote everything to server, start reading from server
//			if ((queue_data(&conn->server_myds->output.queue)==0) && (conn->server_myds->output.pkts->len==0)) {
//				conn->status |= CONNECTION_READING_SERVER;	
//			}
	}
}


static void buffer2array_2(mysql_session_t *sess) {
// buffer2array for both connections
	while(sess->client_myds->buffer2array(sess->client_myds) && (sess->client_myds->fd > 0) ) {}

	if (sess->server_myds!=NULL) { // the backend is initialized
		while(sess->server_myds->buffer2array(sess->server_myds) && (sess->server_myds->fd > 0)) {}
	}
}


static void array2buffer_2(mysql_session_t *sess) {
	proxy_debug(PROXY_DEBUG_PKT_ARRAY, 4, "Calling array2buffer for client\n");
	while(sess->client_myds->array2buffer(sess->client_myds)) {}

	if (sess->server_myds!=NULL) { // the backend is initialized
		proxy_debug(PROXY_DEBUG_PKT_ARRAY, 4, "Calling array2buffer for server\n");
		while(sess->server_myds->array2buffer(sess->server_myds)) {}
	}
}


static void check_fds_errors(mysql_session_t *sess) {
	if ( ((sess->fds[0].revents & POLLERR)==POLLERR) || ((sess->fds[0].revents & POLLHUP)==POLLHUP) || ((sess->fds[0].revents & POLLNVAL)==POLLNVAL) ) { 
		sess->client_myds->shut_soft(sess->client_myds);
	}
	if (sess->server_myds!=NULL) { // the backend is initialized
		if ( ((sess->fds[1].revents & POLLERR)==POLLERR) || ((sess->fds[1].revents & POLLHUP)==POLLHUP) || ((sess->fds[1].revents & POLLNVAL)==POLLNVAL) ) { 
			sess->server_myds->shut_soft(sess->server_myds);
		}
	}
}


static gboolean sync_net(mysql_session_t *sess, int write_only) {
	if (write_only==0) {
		read_from_net_2(sess);
		//if (reconnect_server_on_shut_fd(sess, &sess->server_mycpe)==FALSE) {
		if (reconnect_server_on_shut_fd(sess)==FALSE) {
			return FALSE;
		}
	}
	write_to_net_2(sess, write_only);
	//if (reconnect_server_on_shut_fd(sess, &sess->server_mycpe)==FALSE) {
	if (reconnect_server_on_shut_fd(sess)==FALSE) {
		return FALSE;
	}
	return TRUE;
}


static inline void client_COM_QUIT(mysql_session_t *sess) {
	proxy_debug(PROXY_DEBUG_MYSQL_COM, 5, "Got COM_QUIT packet\n");
}

static inline void client_COM_STATISTICS(mysql_session_t *sess) {
	proxy_debug(PROXY_DEBUG_MYSQL_COM, 5, "Got COM_STATISTICS packet\n");
	if (sess->admin==1) {
	// we shouldn't forward this if we are in admin mode
		sess->healthy=0;
		return;
	}
}

static inline void client_COM_INIT_DB(mysql_session_t *sess, pkt *p) {
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
		int j;
		// start from 1, don't reset hostgroup 0
		for (j=1; j<glovars.mysql_hostgroups; j++) {
			mysql_backend_t *mybe=g_ptr_array_index(sess->mybes,j);
			mybe->reset(mybe,0);
		}
	}
}

static inline void client_COM_QUERY(mysql_session_t *sess, pkt *p) {
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
			if (get_result_from_mysql_query_cache(sess,p)==0) return;
		}
		process_query_rules(sess);
		if (
			(sess->client_command==COM_QUERY) &&
			( sess->query_info.cache_ttl > 0 )
		) {
			sess->query_to_cache=TRUE;		// cache the query
			if (get_result_from_mysql_query_cache(sess,p)==0) {
			} else {
				proxy_debug(PROXY_DEBUG_QUERY_CACHE, 4, "Not found QC entry for checksum %s after query prepocessing\n", g_checksum_get_string(sess->query_info.query_checksum));

/*
				sess->send_to_slave=TRUE;
				if (sess->slave_ptr==NULL) {
					// no slave assigned yet, find one!
					//sess->slave_ptr=new_server_slave();
					sess->slave_ptr=mysql_server_random_entry_from_hostgroup__lock(1);
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
*/
			}
		}
//					conn->status &= ~CONNECTION_READING_CLIENT; // NOTE: this is not true for packets >= 16MB , be careful
	}
}

static int active_backend_for_hostgroup(mysql_session_t *sess, int hostgroup_id) {
	assert(hostgroup_id < glovars.mysql_hostgroups);
	mysql_backend_t *mybe=g_ptr_array_index(sess->mybes,hostgroup_id);
	if (mybe->server_ptr) {
		// backend is active
		return 1;
	} else {
		// backend is NOT active
		return 0;
	}
}


static int process_client_pkts(mysql_session_t *sess) {
	while(sess->client_myds->input.pkts->len) {
		pkt *p;
		unsigned char c;
//			p=get_pkt(sess->client_myds->input.pkts);
		p=g_ptr_array_remove_index(sess->client_myds->input.pkts, 0);
		c=*((unsigned char *)p->data+sizeof(mysql_hdr));
		sess->client_command=c;	// a new packet is read from client, set the COM_
		sess->mysql_query_cache_hit=FALSE;
		sess->query_to_cache=FALSE;
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
			if ( sess->client_command != COM_QUERY ) { // if it is not a QUERY , always send to hostgroup 0
				sess->query_info.destination_hostgroup=0;
			}
			if (active_backend_for_hostgroup(sess, sess->query_info.destination_hostgroup)==0) {
				mysql_session_create_backend_for_hostgroup(sess, sess->query_info.destination_hostgroup);
			}
			mysql_backend_t *mybe=g_ptr_array_index(sess->mybes, sess->query_info.destination_hostgroup);
			if (mybe->server_ptr==NULL) {
				// we don't have a backend , probably because there is no host associated with the hostgroup, or because we have an error
				// park the packet ?
				// create a DataStream without FD ?
				// push the packet back to client_myds ?
				// FIXME
				// trying "put it back"
				GPtrArray *new_input_pkts=g_ptr_array_sized_new(sess->client_myds->input.pkts->len);
				g_ptr_array_add(new_input_pkts,p);
				while(sess->client_myds->input.pkts->len) {
					pkt *pn=g_ptr_array_remove_index(sess->client_myds->input.pkts, 0);
					g_ptr_array_add(new_input_pkts,pn);
				}
				while(new_input_pkts->len) {
					pkt *pn=g_ptr_array_remove_index(new_input_pkts, 0);
					g_ptr_array_add(sess->client_myds->input.pkts,pn);
				}
				g_ptr_array_free(new_input_pkts,TRUE);
				return 0;
			}

			// here, mybe is NOT NULL

			if (mybe->server_mycpe==NULL) {
				// handle error!!
				authenticate_mysql_client_send_ERR(sess, 1045, "#28000Access denied for user");
				return -1;
			}
/*
			if (mybe->server_myds) {
				sess->server_bytes_at_cmd.bytes_sent=mybe->server_myds->bytes_info.bytes_sent;
				sess->server_bytes_at_cmd.bytes_recv=mybe->server_myds->bytes_info.bytes_recv;
			}
*/
			if ( mybe->server_ptr->status==MYSQL_SERVER_STATUS_OFFLINE_HARD ) {
				// we didn't manage to gracefully shutdown the connection , disconnect the client
				return -1;
			}
			if ( mybe->server_ptr->status==MYSQL_SERVER_STATUS_OFFLINE_SOFT && mybe->server_myds->active_transaction==0) {
				// disconnect the backend and get a new one
				proxy_debug(PROXY_DEBUG_MYSQL_SERVER, 7, "MySQL server %s:%d is OFFLINE_SOFT, disconnect\n", mybe->server_ptr->address, mybe->server_ptr->port);
				//reset_mysql_backend(mybe,0);
				mybe->reset(mybe,0);
				mysql_session_create_backend_for_hostgroup(sess, sess->query_info.destination_hostgroup);
				if (mybe->server_ptr==NULL) {
					// FIXME
					assert(0);
					return 0;
				}

			}
			if ( mybe->server_ptr->status==MYSQL_SERVER_STATUS_ONLINE ) {
				proxy_debug(PROXY_DEBUG_MYSQL_SERVER, 7, "MySQL server %s:%d is ONLINE, forward data\n", mybe->server_ptr->address, mybe->server_ptr->port);
				sess->server_myds=mybe->server_myds;
				sess->server_fd=mybe->fd;
				sess->server_mycpe=mybe->server_mycpe;
				sess->server_ptr=mybe->server_ptr;
				sync_server_bytes_at_cmd(sess);
				g_ptr_array_add(sess->server_myds->output.pkts, p);
			} else {
				// we should never reach here, sanity check
				assert(0);
			}
/*
			if (
				(sess->send_to_slave==FALSE) && 
				(sess->master_ptr==NULL) ) { // we don't have a connection to a master

					// no master assigned yet, find one!
					//sess->master_ptr=new_server_master();
					sess->master_ptr=mysql_server_random_entry_from_hostgroup__lock(0);
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
*/
		}
	}
	return 0;
}

static int remove_all_backends_offline_soft(mysql_session_t *sess) {
	int j;
	int cnt=0;
	for (j=0; j<glovars.mysql_hostgroups; j++) {
		mysql_backend_t *mybe=g_ptr_array_index(sess->mybes,j);
// remove all the backends that are not active
		if (mybe->server_ptr!=NULL) {
			if (mybe->server_ptr->status==MYSQL_SERVER_STATUS_OFFLINE_SOFT) {
				if (mybe->server_myds->active_transaction==0) {
					if (mybe->server_myds!=sess->server_myds) {
						//reset_mysql_backend(mybe,0);
						mybe->reset(mybe,0);
					} else {
						if (sess->server_bytes_at_cmd.bytes_sent==sess->server_myds->bytes_info.bytes_sent) {
							if (sess->server_bytes_at_cmd.bytes_recv==sess->server_myds->bytes_info.bytes_recv) {
								//reset_mysql_backend(mybe,0);
								mybe->reset(mybe,0);
								sess->server_myds=NULL;
								sess->server_mycpe=NULL;
							}
						}
					}
				}
			}
		}
	}
	// count after cleanup
	for (j=0; j<glovars.mysql_hostgroups; j++) {
		mysql_backend_t *mybe=g_ptr_array_index(sess->mybes,j);
		if (mybe->server_ptr!=NULL)
			if (mybe->server_ptr->status==MYSQL_SERVER_STATUS_OFFLINE_SOFT)
				cnt++;
	}
	return cnt;
}



static void sess_close(mysql_session_t *sess) {
	int i;
	proxy_debug(PROXY_DEBUG_MYSQL_CONNECTION, 4, "Closing connection on client fd %d (myds %d , sess %p)\n", sess->client_fd, sess->client_myds->fd, sess);
	mysql_data_stream_delete(sess->client_myds);
	if (sess->client_myds->fd) { sess->client_myds->shut_hard(sess->client_myds); }

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


	proxy_debug(PROXY_DEBUG_MYSQL_CONNECTION, 6, "Freeing mysql backends for session %p\n", sess);
	for (i=0; i<glovars.mysql_hostgroups; i++) {
		proxy_debug(PROXY_DEBUG_MYSQL_CONNECTION, 6, "Freeing mysql backend %d for session %p\n", i, sess);
		mysql_backend_t *mybe=g_ptr_array_index(sess->mybes,i);
		//reset_mysql_backend(mybe, sess->force_close_backends);
		mybe->reset(mybe, sess->force_close_backends);
	}
	while (sess->mybes->len) {
		mysql_backend_t *mybe=g_ptr_array_remove_index_fast(sess->mybes,0);
		//g_slice_free1(sizeof(mysql_backend_t),mybe);
		mysql_backend_delete(mybe);
	}
	g_ptr_array_free(sess->mybes,TRUE);

	sess->healthy=0;
	init_query_metadata(sess, NULL);
	//free(sess);
	//mysql_con->net.fd=mysql_fd;
	//if (mysql_con) { mysql_close(mysql_con); }
//	mysql_close(mysql_con); <== don't call mysql_close, as the connection is already closed by the connector logic
	// this needs to be fixed. Connectors shouldn't close a server connection . Shutdown should be moved here
//	mysql_thread_end();

	// unregister the connection
	pthread_rwlock_wrlock(&glomysrvs.rwlock);
	g_ptr_array_remove_fast(glomysrvs.mysql_connections, sess);
	glomysrvs.mysql_connections_cur-=1;
	pthread_rwlock_unlock(&glomysrvs.rwlock);
}


static void process_authentication_pkt(mysql_session_t *sess) {
	pkt *hs=NULL;
	hs=g_ptr_array_remove_index(sess->client_myds->input.pkts, 0);
	sess->ret=check_client_authentication_packet(hs,sess);
	g_slice_free1(hs->length, hs->data);
	if (sess->ret) {
 		create_err_packet(hs, 2, 1045, "#28000Access denied for user");
//        authenticate_mysql_client_send_ERR(sess, 1045, "#28000Access denied for user");
	} else {
		create_ok_packet(hs,2);
		if (sess->mysql_schema_cur==NULL) {
			sess->mysql_schema_cur=strdup(glovars.mysql_default_schema);
		}
	}
	g_ptr_array_add(sess->client_myds->output.pkts, hs);
}


// thread that handles connection
static int session_handler(mysql_session_t *sess) {

  sess->status=CONNECTION_READING_CLIENT|CONNECTION_WRITING_CLIENT|CONNECTION_READING_SERVER|CONNECTION_WRITING_SERVER;
  if (sess->healthy) {


    if (sess->client_myds->active==FALSE) { // || sess->server_myds->active==FALSE) {
      goto exit_session_handler;
    }

    if (sess->sync_net(sess,0)==FALSE) {
      goto exit_session_handler;
    }

    buffer2array_2(sess);

    if (sess->client_myds->pkts_sent==1 && sess->client_myds->pkts_recv==1) {
      sess->process_authentication_pkt(sess);
    }
    // set status to all possible . Remove options during processing
//    sess->status=CONNECTION_READING_CLIENT|CONNECTION_WRITING_CLIENT|CONNECTION_READING_SERVER|CONNECTION_WRITING_SERVER;


    if (process_client_pkts(sess)==-1) {
      // we got a COM_QUIT
      goto exit_session_handler;
    }
    process_server_pkts(sess);

    array2buffer_2(sess);


    if ( (sess->server_myds==NULL) || (sess->last_server_poll_fd==sess->server_myds->fd)) {
      // this optimization is possible only if a connection to the backend didn't break in the meantime,
      // or we never connected to a backend
      if (sess->sync_net(sess,1)==FALSE) {
        goto exit_session_handler;
      }
    }
    if (sess->client_myds->pkts_sent==2 && sess->client_myds->pkts_recv==1) {
      if (sess->mysql_schema_cur==NULL) {
        goto exit_session_handler;
        //sess->close(sess); return -1;
      }
    }
    return 0;
  } else {
  exit_session_handler:
  sess->close(sess);
  return -1;
  }
}


mysql_session_t * mysql_session_new(proxy_mysql_thread_t *handler_thread, int client_fd) {
	int i;
	mysql_session_t *sess=g_malloc0(sizeof(mysql_session_t));
	sess->client_fd=client_fd;
	// register the connection
	pthread_rwlock_wrlock(&glomysrvs.rwlock);
	g_ptr_array_add(glomysrvs.mysql_connections, sess);
	glomysrvs.mysql_connections_cur+=1;
	pthread_rwlock_unlock(&glomysrvs.rwlock);
	// generic initalization
	sess->server_ptr=NULL;
	sess->server_myds=NULL;
	sess->server_mycpe=NULL;
	sess->mysql_username=NULL;
	sess->mysql_password=NULL;
	sess->mysql_schema_cur=NULL;
	sess->mysql_schema_new=NULL;
	sess->server_bytes_at_cmd.bytes_sent=0;
	sess->server_bytes_at_cmd.bytes_recv=0;
	sess->mysql_server_reconnect=TRUE;
	sess->healthy=1;
	sess->force_close_backends=0;
	sess->admin=0;
	sess->resultset=g_ptr_array_new();
	sess->timers=calloc(sizeof(timer),TOTAL_TIMERS);
	sess->handler_thread=handler_thread;
	//sess->client_myds=mysql_data_stream_init(sess->client_fd, sess);
	sess->client_myds=mysql_data_stream_new(sess->client_fd, sess);	
	sess->client_myds->fd=sess->client_fd;
	sess->fds[0].fd=sess->client_myds->fd;
	sess->fds[0].events=POLLIN|POLLOUT;
	sess->nfds=1;
	sess->query_to_cache=FALSE;
	sess->client_command=COM_END;	 // always reset this
	sess->send_to_slave=FALSE;
	memset(&sess->query_info,0,sizeof(mysql_query_metadata_t));
	
	proxy_debug(PROXY_DEBUG_MYSQL_CONNECTION, 6, "Initializing mysql backends for session %p\n", sess);
	sess->mybes=g_ptr_array_sized_new(glovars.mysql_hostgroups);
	for (i=0;i<glovars.mysql_hostgroups;i++) {
		mysql_backend_t *mybe=mysql_backend_new();
		g_ptr_array_add(sess->mybes, mybe);
	}

	sess->conn_poll = conn_poll;
	sess->sync_net = sync_net;
//	sess->array2buffer_2 = array2buffer_2;
//	sess->buffer2array_2 = buffer2array_2;
	sess->check_fds_errors = check_fds_errors;
	//sess->process_client_pkts = process_client_pkts;
	//sess->process_server_pkts = process_server_pkts;
	sess->remove_all_backends_offline_soft = remove_all_backends_offline_soft;
	sess->close = sess_close;
	sess->process_authentication_pkt = process_authentication_pkt;
	sess->handler = session_handler;
	return sess;
}

void mysql_session_delete(mysql_session_t *sess) {
	g_free(sess);
	sess=NULL;
}
