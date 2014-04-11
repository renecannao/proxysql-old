#include "proxysql.h"

#define SIZECHAR	sizeof(char)

// Added by chan ------------------------------------------------

// check char if it could be table name
inline char is_normal_char(char c)
{
	if(c >= 'a' && c <= 'z')
		return 1;
	if(c >= 'A' && c <= 'Z')
		return 1;
	if(c >= '0' && c <= '9')
		return 1;
	if(c == '$' || c == '_')
		return 1;
	return 0;
}

// token char - not table name string
inline char is_token_char(char c)
{
	return !is_normal_char(c);
}

// space - it's much easy to remove duplicated space chars
inline char is_space_char(char c)
{
	if(c == ' ' || c == '\t' || c == '\n' || c == '\r')
		return 1;
	return 0;
}

// check digit
inline char is_digit_char(char c)
{
	if(c >= '0' && c <= '9')
		return 1;
	return 0;
}

// check if it can be HEX char
inline char is_hex_char(char c)
{
	if((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
		return 1;
	return 0;
}

// between pointer, check string is number - need to be changed more functions
char is_digit_string(char *f, char *t)
{
	if(f == t)
	{
		if(is_digit_char(*f))
			return 1;
		else
			return 0;
	}

	int is_hex = 0;
	int i = 0;

	// 0x, 0X
	while(f != t)
	{
		if(i == 1 && *(f-1) == '0' && (*f == 'x' || *f == 'X'))
		{
			is_hex = 1;
		}

		// none hex
		else if(!is_hex && !is_digit_char(*f))
		{
			return 0;
		}

		// hex
		else if(is_hex && !is_hex_char(*f))
		{
			return 0;
		}
		f++;
		i++;
	}
	
	// need to be added function ----------------
	// 23e
	// 23e+1

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

// Added by chan end --------------------------------------------



// Added by chan
void process_query_stats(mysql_session_t *sess){
	char *s = sess->query_info.query;
	int len = sess->query_info.query_len;
	
	int i = 0;

	char *r = (char *) g_malloc(len + SIZECHAR);

	char *p_r = r;
	char *p_r_t = r;

	char prev_char = 0;
	char qutr_char = 0;

	char flag = 0;

	while(i < len)
	{
		// =================================================
		// START - read token char and set flag what's going on.
		// =================================================
		if(flag == 0)
		{
			// store current position
			p_r_t = p_r;

			// comment type 1 - start with '/*'
			if(prev_char == '/' && *s == '*')
			{
				flag = 1;
			}

			// comment type 2 - start with '#'
			else if(*s == '#')
			{
				flag = 2;
			}

			// string - start with '
			else if(*s == '\'' || *s == '"')
			{
				flag = 3;
				qutr_char = *s;
			}

			// may be digit - start with digit
			else if(is_token_char(prev_char) && is_digit_char(*s))
			{
				flag = 4;
				if(len == i+1)
					continue;
			}

			// not above case - remove duplicated space char
			else
			{
				flag = 0;
				if(is_space_char(prev_char) && is_space_char(*s)){
					prev_char = ' ';
					*p_r = ' ';
					s++;
					i++;
					continue;
				}
			}
		}

		// =================================================
		// PROCESS and FINISH - do something on each case
		// =================================================
		else
		{
			// --------
			// comment
			// --------
			if(
				// comment type 1 - /* .. */
				(flag == 1 && prev_char == '*' && *s == '/') ||
				
				// comment type 2 - # ... \n
				(flag == 2 && (*s == '\n' || *s == '\r'))
			)
			{
				p_r = flag == 1 ? p_r_t - SIZECHAR : p_r_t;
				prev_char = ' ';
				flag = 0;
				s++;
				i++;
				continue;
			}

			// --------
			// string
			// --------
			else if(flag == 3)
			{
				// Last char process
				if(len == i + 1)
				{
					p_r = p_r_t;
					*p_r++ = '?';
					flag = 0;
					break;
				}

				// need to be ignored case
				if(p_r > p_r_t + SIZECHAR)
				{
					if(
						(prev_char == '\\' && *s == '\\') ||		// to process '\\\\', '\\'
						(prev_char == '\\' && *s == qutr_char) ||	// to process '\''
						(prev_char == qutr_char && *s == qutr_char)	// to process ''''
					)
					{
						prev_char = 'X';
						s++;
						i++;
						continue;
					}
				}

				// satisfied closing string - swap string to ?
				if(*s == qutr_char && (len == i+1 || *(s + SIZECHAR) != qutr_char))
				{
						p_r = p_r_t;
						*p_r++ = '?';
						flag = 0;
						if(i < len)
							s++;
						i++;
						continue;
				}
			}

			// --------
			// digit
			// --------
			else if(flag == 4)
			{
				// last single char
				if(p_r_t == p_r)
				{
					*p_r++ = '?';
					i++;
					continue;
				}

				// token char or last char
				if(is_token_char(*s) || len == i+1)
				{
					if(is_digit_string(p_r_t, p_r))
					{
						p_r = p_r_t;
						*p_r++ = '?';
						if(len == i+1)
						{
							if(is_token_char(*s))
								*p_r++ = *s;
							i++;
							continue;
						}


					}
					flag = 0;
				}
			}
		}

		// =================================================
		// COPY CHAR
		// =================================================
		// convert every space char to ' '
		*p_r++ = !is_space_char(*s) ? *s : ' ';
		prev_char = *s++;

		i++;
	}
	*p_r = 0;

	// process query stats
	if(*r){
		// to save memory usage
		char *r2 = g_strdup(r);
		g_free(r);
		char *md5 = str2md5(r2);
		proxy_debug(PROXY_DEBUG_GENERIC, 1,  "%s => %s\n", md5, r2);
		qr_set(md5, r2);
	}
}
// Added by chan end.
