#ifndef __SETTINGS_API_H__
#define __SETTINGS_API_H__

#include "connection.h"

/* GET: returns the live configuration as JSON. */
void handle_get_config(connection_t *c);

/* POST: rewrites rtp2httpd.conf from a form-urlencoded body and reloads. */
void handle_save_config(connection_t *c);

#endif /* __SETTINGS_API_H__ */
