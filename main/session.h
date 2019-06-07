#ifndef GG_SESSION
#define GG_SESSION


typedef struct {
	char x_auth_balance[128];
	char x_auth_token[128];
	char x_auth_userid[32];
	char x_auth_usertype[32];
} GGAuth;

extern GGAuth session;


/* TODO: */
void save_session ();
void load_session ();

#endif
