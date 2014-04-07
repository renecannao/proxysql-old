#include "proxysql.h"
long monotonic_time() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (((long) ts.tv_sec) * 1000000) + (ts.tv_nsec / 1000);
}

// Added by chan
char is_token(char c){
  if(c <= 47)
    return 1;
  if(c >= 58 && c <= 64)
    return 1;
  if(c >= 91 && c <= 94)
    return 1;
  if(c >= 123 && c <= 127)
    return 1;
  return 0;
}

char is_digit(char *from, char *to){
  if(from == to) return 0;
  int i = 0;
  while(from != to){
    if(i++ == 1 && (*from == 'x' || *from == 'X') && *(from-1) == '0')
      return 1;
    if(*from < '0' || *from > '9'){
      return 0;
    }
    from++;
  }
  return 1;
}

// need to be changed - I've got this code from google result. :)
char *str2md5(const char *str) {
  int n;
  MD5_CTX c;
  unsigned char digest[16];
  char *out = (char*)g_malloc(33);
  MD5_Init(&c);
  int length = strlen(str);

  while (length > 0) {
    if (length > 512) {
      MD5_Update(&c, str, 512);
    } else {
      MD5_Update(&c, str, length);
    }
    length -= 512;
    str += 512;
  }

  MD5_Final(digest, &c);
  for (n = 0; n < 16; ++n) {
    snprintf(&(out[n*2]), 16*2, "%02x", (unsigned int)digest[n]);
  }
  return out;
}
// Added by chan end.
