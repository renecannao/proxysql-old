int listen_on_port(uint16_t);
int listen_on_unix(char *);
int connect_socket(char *, int);
gboolean write_one_pkt_to_net(mysql_data_stream_t *, pkt *); 


#define ioctl_FIONBIO(fd, mode) \
	{ \
		int ioctl_mode=mode; \
		ioctl(fd, FIONBIO, (char *)&ioctl_mode); \
	}
