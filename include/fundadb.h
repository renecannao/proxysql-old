

#define num_hashes 24
#define EXPIRE_DROPIT   0

/*
#define HASH_PURGETIME_USEC  10000000
#define HASH_PURGELOOP_USEC  100000
#define HASH_EXPIRE_MAX              365*24*3600
#define HASH_EXPIRE_DEFAULT  3600
*/
#define UDF_BUFFER_SIZE 255
#define UDF_BUFFER_YES  1
#define UDF_BUFFER_NO   2


/*
// the follow macro is used to verify if the connections were initialized
#define CHECK_HASH_INIT if ((__sync_fetch_and_add(&hash_initialized, 0))==0) { strcpy(message, "No avaliable servers"); return 1; }
#define CHECK_QUEUE_INIT if ((__sync_fetch_and_add(&queue_initialized, 0))==0) { strcpy(message, "No avaliable servers"); return 1; }
*/

#ifdef DEFINE_VARIABLES
unsigned int hash_initialized=0;
//unsigned int queue_initialized=0;
#else
extern unsigned int hash_initialized;
//extern unsigned int queue_initialized;
#endif /* DEFINE_VARIABLES */


EXTERN fdb_system_var_t fdb_system_var;
EXTERN fdb_hash_t **fdb_hashes;

struct __fdb_hash_t {
    pthread_rwlock_t lock;
    GHashTable *hash;
    GPtrArray *ptrArray;
    long long dataSize;
    long long purgeChunkSize;
    long long purgeIdx;
};

struct __fdb_hashes_group_t {
	fdb_hash_t **fdb_hashes;
	int size;
	time_t now;
    unsigned int hash_expire_default;
    unsigned long long cntDel;
    unsigned long long cntGet;
    unsigned long long cntGetOK;
    unsigned long long cntSet;
	unsigned long long cntPurge;
};

struct __fdb_hash_entry {
    char *key;
    char *value;
    fdb_hash_t *hash;
    struct __fdb_hash_entry *self;
    unsigned int klen;
    unsigned int length;
    time_t expire;
    time_t access;
};

pkt * fdb_get(fdb_hashes_group_t *, const char *, mysql_session_t *);
gboolean fdb_set(fdb_hashes_group_t * , void *, unsigned int , void *, unsigned int , time_t, gboolean);
long long fdb_del(UDF_INIT *initid, UDF_ARGS *args, char *is_null, char *error);
inline void hash_value_destroy_func(void *);
void fdb_hashes_new(fdb_hashes_group_t *, size_t, unsigned int);
void *purgeHash_thread(void *);
long long fdb_hash_init(UDF_INIT *initid, UDF_ARGS *args, char *is_null, char *error);
