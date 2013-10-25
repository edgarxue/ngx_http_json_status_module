/* -*- mode:c; coding:utf-8 -*- */

#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "ngx_http_json_status_module.h"

/* #define DYNAMIC_ALLOCATE 1 // test */

static ngx_command_t
ngx_http_json_status_commands[] = {
  {
    ngx_string("status"),
    NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
    ngx_http_json_status,
    0,
    0,
    NULL
  },
  ngx_null_command
};

static ngx_http_module_t ngx_http_json_status_module_ctx = {
  NULL,                              /* preconfiguration */
  NULL,                              /* postconfiguration */

  ngx_http_json_status_create_main_conf, /* create main configuration */
  ngx_http_json_status_init_main_conf,   /* init main configuration */

  NULL,                              /* create server configuration */
  NULL,                              /* merge server configuration */

  NULL,                              /* create location configuration */
  NULL                               /* merge location configuration */
};

ngx_module_t ngx_http_json_status_module = {
  NGX_MODULE_V1,
  &ngx_http_json_status_module_ctx, /* module context */
  ngx_http_json_status_commands,    /* module directives */
  NGX_HTTP_MODULE,                      /* module type */
  NULL,                                 /* init master */
  NULL,                                 /* init module */
  NULL,                                 /* init process */
  NULL,                                 /* init thread */
  NULL,                                 /* exit thread */
  NULL,                                 /* exit process */
  NULL,                                 /* exit master */
  NGX_MODULE_V1_PADDING
};

static void *
ngx_http_json_status_create_main_conf(ngx_conf_t *cf)
{
  ngx_conf_log_error(NGX_LOG_DEBUG, cf, 0, "%s #start.", __FUNCTION__);

  ngx_http_json_status_main_conf_t *jsmcf;
  struct hostent *host;

  jsmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_json_status_main_conf_t));
  if (jsmcf == NULL) {
    return NULL;
  }

  if (gethostname(jsmcf->hostname, NGX_MAXHOSTNAMELEN) == -1) {
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "gethostname() failed");
    return NULL;
  }
  host = gethostbyname(jsmcf->hostname);
  if (host == NULL) {
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "gethostbyname() failed");
    return NULL;
  }
  ngx_sprintf(jsmcf->addr, "%d.%d.%d.%d",
	      (BYTE)*(host->h_addr),
	      (BYTE)*(host->h_addr + 1),
	      (BYTE)*(host->h_addr + 2),
	      (BYTE)*(host->h_addr + 3));

  ngx_conf_log_error(NGX_LOG_DEBUG, cf, 0, "hostname:%s, address:%s", &jsmcf->hostname, jsmcf->addr);

  return jsmcf;
}

static char *
ngx_http_json_status_init_main_conf(ngx_conf_t *cf, void *conf)
{
  ngx_conf_log_error(NGX_LOG_DEBUG, cf, 0, "#start. %s:%d", __FUNCTION__, __LINE__);
  return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_json_status_handler(ngx_http_request_t *r)
{
  ngx_http_upstream_main_conf_t    *umcf;
  ngx_http_json_status_main_conf_t *jsmcf;
  size_t                            size;
  ngx_buf_t                        *b;
  ngx_int_t                         rc;
  ngx_chain_t                       out;
  time_t                            now = time((time_t *)0);
  ngx_atomic_int_t                  ap, hn, ac, rq, rd, wr, wa, acc;
  ngx_uint_t                        i, j, k;

  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "#start. %s:%d", __FUNCTION__, __LINE__);

  umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);
  jsmcf = ngx_http_get_module_main_conf(r, ngx_http_json_status_module); /*ngx_http_request_t, module(ngx_module_t)*/

  /* GET or HEAD only */
  if (r->method != NGX_HTTP_GET && r->method != NGX_HTTP_HEAD) {
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "method = %d: GET = %d,HEAD = %d", r->method, NGX_HTTP_GET, NGX_HTTP_HEAD);
    return NGX_HTTP_NOT_ALLOWED;
  }

  /* request bodyは不要なので破棄する */
  rc = ngx_http_discard_request_body(r);
  if (rc != NGX_OK) {
    return rc;
  }

#ifdef DYNAMIC_ALLOCATE
  b = ngx_calloc_buf(r->pool);
#else
  /* upstreamsのサイズ計算 */
  size_t upstream_size = sizeof("\"upstreams\":{},");
  for (i = 0; i < umcf->upstreams.nelts; i++) {
    ngx_http_upstream_srv_conf_t *uscf   = ((ngx_http_upstream_srv_conf_t **)umcf->upstreams.elts)[i];
    ngx_http_upstream_rr_peers_t *peers  = uscf->peer.data;
    upstream_size += sizeof("\"\":[]")+sizeof(uscf->host)+ // upstream name
      (sizeof("{\"server\":\"\",\"backup\":\"\",\"weight\":\"\",\"state\":\"\",\"active\":\"\",\"keepalive\":\"\",\"requests\":\"\",\"responses\":\"\",\"sent\":\"\",\"received\":\"\",\"fails\":\"\",\"unavail\":\"\",\"health_checks\":\"\",\"downtime\":\"\",\"downstart\":\"\"}")+
       sizeof("{\"total\":\"\",\"1xx\":\"\",\"2xx\":\"\",\"3xx\":\"\",\"4xx\":\"\",\"5xx\":\"\"}")+ // responses
       sizeof("{\"checks\":\"\",\"fails\":\"\",\"unhealthy\":\"\",\"last_passed\":\"\"}")+ // health_checks
       //sizeof(ngx_uint_t)*10+ // responses + health_checks values
       sizeof("N/A")*10+     // responses + health_checks values (暫定)
       sizeof(ngx_str_t)*1+  // server
       sizeof(ngx_uint_t)*3+ // backup + weight + fails
       sizeof("unhealthy")+  // satte
       sizeof("N/A")*8       // etc(暫定)
       )*peers->number
      ;
  }

  /* 合計 */
  size = sizeof("{}")+
    /* server info */
    sizeof("\"version\":\"\",")+sizeof(NGX_HTTP_JSON_STATUS_MODULE_VERSION)+
    sizeof("\"nginx_version\":\"\",")+sizeof(NGINX_VERSION)+
    sizeof("\"address\":\"\",")+sizeof(jsmcf->addr)+
    sizeof("\"timestamp\":\"\",")+sizeof(time_t)+
    /* connections */
    sizeof("\"connections\":{},")+
    sizeof("\"accepted\":\"\",")+NGX_ATOMIC_T_LEN+
    sizeof("\"dropped\":\"\",")+NGX_ATOMIC_T_LEN+ // c_dropped = c_accepted.to_i - handled.to_i (newrelic_nginx_agent参照)
    sizeof("\"active\":\"\",")+NGX_ATOMIC_T_LEN+
    sizeof("\"idle\":\"\",")+NGX_ATOMIC_T_LEN+
    sizeof("\"counter\":\"\",")+NGX_ATOMIC_T_LEN+
    /* requests */
    sizeof("\"requests\":{},")+
    sizeof("\"total\":\"\",")+NGX_ATOMIC_T_LEN+
    sizeof("\"current\":\"\"")+NGX_ATOMIC_T_LEN+ // r_current = c_reading.to_i + c_writing.to_i (newrelic_nginx_agent参照)
    /* upstreams */
    upstream_size+
    /* terminate */
    sizeof("\0")
    ;
  b = ngx_create_temp_buf(r->pool, size);
#endif
  if (b == NULL) {
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }

  out.buf = b;
  out.next = NULL;

  /* NGX_STAT_STUBを有効にする(src/event/ngx_event.h) */
  acc = *ngx_connection_counter;
  ap = *ngx_stat_accepted;
  ac = *ngx_stat_active; // workerが異常終了した場合この値をリセットする必要がある
  hn = *ngx_stat_handled;
  rq = *ngx_stat_requests;
  rd = *ngx_stat_reading;
  wr = *ngx_stat_writing;
#if nginx_version >= 1003000
  wa = *ngx_stat_waiting; // ver1.4から？
#else
  wa = ac - (rd + wr);
#endif

#ifdef DYNAMIC_ALLOCATE
  b->pos = (u_char *) "{";
  b->last = b->pos + sizeof("{") - 1;
#else
  b->last = ngx_sprintf(b->last, "{"); /* contents start */
  b->last = ngx_sprintf(b->last, "\"version\":\"%s\",", NGX_HTTP_JSON_STATUS_MODULE_VERSION); /* module version */
  b->last = ngx_sprintf(b->last, "\"nginx_version\":\"%s\",", NGINX_VERSION);
  b->last = ngx_sprintf(b->last, "\"address\":\"%s\",", jsmcf->addr);
  b->last = ngx_sprintf(b->last, "\"timestamp\":\"%ld\",", now);
  b->last = ngx_sprintf(b->last, "\"connections\":{\"accepted\":\"%uA\",\"dropped\":\"%uA\",\"active\":\"%uA\",\"idle\":\"%uA\",\"counter\":\"%uA\"},", ap, ap-hn, ac, wa, acc);
  b->last = ngx_sprintf(b->last, "\"requests\":{\"total\":\"%uA\",\"current\":\"%uA\"},", rq, rd+wr);
  b->last = ngx_sprintf(b->last, "\"upstreams\":{");

  for (i = 0; i < umcf->upstreams.nelts; i++) {
    ngx_http_upstream_srv_conf_t *uscf   = ((ngx_http_upstream_srv_conf_t **)umcf->upstreams.elts)[i];
    ngx_http_upstream_rr_peers_t *peers  = uscf->peer.data;
    ngx_http_upstream_server_t   *server = uscf->servers->elts;

    if (i>0) {b->last = ngx_sprintf(b->last, ",", &peers->peer[j].name);}
    // upstream directive
    b->last = ngx_sprintf(b->last, "\"%V\":[", &uscf->host);

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "#upstream: %V", &uscf->host);

    for (j = 0; j < peers->number; j++) {

      // config情報
      ngx_uint_t config_backup = 0;
      ngx_uint_t config_down = 0;
      for (k = 0; k < uscf->servers->nelts; k++) {
	if (ngx_strtcmp(&peers->peer[j].name, &server[k].addrs[SERVER_ADDRS_ZERO].name) == 0) {
	  config_down = server[k].down;
	  config_backup = server[k].backup;
	  break;
	}
      }

      if (j>0) {b->last = ngx_sprintf(b->last, ",", &peers->peer[j].name);}
      b->last = ngx_sprintf(b->last, "{\"server\":\"%V\",\"backup\":\"%d\",\"weight\":\"%d\",\"state\":\"%s\",\"active\":\"%s\",\"keepalive\":\"%s\",\"requests\":\"%s\",\"responses\":%s,\"sent\":\"%s\",\"received\":\"%s\",\"fails\":\"%d\",\"unavail\":\"%s\",\"health_checks\":%s,\"downtime\":\"%s\",\"downstart\":\"%s\"}",
			    &peers->peer[j].name,
			    config_backup,
			    peers->peer[j].weight, //effective_weight, //current_weight,
			    (peers->peer[j].down == 1)?(u_char*)"down":(u_char*)"up", // Current state, which may be one of "up”, "down”, "unavail”, or "unhealthy"
			    (u_char *)"N/A", // active
			    (u_char *)"N/A", // keepalive
			    (u_char *)"N/A", // requests
			    (u_char *)"{\"total\":\"N/A\",\"1xx\":\"N/A\",\"2xx\":\"N/A\",\"3xx\":\"N/A\",\"4xx\":\"N/A\",\"5xx\":\"N/A\"}", // responses
			    (u_char *)"N/A", // sent
			    (u_char *)"N/A", // received
			    peers->peer[j].fails,
			    (u_char *)"N/A", // unavail
			    (u_char *)"{\"checks\":\"N/A\",\"fails\":\"N/A\",\"unhealthy\":\"N/A\",\"last_passed\":\"N/A\"}", // health_checks
			    (u_char *)"N/A", // downtime
			    (u_char *)"N/A"  // downstart
			    );
    }

    b->last = ngx_sprintf(b->last, "]", &uscf->host);
  }

  b->last = ngx_sprintf(b->last, "}");
  b->last = ngx_sprintf(b->last, "}"); /* contents end */
#endif

  b->memory = 1;
  b->flush = 1;
  b->last_buf = 1;
  b->last_in_chain = 1;

  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "#length %d %d", size, sizeof(b));

  ngx_str_set(&r->headers_out.content_type, "application/json; charset=utf-8");
  r->headers_out.status = NGX_HTTP_OK;
  r->headers_out.content_length_n = b->last - b->pos;
  rc = ngx_http_send_header(r);
  if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "#return. %s:%d", __FUNCTION__, __LINE__);
    return rc;
  }

  return ngx_http_output_filter(r, &out);
}

static char *
ngx_http_json_status(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
  ngx_http_core_loc_conf_t  *clcf;
  ngx_http_json_status_main_conf_t *mcf = conf;

  clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
  clcf->handler = ngx_http_json_status_handler;

  mcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_json_status_module);
  ngx_conf_log_error(NGX_LOG_DEBUG, cf, 0, "#hostname: %s. %s:%d", &mcf->hostname, __FUNCTION__, __LINE__);

  return NGX_CONF_OK;
}

/* ********** misc ********** */
static int
ngx_strtcmp(ngx_str_t *s1, ngx_str_t *s2) {
  if (s1->len == 0 || s2->len == 0 || s1->data == NULL || s2->data == NULL) {
    return 128;
  }
  if (s1->len == s2->len) {
    return ngx_strncmp(s1->data, s2->data, s1->len);
  }
  return ngx_strcmp(s1->data, s2->data);
}
