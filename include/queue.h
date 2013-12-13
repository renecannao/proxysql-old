void queue_init(queue_t *);
void queue_destroy(queue_t *);
inline void queue_zero(queue_t *);
inline int queue_available(queue_t *);
inline int queue_data(queue_t *);
inline void queue_r(queue_t *, int);
inline void queue_w(queue_t *, int);
inline void *queue_r_ptr(queue_t *);
inline void *queue_w_ptr(queue_t *);
