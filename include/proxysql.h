#include <search.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <netinet/in.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <sys/syscall.h>

#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <poll.h>
#include <glib.h>
#include <execinfo.h>

#include <my_global.h>
#include <mysql.h>
#include <mysql_com.h>
#include <mysql/plugin.h>

#include "sqlite3.h"
#include "external.h"
#include "structs.h"
#include "queue.h"
#include "mysql_protocol.h"
#include "mysql_connpool.h"
#include "mysql_handler.h"
#include "network.h"
#include "fundadb.h"
#include "threads.h"
#include "global_variables.h"
#include "debug.h"
#include "mem.h"
#include "free_pkts.h"
#include "admin_sqlite.h"

#undef max
#define max(x,y) ((x) > (y) ? (x) : (y))

/* PANIC() is a exit with message */
void PANIC(char* msg);
#define PANIC(msg)  { perror(msg); exit(EXIT_FAILURE); }
/*
#ifdef DEBUG
#define debug_print(fmt, ...) \
        do { if (DEBUG) fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, \
                                __LINE__, __func__, __VA_ARGS__); } while (0)
#else
#define debug_print(fmt, ...) 
#endif
*/
