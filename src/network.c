#include "proxysql.h"

inline void mysql_data_stream_shut_soft(mysql_data_stream_t *myds) {
#ifdef DEBUG_shutfd
	debug_print("Shutdown soft %d\n", myds->fd);
#endif
	myds->active=FALSE;
}

inline void mysql_data_stream_shut_hard(mysql_data_stream_t *myds) {
#ifdef DEBUG_shutfd
	debug_print("Shutdown hard %d\n", myds->fd);
#endif
	if (myds->fd >= 0) {
		shutdown(myds->fd, SHUT_RDWR);
		close(myds->fd);
		myds->fd = -1;
	}
}


int listen_on_port(uint16_t port) {
    int rc, arg_on=1, arg_off=0;
	struct sockaddr_in addr;
	int sd;
	if ( (sd = socket(PF_INET, SOCK_STREAM, 0)) < 0 )
		PANIC("Socket - TCP");
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	if ( bind(sd, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
		PANIC("Bind - TCP");
	if ( listen(sd, glovars.backlog) != 0 )
		PANIC("Listen - TCP");

	rc = setsockopt(sd, SOL_SOCKET,  SO_REUSEADDR, (char *)&arg_on, sizeof(arg_on));
	if (rc < 0) {
		PANIC("setsockopt() failed");
	}
	return sd;
}

int listen_on_unix(char *path) {
	struct sockaddr_un serveraddr;
	int sd;
	unlink(path);
	if ( ( sd = socket(AF_UNIX, SOCK_STREAM, 0)) <0 )
		PANIC("Socket - Unix");
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sun_family = AF_UNIX;
    strncpy(serveraddr.sun_path, path, sizeof(serveraddr.sun_path) - 1);
    if ( bind(sd, (struct sockaddr *)&serveraddr, sizeof(struct sockaddr_un)) != 0 )
		PANIC("Bind - Unix");
	if ( listen(sd, glovars.backlog) != 0 )
		PANIC("Listen - Unix");

	return sd;
}


int connect_socket(char *address, int connect_port)
{
	struct sockaddr_in a;
	int s;

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		close(s);
		return -1;
	}

	memset(&a, 0, sizeof(a));
	a.sin_port = htons(connect_port);
	a.sin_family = AF_INET;

	if (!inet_aton(address, (struct in_addr *) &a.sin_addr.s_addr)) {
		perror("bad IP address format");
		close(s);
		return -1;
	}

	if (connect(s, (struct sockaddr *) &a, sizeof(a)) == -1) {
		perror("connect()");
		shutdown(s, SHUT_RDWR);
		close(s);
		return -1;
	}
	return s;
}


int read_from_net(mysql_data_stream_t *myds) {
	int r;
	queue_t *q=&myds->input.queue;
	int s=queue_available(q);
	r = recv(myds->fd, queue_w_ptr(q), s, 0);
#ifdef DEBUG_read_from_net
	debug_print("read %d bytes from fd %d into a buffer of %d bytes free\n", r, myds->fd, s);
#endif
	if (r < 1) {
		if (r==-1) {
			mysql_data_stream_shut_soft(myds);
		} //else { printf("%d\n",errno); }
		if ((r==0) && (!errno)) { mysql_data_stream_shut_soft(myds); }
	}
	else {
		queue_w(q,r);
		myds->bytes_info.bytes_recv+=r;
	}
	return r;	
}

int write_to_net(mysql_data_stream_t *myds) {
	int r=0;
	queue_t *q=&myds->output.queue;
//	r = write(myds->fd, queue_r_ptr(&myds->output.queue), queue_data(&myds->output.queue));
	int s = queue_data(q);
	if (s==0) return 0;
	r = send(myds->fd, queue_r_ptr(q), s, 0);
#ifdef DEBUG_write_to_net
	//debug_print("wrote %d bytes to fd %d\n", r, myds->fd);
	debug_print("wrote %d bytes to fd %d from a buffer with %d bytes of data\n", r, myds->fd, s);
#endif
	//if (r < 1) {
	if (r < 0) {
		mysql_data_stream_shut_soft(myds);
	}
	else {
		queue_r(q,r);
		myds->bytes_info.bytes_sent+=r;
	}
	return r;
}

int buffer2array(mysql_data_stream_t *myds) {
	int ret=0;
	queue_t *qin = &myds->input.queue;
#ifdef DEBUG_buffer2array
		debug_print("BEGIN : bytes in buffer = %d\n", queue_data(qin));
#endif
	if (queue_data(qin)==0) return ret;
	if (myds->input.mypkt==NULL) {
#ifdef DEBUG_buffer2array
		debug_print("%s\n", "Allocating a new packet");
#endif
#ifdef PKTALLOC
#ifdef DEBUG_pktalloc	
		debug_print("%s\n", "mypkt_alloc");
#endif
		myds->input.mypkt=mypkt_alloc(myds->sess);	
#else
		myds->input.mypkt=g_slice_alloc(sizeof(pkt));
#endif
		myds->input.mypkt->length=0;
	}	
	if ((myds->input.mypkt->length==0) && queue_data(qin)<sizeof(mysql_hdr)) {
		queue_zero(qin);
	}
	if ((myds->input.mypkt->length==0) && queue_data(qin)>=sizeof(mysql_hdr)) {
#ifdef DEBUG_buffer2array
		debug_print("%s\n", "Reading the header of a new packet");
#endif
		memcpy(&myds->input.hdr,queue_r_ptr(qin),sizeof(mysql_hdr));
		queue_r(qin,sizeof(mysql_hdr));
#ifdef DEBUG_buffer2array
		debug_print("Allocating %d bytes for a new packet\n", myds->input.hdr.pkt_length+sizeof(mysql_hdr));
#endif
		myds->input.mypkt->length=myds->input.hdr.pkt_length+sizeof(mysql_hdr);
		myds->input.mypkt->data=g_slice_alloc(myds->input.mypkt->length);
		memcpy(myds->input.mypkt->data, &myds->input.hdr, sizeof(mysql_hdr)); // immediately copy the header into the packet
		myds->input.partial=sizeof(mysql_hdr);
		ret+=sizeof(mysql_hdr);
	}
	if ((myds->input.mypkt->length>0) && queue_data(qin)) {
		int b= ( queue_data(qin) > (myds->input.mypkt->length-myds->input.partial) ? myds->input.mypkt->length-myds->input.partial : queue_data(qin) );
#ifdef DEBUG_buffer2array
		debug_print("Copied %d bytes into packet\n", b);
#endif
		memcpy(myds->input.mypkt->data + myds->input.partial, queue_r_ptr(qin),b);
		queue_r(qin,b);			
		myds->input.partial+=b;
		ret+=b;
	}
	if ((myds->input.mypkt->length>0) && (myds->input.mypkt->length==myds->input.partial) ) {
#ifdef DEBUG_buffer2array
		debug_print("Packet (%d bytes) completely read, moving into input.pkts array\n", myds->input.mypkt->length);
#endif
		g_ptr_array_add(myds->input.pkts, myds->input.mypkt);
		myds->pkts_recv+=1;
		myds->input.mypkt=NULL;
	}
#ifdef DEBUG_buffer2array
		debug_print("END : bytes in buffer = %d\n", queue_data(qin));
#endif
	return ret;
}

int array2buffer(mysql_data_stream_t *myds) {
	int ret=0;
	queue_t *qout = &myds->output.queue;
#ifdef DEBUG_array2buffer
		debug_print("Entering array2buffer with partial_send = %d and queue_available = %d\n", myds->output.partial, queue_available(qout));
#endif
	if (queue_available(qout)==0) return ret;	// no space to write
	if (myds->output.partial==0) { // read a new packet
		if (myds->output.pkts->len) {
#ifdef DEBUG_array2buffer
		debug_print("%s\n", "Removing a packet from array");
#endif
			if (myds->output.mypkt) {
#ifdef PKTALLOC
#ifdef DEBUG_pktalloc
				debug_print("%s\n", "mypkt_free");
#endif
				mypkt_free(myds->output.mypkt, myds->sess);	
#else
				g_slice_free1(sizeof(pkt), myds->output.mypkt);
#endif
			}
			myds->output.mypkt=g_ptr_array_remove_index(myds->output.pkts, 0);
		} else {
			return ret;
		}
	}
	int b= ( queue_available(qout) > (myds->output.mypkt->length - myds->output.partial) ? (myds->output.mypkt->length - myds->output.partial) : queue_available(qout) );
	memcpy(queue_w_ptr(qout), myds->output.mypkt->data + myds->output.partial, b);
	queue_w(qout,b);	
#ifdef DEBUG_array2buffer
	debug_print("Copied %d bytes into send buffer\n", b);
#endif	
	myds->output.partial+=b;
	ret=b;
	if (myds->output.partial==myds->output.mypkt->length) {
		g_slice_free1(myds->output.mypkt->length, myds->output.mypkt->data);
#ifdef DEBUG_array2buffer
		debug_print("%s\n", "Packet completely written into send buffer");
#endif
		myds->output.partial=0;
		myds->pkts_sent+=1;
	}
	return ret;
}


pkt * read_one_pkt_from_net(mysql_data_stream_t *myds) { // this should be used ONLY when sure that only 1 packet is expected, for example during authentication
	// loop until a packet is read
    while (myds->input.pkts->len==0) {
        read_from_net(myds);
        buffer2array(myds);
		if ((myds->active==FALSE)) {
			// the connection is broken , return NULL
			return NULL;
		}
    }
    return g_ptr_array_remove_index(myds->input.pkts, 0);
}

gboolean write_one_pkt_to_net(mysql_data_stream_t *myds, pkt *p) {// this should be used ONLY when sure that only 1 packet is expected, for example during authentication
	g_ptr_array_add(myds->output.pkts, p);
	array2buffer(myds);
	while (queue_data(&myds->output.queue) && (myds->active==TRUE)) {
		write_to_net(myds);
	}
	if (myds->active==FALSE) {
		return FALSE;
	}
}

int conn_poll(mysql_session_t *sess) {
	int r;
	struct pollfd *fds=sess->fds;
	fds[0].events=0;
	start_timer(sess->timers,TIMER_poll);
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
#ifdef DEBUG_poll
        debug_print("calling poll: fd %d events %d , fd %d events %d\n" , sess->fds[0].fd , sess->fds[0].events, sess->fds[1].fd , sess->fds[1].events);
#endif
	r=poll(fds,sess->nfds,glovars.mysql_poll_timeout);
	stop_timer(sess->timers,TIMER_poll);
	return r;
}

void read_from_net_2(mysql_session_t *sess) {
	// read_from_net for both sockets
	start_timer(sess->timers,TIMER_read_from_net);
	if ((sess->client_myds->fd > 0) && ((sess->fds[0].revents & POLLIN) == POLLIN)) {
#ifdef DEBUG_read_from_net
		debug_print("Calling read_from_net for %s\n", "client");
#endif
		read_from_net(sess->client_myds);
	}
	if (
		(sess->server_myds!=NULL) && // the backend is initialized
		(sess->server_myds->fd > 0) &&
		((sess->fds[1].revents & POLLIN) == POLLIN)) {
#ifdef DEBUG_read_from_net
		debug_print("Calling read_from_net for %s\n", "server");
#endif
		read_from_net(sess->server_myds);
	}
	stop_timer(sess->timers,TIMER_read_from_net);
}

void write_to_net_2(mysql_session_t *sess, int ignore_revents) {
	// write_to_net for both sockets
	start_timer(sess->timers,TIMER_write_to_net);
	if ((sess->client_myds->fd > 0) && ( ignore_revents || ((sess->fds[0].revents & POLLOUT) == POLLOUT) ) ) {
#ifdef DEBUG_write_to_net
		debug_print("Calling write_to_net for %s\n", "client");
#endif
		write_to_net(sess->client_myds);
			// if I wrote everything to client, start reading from client
//		  if ((queue_data(&conn->client_myds->output.queue)==0) && (conn->client_myds->output.pkts->len==0)) {
//			  conn->status |= CONNECTION_READING_CLIENT;  
//		  }
	}

	if (
		(sess->server_myds!=NULL) && // the backend is initialized
		(sess->server_myds->fd > 0)
		&& ( ignore_revents || ((sess->fds[1].revents & POLLOUT) == POLLOUT) ) ) {
#ifdef DEBUG_write_to_net
		debug_print("Calling write_to_net for %s\n", "server");
#endif
		write_to_net(sess->server_myds);
			// if I wrote everything to server, start reading from server
//		  if ((queue_data(&conn->server_myds->output.queue)==0) && (conn->server_myds->output.pkts->len==0)) {
//			  conn->status |= CONNECTION_READING_SERVER;  
//		  }
	}
	stop_timer(sess->timers,TIMER_write_to_net);
}

void buffer2array_2(mysql_session_t *sess) {
// buffer2array for both connections
#ifdef DEBUG_buffer2array
	debug_print("Calling buffer2array for %s\n", "client");
#endif
	start_timer(sess->timers,TIMER_buffer2array);
	while((buffer2array(sess->client_myds)) && (sess->client_myds->fd > 0) ) {}

	if (sess->server_myds!=NULL) { // the backend is initialized
#ifdef DEBUG_buffer2array
		debug_print("Calling buffer2array for %s\n", "server");
#endif
		while(buffer2array(sess->server_myds) && (sess->server_myds->fd > 0)) {}
	}
	stop_timer(sess->timers,TIMER_buffer2array);
}


void array2buffer_2(mysql_session_t *sess) {
	start_timer(sess->timers,TIMER_array2buffer);
#ifdef DEBUG_array2buffer
	debug_print("Calling array2buffer for %s\n", "client");
#endif
	while(array2buffer(sess->client_myds)) {}

	if (sess->server_myds!=NULL) { // the backend is initialized
#ifdef DEBUG_array2buffer
		debug_print("Calling array2buffer for %s\n", "server");
#endif
		while(array2buffer(sess->server_myds)) {}
	}
	stop_timer(sess->timers,TIMER_array2buffer);
}


void check_fds_errors(mysql_session_t *sess) {
	if ((sess->fds[0].revents == POLLERR) || (sess->fds[0].revents == POLLHUP) || (sess->fds[0].revents == POLLNVAL)) { 
		mysql_data_stream_shut_soft(sess->client_myds);
	}
	if (sess->server_myds!=NULL) { // the backend is initialized
		if ((sess->fds[1].revents == POLLERR) || (sess->fds[1].revents == POLLHUP) || (sess->fds[1].revents == POLLNVAL)) {
			mysql_data_stream_shut_soft(sess->server_myds);
		}
	}
}


gboolean sync_net(mysql_session_t *sess, int write_only) {
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
