int listen_on_port(uint16_t);
int listen_on_unix(char *);
int connect_socket(char *, int);
gboolean write_one_pkt_to_net(mysql_data_stream_t *, pkt *); 
