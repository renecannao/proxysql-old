int listen_on_port(uint16_t);
int listen_on_unix(char *);
int connect_socket(char *, int);
inline void mysql_data_stream_shut_soft(mysql_data_stream_t *);
inline void mysql_data_stream_shut_hard(mysql_data_stream_t *);
int read_from_net(mysql_data_stream_t *);
int write_to_net(mysql_data_stream_t *);
int buffer2array(mysql_data_stream_t *);
int array2buffer(mysql_data_stream_t *);
pkt * read_one_pkt_from_net(mysql_data_stream_t *); // this should be used ONLY when sure that only 1 packet is expected, for example during authentication
gboolean write_one_pkt_to_net(mysql_data_stream_t *, pkt *); // this should be used ONLY when sure that only 1 packet is expected, for example during authentication
int conn_poll(mysql_session_t *);
void read_from_net_2(mysql_session_t *);
void write_to_net_2(mysql_session_t *, int);
void buffer2array_2(mysql_session_t *);
void array2buffer_2(mysql_session_t *);
void check_fds_errors(mysql_session_t *);
gboolean sync_net(mysql_session_t *, int);
