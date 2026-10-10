#ifndef __FORK_WEB_H__
#define __FORK_WEB_H__

/*
 * Fork-only web features (see FORK.md): the /setting page, HTTP Basic Auth for
 * the web UI, and bare-path access when app-path-prefix is set. Kept out of
 * upstream files so syncing with stackia/rtp2httpd only touches the hook lines
 * listed in FORK.md.
 */

#include "vendor/picohttpparser/picohttpparser.h"
#include <stddef.h>

typedef struct connection_s connection_t;

typedef struct {
  char authorization[512];
  char xff_last_hop[64]; /* Rightmost X-Forwarded-For entry: the one our proxy appended */
} fork_http_req_t;

typedef struct {
  char *setting_page_path;  /* Leading slash, e.g. "/setting" */
  char *setting_page_route; /* Same without the leading slash */
  char *web_auth_user;
  char *web_auth_password;
  int web_auth_require_local;
} fork_config_t;

extern fork_config_t fork_config;

void fork_config_init(void);

/* Mirror config_snapshot/config_restore_snapshot so a rolled-back reload also
 * rolls back fork settings. */
void fork_config_snapshot(void);
void fork_config_restore_snapshot(void);

int fork_config_parse_global(const char *param, const char *value);

void fork_http_consume_header(fork_http_req_t *ext, const struct phr_header *header);

/*
 * Runs after r2h-token validation, before upstream's status/player dispatch.
 * Enforces Basic Auth on the web UI routes and serves /setting and its API.
 * Returns 1 if the request was answered, 0 to continue upstream routing.
 */
int fork_web_route(connection_t *c, const char *service_path, size_t path_len);

#endif /* __FORK_WEB_H__ */
