struct _debug_level {
    enum debug_module module;
    int verbosity;
    char *name;
};


#ifdef DEBUG
//#define DEBUG_read_from_net
//#define DEBUG_write_to_net
//#define DEBUG_buffer2array
//#define DEBUG_array2buffer
//#define DEBUG_shutfd
//#define DEBUG_mysql_rw_split
//#define DEBUG_poll
//#define DEBUG_COM
//#define DEBUG_auth
//#define DEBUG_mysql_conn
//#define DEBUG_pktalloc
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

#ifdef DEBUG
#define proxy_debug(module, verbosity, fmt, ...) \
	do { if (gdbg) { \
	proxy_debug_func(module, verbosity, "%d:%s:%d:%s(): LVL#%d : " fmt, syscall(SYS_gettid), __FILE__, __LINE__, __func__ , verbosity , ## __VA_ARGS__); \
	} } while (0)
#else
#define proxy_debug(module, verbosity, fmt, ...)
#endif

#ifdef DEBUG
#define proxy_error(fmt, ...) proxy_error_func("%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__ , ## __VA_ARGS__);
#else
#define proxy_error(fmt, ...) proxy_error_func(fmt , ## __VA_ARGS__);
#endif

void proxy_debug_func(enum debug_module, int, const char *, ...);
void proxy_error_func(const char *, ...);
inline void start_timer(timer *, enum enum_timer);
inline void stop_timer(timer *, enum enum_timer);
void * dump_timers();
void crash_handler(int);
void init_debug_struct();
