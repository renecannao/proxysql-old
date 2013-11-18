#undef EXTERN
#undef INITIALIZER

#ifdef DEFINE_VARIABLES
#define EXTERN                  extern
#define INITIALIZER(...)        /* nothing */
#else
#define EXTERN                  /* nothing */
#define INITIALIZER(...)        = __VA_ARGS__
#endif /* DEFINE_VARIABLES */
