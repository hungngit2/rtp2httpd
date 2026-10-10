#include "fork_web.h"
#include "configuration.h"
#include "connection.h"
#include "embedded_web.h"
#include "http.h"
#include "http_headers.h"
#include "settings_api.h"
#include "utils.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

fork_config_t fork_config = {0};
static fork_config_t fork_config_saved = {0};

static void fork_free_string(char **str) {
  free(*str);
  *str = NULL;
}

static int fork_parse_bool(const char *value) {
  return strcasecmp("on", value) == 0 || strcasecmp("true", value) == 0 || strcasecmp("yes", value) == 0 ||
         strcmp("1", value) == 0;
}

static void set_setting_page_path(const char *value) {
  char normalized[HTTP_URL_BUFFER_SIZE];
  size_t len = 0;
  const char *src = value;

  if (!value || value[0] == '\0') {
    logger(LOG_ERROR, "setting-page-path cannot be empty, keeping previous value");
    return;
  }

  while (*src == '/')
    src++;
  normalized[len++] = '/';
  while (*src && len < sizeof(normalized) - 1)
    normalized[len++] = *src++;
  if (*src != '\0') {
    logger(LOG_ERROR, "setting-page-path is too long, keeping previous value");
    return;
  }
  while (len > 1 && normalized[len - 1] == '/')
    len--;
  normalized[len] = '\0';

  fork_free_string(&fork_config.setting_page_path);
  fork_free_string(&fork_config.setting_page_route);
  fork_config.setting_page_path = strdup(normalized);
  fork_config.setting_page_route = strdup(normalized + 1);
}

void fork_config_init(void) {
  fork_free_string(&fork_config.web_auth_user);
  fork_free_string(&fork_config.web_auth_password);
  fork_config.web_auth_require_local = 0;
  set_setting_page_path("/setting");
}

static char *dup_or_null(const char *s) { return s ? strdup(s) : NULL; }

static void free_fork_config(fork_config_t *cfg) {
  fork_free_string(&cfg->setting_page_path);
  fork_free_string(&cfg->setting_page_route);
  fork_free_string(&cfg->web_auth_user);
  fork_free_string(&cfg->web_auth_password);
}

void fork_config_snapshot(void) {
  free_fork_config(&fork_config_saved);
  fork_config_saved.setting_page_path = dup_or_null(fork_config.setting_page_path);
  fork_config_saved.setting_page_route = dup_or_null(fork_config.setting_page_route);
  fork_config_saved.web_auth_user = dup_or_null(fork_config.web_auth_user);
  fork_config_saved.web_auth_password = dup_or_null(fork_config.web_auth_password);
  fork_config_saved.web_auth_require_local = fork_config.web_auth_require_local;
}

void fork_config_restore_snapshot(void) {
  free_fork_config(&fork_config);
  fork_config = fork_config_saved;
  memset(&fork_config_saved, 0, sizeof(fork_config_saved));
}

int fork_config_parse_global(const char *param, const char *value) {
  if (strcasecmp("setting-page-path", param) == 0) {
    set_setting_page_path(value);
    return 1;
  }
  if (strcasecmp("web-auth-user", param) == 0) {
    fork_free_string(&fork_config.web_auth_user);
    fork_config.web_auth_user = strdup(value);
    return 1;
  }
  if (strcasecmp("web-auth-password", param) == 0) {
    fork_free_string(&fork_config.web_auth_password);
    fork_config.web_auth_password = strdup(value);
    return 1;
  }
  if (strcasecmp("web-auth-require-local", param) == 0) {
    fork_config.web_auth_require_local = fork_parse_bool(value);
    return 1;
  }
  return 0;
}

void fork_http_consume_header(fork_http_req_t *ext, const struct phr_header *header) {
  if (!header->name)
    return;

  if (http_header_name_is(header, "Authorization")) {
    http_headers_copy_token(ext->authorization, sizeof(ext->authorization), header->value, header->value_len);
  } else if (http_header_name_is(header, "X-Forwarded-For")) {
    const char *start = header->value;
    const char *end = header->value + header->value_len;
    const char *p;

    for (p = end; p > header->value; p--) {
      if (p[-1] == ',') {
        start = p;
        break;
      }
    }
    while (start < end && (*start == ' ' || *start == '\t'))
      start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
      end--;
    http_headers_copy_token(ext->xff_last_hop, sizeof(ext->xff_last_hop), start, (size_t)(end - start));
  }
}

static int ipv4_addr_in_cidr(uint32_t addr, uint32_t base, int prefix_len) {
  uint32_t mask = (prefix_len == 0) ? 0 : (~0U << (32 - prefix_len));
  return (addr & mask) == (base & mask);
}

static int ipv4_is_local(uint32_t addr) {
  return ipv4_addr_in_cidr(addr, 0x7F000000, 8) ||  /* 127.0.0.0/8 */
         ipv4_addr_in_cidr(addr, 0x0A000000, 8) ||  /* 10.0.0.0/8 */
         ipv4_addr_in_cidr(addr, 0xAC100000, 12) || /* 172.16.0.0/12 */
         ipv4_addr_in_cidr(addr, 0xC0A80000, 16) || /* 192.168.0.0/16 */
         ipv4_addr_in_cidr(addr, 0xA9FE0000, 16);   /* 169.254.0.0/16 */
}

static int ipv6_is_local(const struct in6_addr *addr) {
  if (IN6_IS_ADDR_LOOPBACK(addr) || IN6_IS_ADDR_LINKLOCAL(addr))
    return 1;
  if ((addr->s6_addr[0] & 0xFE) == 0xFC) /* fc00::/7 unique-local */
    return 1;
  if (IN6_IS_ADDR_V4MAPPED(addr)) {
    uint32_t v4;
    memcpy(&v4, &addr->s6_addr[12], sizeof(v4));
    return ipv4_is_local(ntohl(v4));
  }
  return 0;
}

static int address_string_is_local(const char *ip_str) {
  struct in_addr v4;
  struct in6_addr v6;

  if (inet_pton(AF_INET, ip_str, &v4) == 1)
    return ipv4_is_local(ntohl(v4.s_addr));
  if (inet_pton(AF_INET6, ip_str, &v6) == 1)
    return ipv6_is_local(&v6);
  return 0;
}

static int socket_peer_is_local(const connection_t *c) {
  if (c->client_addr_len > 0 && c->client_addr.ss_family == AF_UNIX)
    return 1;
  if (c->client_addr.ss_family == AF_INET)
    return ipv4_is_local(ntohl(((const struct sockaddr_in *)&c->client_addr)->sin_addr.s_addr));
  if (c->client_addr.ss_family == AF_INET6)
    return ipv6_is_local(&((const struct sockaddr_in6 *)&c->client_addr)->sin6_addr);
  return 0;
}

/*
 * A remote peer is never local. Behind a local reverse proxy (xff on), judge the
 * rightmost X-Forwarded-For hop, which the proxy appended; entries to its left
 * come from the client and can be forged (upstream's x_forwarded_for is the
 * leftmost one, so it must not be used here).
 */
static int is_client_local(const connection_t *c) {
  if (!socket_peer_is_local(c))
    return 0;
  if (config.xff && c->http_req->fork.xff_last_hop[0] != '\0')
    return address_string_is_local(c->http_req->fork.xff_last_hop);
  return 1;
}

static int base64_char_value(char ch) {
  if (ch >= 'A' && ch <= 'Z')
    return ch - 'A';
  if (ch >= 'a' && ch <= 'z')
    return ch - 'a' + 26;
  if (ch >= '0' && ch <= '9')
    return ch - '0' + 52;
  if (ch == '+')
    return 62;
  if (ch == '/')
    return 63;
  return -1;
}

static int base64_decode(const char *input, char *output, size_t output_size) {
  size_t len = strlen(input);
  size_t pad = 0;
  size_t out_i = 0;

  if (len == 0 || len % 4 != 0)
    return -1;
  if (input[len - 1] == '=')
    pad++;
  if (input[len - 2] == '=')
    pad++;
  if ((len / 4) * 3 - pad >= output_size)
    return -1;

  for (size_t i = 0; i < len; i += 4) {
    int is_last_block = (i + 4 == len);

    if (!is_last_block && (input[i + 2] == '=' || input[i + 3] == '='))
      return -1;
    if (is_last_block && input[i + 2] == '=' && input[i + 3] != '=')
      return -1;

    int v0 = base64_char_value(input[i]);
    int v1 = base64_char_value(input[i + 1]);
    int v2 = (input[i + 2] == '=') ? 0 : base64_char_value(input[i + 2]);
    int v3 = (input[i + 3] == '=') ? 0 : base64_char_value(input[i + 3]);
    if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0)
      return -1;

    uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) | ((uint32_t)v2 << 6) | (uint32_t)v3;
    output[out_i++] = (char)((triple >> 16) & 0xFF);
    if (input[i + 2] != '=')
      output[out_i++] = (char)((triple >> 8) & 0xFF);
    if (input[i + 3] != '=')
      output[out_i++] = (char)(triple & 0xFF);
  }
  output[out_i] = '\0';
  return (int)out_i;
}

static int is_web_auth_valid(const connection_t *c) {
  static const char prefix[] = "Basic ";
  const char *auth = c->http_req->fork.authorization;
  char decoded[256];
  char *colon;

  if (strncasecmp(auth, prefix, sizeof(prefix) - 1) != 0)
    return 0;
  if (base64_decode(auth + sizeof(prefix) - 1, decoded, sizeof(decoded)) < 0)
    return 0;
  colon = strchr(decoded, ':');
  if (!colon)
    return 0;
  *colon = '\0';
  return strcmp(decoded, fork_config.web_auth_user) == 0 && strcmp(colon + 1, fork_config.web_auth_password) == 0;
}

static void send_401_basic(connection_t *c) {
  static const char body[] = "<!doctype html><title>401</title>Unauthorized";
  size_t body_len = sizeof(body) - 1;

  send_http_headers(c, STATUS_401, "text/html; charset=utf-8", "WWW-Authenticate: Basic realm=\"rtp2httpd\"\r\n");
  if (c->request_is_head || strcasecmp(c->http_req->method, "HEAD") == 0)
    body_len = 0;
  connection_queue_output_and_flush(c, body_len ? (const uint8_t *)body : NULL, body_len);
}

static int path_equals(const char *path, size_t path_len, const char *route) {
  size_t route_len = strlen(route);
  return route_len == path_len && strncmp(path, route, path_len) == 0;
}

static int path_has_prefix(const char *path, size_t path_len, const char *prefix) {
  size_t prefix_len = strlen(prefix);
  return path_len >= prefix_len && strncmp(path, prefix, prefix_len) == 0;
}

static void route_with_suffix(char *out, size_t out_size, const char *route, const char *suffix) {
  if (route[0] != '\0')
    snprintf(out, out_size, "%s/%s", route, suffix);
  else
    snprintf(out, out_size, "%s", suffix);
}

static int in_web_auth_scope(const char *path, size_t path_len, const char *setting_route,
                             const char *setting_api_prefix) {
  const char *status_route = config.status_page_route ? config.status_page_route : "status";
  const char *player_route = config.player_page_route ? config.player_page_route : "player";
  char status_sse[HTTP_URL_BUFFER_SIZE];
  char status_api[HTTP_URL_BUFFER_SIZE];

  route_with_suffix(status_sse, sizeof(status_sse), status_route, "sse");
  route_with_suffix(status_api, sizeof(status_api), status_route, "api/");

  return path_equals(path, path_len, status_route) || path_equals(path, path_len, status_sse) ||
         path_has_prefix(path, path_len, status_api) || path_equals(path, path_len, player_route) ||
         path_equals(path, path_len, setting_route) || path_has_prefix(path, path_len, setting_api_prefix);
}

/* Browsers resend cached Basic credentials for every deeper path (e.g. /tv/http/...),
 * and upstream forwards raw_headers to /http/ sources, so drop ours before routing. */
static void strip_web_auth_header(http_request_t *req) {
  static const char name[] = "Authorization:";
  char *line = req->raw_headers;
  char *end = req->raw_headers + req->raw_headers_len;

  while (line < end) {
    char *next = memchr(line, '\n', (size_t)(end - line));
    next = next ? next + 1 : end;
    if ((size_t)(next - line) >= sizeof(name) - 1 && strncasecmp(line, name, sizeof(name) - 1) == 0) {
      memmove(line, next, (size_t)(end - next));
      req->raw_headers_len -= (size_t)(next - line);
      req->raw_headers[req->raw_headers_len] = '\0';
      return;
    }
    line = next;
  }
}

int fork_web_route(connection_t *c, const char *service_path, size_t path_len) {
  const char *setting_route = fork_config.setting_page_route ? fork_config.setting_page_route : "setting";
  char setting_api_prefix[HTTP_URL_BUFFER_SIZE];

  route_with_suffix(setting_api_prefix, sizeof(setting_api_prefix), setting_route, "api/");

  if (fork_config.web_auth_user && fork_config.web_auth_user[0] && fork_config.web_auth_password &&
      fork_config.web_auth_password[0]) {
    int auth_valid = is_web_auth_valid(c);

    if (in_web_auth_scope(service_path, path_len, setting_route, setting_api_prefix) && !auth_valid &&
        (fork_config.web_auth_require_local || !is_client_local(c))) {
      send_401_basic(c);
      return 1;
    }
    if (auth_valid)
      strip_web_auth_header(c->http_req);
  }

  if (path_equals(service_path, path_len, setting_route)) {
    handle_embedded_file(c, "/setting.html");
    return 1;
  }

  if (path_has_prefix(service_path, path_len, setting_api_prefix)) {
    const char *api_name = service_path + strlen(setting_api_prefix);
    size_t api_name_len = path_len - strlen(setting_api_prefix);

    if (path_equals(api_name, api_name_len, "get-config"))
      handle_get_config(c);
    else if (path_equals(api_name, api_name_len, "save-config"))
      handle_save_config(c);
    else
      http_send_404(c);
    return 1;
  }

  return 0;
}
