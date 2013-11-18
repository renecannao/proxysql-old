//int __debug_code(const char *);


#ifdef DEBUG
#define DEBUG_read_from_net
#define DEBUG_write_to_net
#define DEBUG_buffer2array
#define DEBUG_array2buffer
#define DEBUG_shutfd
#define DEBUG_mysql_rw_split
#define DEBUG_poll
#define DEBUG_COM
#define DEBUG_auth
#define DEBUG_mysql_conn
#define DEBUG_pktalloc
#endif /* DEBUG */

#ifdef DEBUG
#define debug_print(fmt, ...) \
        do { if (DEBUG && glovars.verbose>=10) {   \
			unsigned long self; \
			struct timeval tv; \
			gettimeofday(&tv, NULL); \
			self= (unsigned long) pthread_self(); \
			glotimeold=glotimenew; \
			glotimenew=tv.tv_sec * 1000000 + tv.tv_usec; \
        	fprintf(stderr, "%lu %s:%d:%s() %ld.%ld ( %lld ) : " fmt, self, __FILE__, __LINE__, __func__, tv.tv_sec, tv.tv_usec, glotimenew-glotimeold , __VA_ARGS__); } \
        } while (0)
#else
#define debug_print(fmt, ...) 
#endif


inline void start_timer(timer *, enum enum_timer);
inline void stop_timer(timer *, enum enum_timer);
void * dump_timers();
void crash_handler(int);
