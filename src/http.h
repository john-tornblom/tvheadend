/*
 *  tvheadend, HTTP interface
 *  Copyright (C) 2007 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HTTP_H_
#define HTTP_H_

#include "htsbuf.h"

TAILQ_HEAD(http_arg_list, http_arg);

typedef struct http_arg {
  TAILQ_ENTRY(http_arg) link;
  char *key;
  char *val;
} http_arg_t;

#define HTTP_STATUS_SWICH_PROTO  101
#define HTTP_STATUS_OK           200
#define HTTP_STATUS_PARTIAL_CONTENT 206
#define HTTP_STATUS_FOUND        302
#define HTTP_STATUS_BAD_REQUEST  400
#define HTTP_STATUS_UNAUTHORIZED 401
#define HTTP_STATUS_NOT_FOUND    404


typedef struct http_connection {
  int hc_fd;
  struct sockaddr_storage *hc_peer;
  struct sockaddr_storage *hc_self;
  char *hc_representative;

  char *hc_url;
  char *hc_url_orig;
  int hc_keep_alive;

  htsbuf_queue_t hc_reply;

  struct http_arg_list hc_args;

  struct http_arg_list hc_req_args; /* Argumets from GET or POST request */

  enum {
    HTTP_CON_WAIT_REQUEST,
    HTTP_CON_READ_HEADER,
    HTTP_CON_END,
    HTTP_CON_POST_DATA,
  } hc_state;

  enum {
    HTTP_CMD_GET,
    HTTP_CMD_HEAD,
    HTTP_CMD_POST,
    RTSP_CMD_DESCRIBE,
    RTSP_CMD_OPTIONS,
    RTSP_CMD_SETUP,
    RTSP_CMD_TEARDOWN,
    RTSP_CMD_PLAY,
    RTSP_CMD_PAUSE,
  } hc_cmd;

  enum {
    HTTP_VERSION_1_0,
    HTTP_VERSION_1_1,
    RTSP_VERSION_1_0,
  } hc_version;

  char *hc_username;
  char *hc_password;

  struct config_head *hc_user_config;

  int hc_no_output;

  /* Support for HTTP POST */
  
  char *hc_post_data;
  unsigned int hc_post_len;

  void *hc_websocket_opaque;

} http_connection_t;


void http_arg_flush(struct http_arg_list *list);

char *http_arg_get(struct http_arg_list *list, const char *name);

void http_arg_set(struct http_arg_list *list, char *key, char *val);

int http_tokenize(char *buf, char **vec, int vecsize, int delimiter);

void http_error(http_connection_t *hc, int error);

void http_output_html(http_connection_t *hc);

void http_output_content(http_connection_t *hc, const char *content);

void http_redirect(http_connection_t *hc, const char *location);

void http_send_header(http_connection_t *hc, int rc, const char *content, 
		      int64_t contentlen, const char *encoding,
		      const char *location, int maxage, const char *range,
		      const char *disposition);

typedef int (http_callback_t)(http_connection_t *hc, 
			      const char *remain, void *opaque);

typedef struct http_path {
  LIST_ENTRY(http_path) hp_link;
  const char *hp_path;
  void *hp_opaque;
  http_callback_t *hp_callback;
  int hp_len;
  uint32_t hp_accessmask;
} http_path_t;

http_path_t *http_path_add(const char *path, void *opaque,
			   http_callback_t *callback, uint32_t accessmask);



void http_server_init(const char *bindaddr);

int http_access_verify(http_connection_t *hc, int mask);

void http_deescape(char *s);

#define WS_OPCODE_CONTINUATION     0
#define WS_OPCODE_TEXT             1
#define WS_OPCODE_BINARY           2
#define WS_OPCODE_CONNECTION_CLOSE 8
#define WS_OPCODE_PING             9
#define WS_OPCODE_PONG             10

typedef int (websocket_callback_init_t)(http_connection_t *hc,
					const char *remain);

typedef int (websocket_callback_data_t)(http_connection_t *hc,
					int opcode,
					const uint8_t *data,
					size_t len);

typedef void (websocket_callback_fini_t)(http_connection_t *hc);

typedef struct websocket_handler {
  const char *wsh_protocol;
  websocket_callback_init_t *wsh_init_cb;
  websocket_callback_data_t *wsh_data_cb;
  websocket_callback_fini_t *wsh_fini_cb;
} websocket_handler_t;


http_path_t * websocket_path_add(const char *path,
				 websocket_handler_t *wsh,
				 uint32_t accessmask);

int websocket_send(int fd, int opcode, const void *data, size_t len);

int websocket_sendq(int fd, int opcode, htsbuf_queue_t *hq);


#endif /* HTTP_H_ */
