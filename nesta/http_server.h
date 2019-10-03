/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * The MIT License
 *
 * Copyright (c) 2008-2019 YAMAMOTO Naoki
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef _HTTP_SERVER_H_
#define _HTTP_SERVER_H_

#include "nestalib.h"   /* core functions */

#define PROGRAM_NAME "nesta"

#define DEFAULT_PORT 8080                /* http listen port */
#define DEFAULT_BACKLOG 50               /* listen backlog number */
#define DEFAULT_WORKER_THREADS 10        /* worker threads number */
#define DEFAULT_WORKER_THREAD_TIMEOUT 600         /* thread timeout(10 min) */
#define DEFAULT_WORKER_THREAD_CHECK_INTERVAL 1800 /* thread check interval(30 min) */
#define DEFAULT_KEEP_ALIVE_TIMEOUT 3     /* keep-alive timeout seconds */
#define DEFAULT_KEEP_ALIVE_REQUESTS 5    /* keep-alive max requests */

#define DEFAULT_SESSION_RELAY_PORT 9080         /* session relay listen port */
#define DEFAULT_SESSION_RELAY_BACKLOG 5         /* session relay listen backlog number */
#define DEFAULT_SESSION_RELAY_WORKER_THREADS 1  /* session relay worker threads number */
#define DEFAULT_SESSION_RELAY_CHECK_INTERVAL 300 /* session relay server check interval(5 min) */
#define ZONE_CAPACITY 20

/* http thread argument */
struct thread_args_t {
    SOCKET client_socket;
    struct sockaddr_in sockaddr;
};

/* http worker thread status */
#define WORKER_THREAD_UNUSE    0
#define WORKER_THREAD_SLEEPING 1
#define WORKER_THREAD_RUNNING  2

/* http worker thread info. */
struct worker_thread_info_t {
    int thread_no;                      /* thread number */
    int status;                         /* running, sleeping or unuse */ 
    int command_flag;                   /* executing command flag */
    unsigned long count;                /* request count */
    int64 last_access;                  /* last access time(micro seconds) */
};

/* program configuration */
struct http_conf_t {
    int daemonize;                      /* execute as daemon(Linux/MacOSX only) */
    char username[256];                 /* execute as username(Linux/MacOSX only) */
    ushort port_no;                     /* listen port number */
    int backlog;                        /* listen backlog number */
    int worker_threads;                 /* worker thread number */
    int extend_worker_threads;          /* extend worker thread number */
    int min_worker_threads;             /* min worker thread number(not parameter) */
    int max_worker_threads;             /* max worker thread number(not parameter) */
    int worker_thread_timeout;          /* worker thread timeout seconds */
    int worker_thread_check_interval;   /* worker thread timeout check interval time(sec) */
    int keep_alive_timeout;             /* keep-alive timeout seconds */
    int keep_alive_requests;            /* max keep-alive requests */
    char document_root[MAX_PATH+1];     /* document root */
    char access_log_fname[MAX_PATH+1];  /* access log file name */
    int daily_log_flag;                 /* daily access log */
    long file_cache_size;               /* file cache size(bytes) */
    char error_file[MAX_PATH+1];        /* error file name */
    char output_file[MAX_PATH+1];       /* output file name */
    int api_count;                      /* count of request hook APIs */
    struct hook_api_t* api_table;       /* request hook APIs */
    int init_api_count;                 /* count of initial hook APIs */
    HOOK_FUNCPTR* init_api_table;       /* initial hook APIs */
    int term_api_count;                 /* count of terminate hook APIs */
    HOOK_FUNCPTR* term_api_table;       /* terminate hook APIs */
    struct user_param_t u_param;        /* user parameter */
    struct vector_t* zone_table;        /* application zone table */
    char session_relay_host[MAX_HOSTNAME]; /* session relay my ip-addr(string) */
    ushort session_relay_port;          /* session relay port number */
    int session_relay_backlog;          /* session relay listen backlog number */
    int session_relay_worker_threads;   /* session relay worker thread number */
    int session_relay_check_interval;   /* session relay server check interval time(seconds) */
    /* session relay copy info. */
    int session_relay_copy_count;       /* count of copy host */
    char session_relay_copy_host[MAX_SESSION_RELAY_COPY][MAX_HOSTNAME];
    ushort session_relay_copy_port[MAX_SESSION_RELAY_COPY];
};

/* macros */
#define TRACE(fmt, ...) \
    if (g_trace_mode) { \
        fprintf(stdout, fmt, __VA_ARGS__); \
    }

#define is_session_relay() \
    (g_conf->session_relay_host[0] != '\0' && g_conf->session_relay_port > 0)

#ifdef _WIN32
#define get_abspath(abs_path, path, maxlen) \
    _fullpath(abs_path, path, maxlen)
#else
#define get_abspath(abs_path, path, maxlen) \
    realpath(path, abs_path)
#endif

/* global variables */
#ifndef _MAIN
    extern
#endif
struct http_conf_t* g_conf;     /* read only configure data */

#ifndef _MAIN
    extern
#endif
SOCKET g_listen_socket;         /* http listen socket */

#ifndef _MAIN
    extern
#endif
SOCKET g_session_relay_socket;  /* session relay listen socket */

#ifndef _MAIN
    extern
#endif
int g_shutdown_flag;        /* not zero is shutdown mode */

#ifndef _MAIN
    extern
#endif
int g_trace_mode;           /* not zero is trace mode */

#ifndef _MAIN
    extern
#endif
struct file_cache_t* g_file_cache;  /* file cache */

#ifndef _MAIN
    extern
#endif
struct queue_t* g_queue;  /* HTTP request queue */

#ifndef _MAIN
    extern
#endif
struct queue_t* g_session_relay_queue;  /* session relay request command queue */

#ifndef _MAIN
    extern
#endif
struct worker_thread_info_t* g_worker_thread_tbl;  /* worker thread table(max_worker_threads) */

#ifndef _MAIN
    extern
#endif
int64 g_http_start_time;  /* start time of http server */

#ifndef _MAIN
    extern
#endif
struct srelay_server_t* g_session_relay;    /* session relay server */

/* prototypes */
#ifdef __cplusplus
extern "C" {
#endif

/* config.c */
int config(const char* conf_fname, int start_mode);
int config_name_count(const char* conf_fname, const char* pname);

/* dynlib.c */
int dyn_api_load(struct appzone_t* zone, const char* app_name, const char* func_name, const char* lib_name);
int dyn_init_api_load(const char* func_name, const char* lib_name);
int dyn_term_api_load(const char* func_name, const char* lib_name);
void dyn_unload(void);

/* http_server.c */
void http_server(void);
SOCKET socket_listen(ulong addr, ushort port, int backlog, struct sockaddr_in* sockaddr);

/* document.c */
int check_file(const char* request_file);
int doc_send(SOCKET socket, struct in_addr addr, const char* root, const char* file_name, struct http_header_t* hdr, int keep_alive_timeout, int keep_alive_requests, int* res_size);

/* command.c */
void stop_server(void);
void status_server(void);
void trace_mode_server(const char* mode);

/* log.c */
void log_initialize(const char* fname, int daily_flag);
void log_write(struct request_t* req, int status, int content_size);
void log_finalize(void);

/* srelay_server.c */
int session_relay_server(void);
int request_session_relay(void);
void session_relay_close(void);

#ifdef __cplusplus
}
#endif

#endif  /* _HTTP_SERVER_H_ */
