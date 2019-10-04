/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * The MIT License
 *
 * Copyright (c) 2008-2010 YAMAMOTO Naoki
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _MAIN
#include "http_server.h"

#ifndef WIN32
#include <pwd.h>
#endif

#define DEFAULT_CONF_FILE  "./conf/" PROGRAM_NAME ".conf"

#define ACT_START  0
#define ACT_STOP   1
#define ACT_STATUS 2
#define ACT_TRACE  3

static char* conf_file = NULL;  /* config file name */
static int action = ACT_START;  /* ACT_START, ACT_STOP, ACT_STATUS, ACT_TRACE */
static char* act_value = NULL;

static int shutdown_done_flag = 0;  /* shutdown済みフラグ */
static int cleanup_done_flag = 0;   /* cleanup済みフラグ */

static CS_DEF(shutdown_lock);
static CS_DEF(cleanup_lock);

static void version()
{
    fprintf(stdout, "%s\n", SERVER_NAME);
    fprintf(stdout, "Copyright (c) 2008-2010 YAMAMOTO Naoki\n\n");
}

static void usage()
{
    version();
    fprintf(stdout, "usage: %s [-start | -stop | -status | -trace {on|off} -version] [-f conf.file]\n\n", PROGRAM_NAME);
}

static int call_init_api(int count, HOOK_FUNCPTR* api_table)
{
    HOOK_FUNCPTR* api;
    int result = 0;

    api = api_table;
    while (count--) {
        if (*api == NULL)
            return 0;
        result = (*api)(&g_conf->u_param);
        if (result != 0)
            return result;
        api++;
    }
    return result;
}

static int call_term_api(int count, HOOK_FUNCPTR* api_table)
{
    HOOK_FUNCPTR* api;

    api = api_table;
    while (count--) {
        if (*api == NULL)
            return 0;
        (*api)(&g_conf->u_param);
        api++;
    }
    return 0;
}

static void cleanup()
{
    /* Ctrl-Cで終了させたときに Windowsでは http_server()の
       メインループでも割り込みが起きて後処理のcleanup()が
       呼ばれるため、１回だけ実行されるように制御します。*/
    CS_START(&cleanup_lock);
    if (! cleanup_done_flag) {
        if (g_session_relay_socket != INVALID_SOCKET) {
            shutdown(g_session_relay_socket, 2);  /* 2: RDWR stop */
            SOCKET_CLOSE(g_session_relay_socket);
        }
        if (g_listen_socket != INVALID_SOCKET) {
            shutdown(g_listen_socket, 2);  /* 2: RDWR stop */
            SOCKET_CLOSE(g_listen_socket);
        }
        /* 終了 API の呼び出し */
        if (action == ACT_START) {
            if (g_conf->term_api_count > 0) {
                call_term_api(g_conf->term_api_count, g_conf->term_api_table);
                TRACE("called terminate APIs(%d).\n", g_conf->term_api_count);
            }
        }
        dyn_unload();

        if (action == ACT_START) {
            int zone_c;
            int i;

            zone_c = vect_count(g_conf->zone_table);
            for (i = 0; i < zone_c; i++) {
                struct appzone_t* z;
                z = vect_get(g_conf->zone_table, i);
                if (z != NULL) {
                    if (z->max_session != 0) {
                        ssn_finalize(z->zone_session);
                        TRACE("%s zone terminated.\n", z->zone_name);
                    }
                    free(z);
                }
            }
            /* セッション・リレーを終了します。*/
            if (is_session_relay()) {
                srelay_finalize(g_session_relay);
                TRACE("%s terminated.\n", "session relay");
            }
        }

        if (action == ACT_START) {
            vect_finalize(g_conf->zone_table);
            log_finalize();
            TRACE("%s terminated.\n", "log");
            if (g_file_cache != NULL) {
                fc_finalize(g_file_cache);
                TRACE("%s terminated.\n", "file cache");
            }
            if (g_session_relay_queue != NULL) {
                que_finalize(g_session_relay_queue);
                TRACE("%s terminated.\n", "session relay queue");
            }
            if (g_worker_thread_tbl != NULL) {
                free(g_worker_thread_tbl);
            }
            que_finalize(g_queue);
            TRACE("%s terminated.\n", "request queue");
        }
        logout_finalize();
        err_finalize();
        sock_finalize();
        mt_finalize();
        cleanup_done_flag = 1;
    }
    CS_END(&cleanup_lock);
}

static void sig_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        /* Windowsでは、メインスレッドのみで割り込みがかかるが、
           pthreadではすべてのワーカースレッドに割り込みがかかるため、
           プログラムの停止を１回のみ実行するように制御します。*/
        CS_START(&shutdown_lock);
        if (! shutdown_done_flag) {
            cleanup();
            shutdown_done_flag = 1;
            printf("\n%s was terminated.\n", SERVER_NAME);
        }
        exit(0);
        CS_END(&shutdown_lock);
#ifndef _WIN32
    } else if (signo == SIGPIPE) {
        /* ignore */
#endif
    }
}

static int startup()
{
    /* グローバル変数の初期化 */
    g_listen_socket = INVALID_SOCKET;
    g_session_relay_socket = INVALID_SOCKET;

    /* 割り込み処理用のクリティカルセクション初期化 */
    CS_INIT(&shutdown_lock);
    CS_INIT(&cleanup_lock);

    /* マルチスレッド対応関数の初期化 */
    mt_initialize();

    /* ソケット関数の初期化 */
    sock_initialize();

    /* エラーファイルの初期化 */
    err_initialize(g_conf->error_file);

    /* アウトプットファイルの初期化 */
    logout_initialize(g_conf->output_file);

    if (action == ACT_START) {
        int i;

        /* HTTPリクエスト・キューの初期化 */
        g_queue = que_initialize();
        if (g_queue == NULL)
            return -1;
        TRACE("%s initialized.\n", "request queue");

        /* ワーカースレッド情報の初期化 */
        g_worker_thread_tbl =
            (struct worker_thread_info_t*)calloc(g_conf->max_worker_threads,
                                                 sizeof(struct worker_thread_info_t));
        if (g_worker_thread_tbl == NULL) {
            fprintf(stderr, "no memory.\n");
            return -1;
        }
        for (i = 0; i < g_conf->max_worker_threads; i++) {
            g_worker_thread_tbl[i].thread_no = i + 1;
        }

        /* セッション・リレー・キューの初期化 */
        if (is_session_relay()) {
            g_session_relay_queue = que_initialize();
            if (g_session_relay_queue == NULL)
                return -1;
            TRACE("%s initialized.\n", "session relay queue");
        }

        /* ファイルキャッシュの初期化 */
        if (g_conf->file_cache_size > 0) {
            g_file_cache = fc_initialize(g_conf->file_cache_size);
            if (g_file_cache) {
                TRACE("file cache initialized(%ld bytes).\n", g_conf->file_cache_size);
            }
        }

        /* アクセスログの初期化 */
        log_initialize(g_conf->access_log_fname, g_conf->daily_log_flag);
        TRACE("%s initialized.\n", "log");
    }

    /* セッションリレーの初期化を行ないます。*/
    if (action == ACT_START) {
        if (is_session_relay()) {
            ulong my_addr;
            int i;
            int n = 0;
            char* host_tbl[MAX_SESSION_RELAY_COPY];
            ushort port_tbl[MAX_SESSION_RELAY_COPY];

            /* セッションをコピーするサーバー名とポート番号を設定します。
             * 最大値は MAX_SESSION_RELAY_COPY になります。
             * 自分自身へはコピーしないようにします。
             */
            my_addr = inet_addr(g_conf->session_relay_host);
            for (i = 0; i < g_conf->session_relay_copy_count; i++) {
                ulong target_addr;

                target_addr = inet_addr(g_conf->session_relay_copy_host[i]);
                if (my_addr != target_addr) {
                    /* 自分自身へはコピーしないようにチェックします。*/
                    if (n >=  MAX_SESSION_RELAY_COPY)
                        break;
                    host_tbl[n] = (char*)alloca(strlen(g_conf->session_relay_copy_host[i])+1);
                    strcpy(host_tbl[n], g_conf->session_relay_copy_host[i]);
                    port_tbl[n] = g_conf->session_relay_copy_port[i];
                    n++;
                }
            }

            /* セッション・リレーのサーバー情報を初期化します。*/
            g_session_relay = srelay_initialize(n,
                                                (const char**)host_tbl,
                                                port_tbl,
                                                g_conf->session_relay_check_interval,
                                                inet_addr(g_conf->session_relay_host),
                                                g_conf->session_relay_port);
            if (g_session_relay)
                TRACE("%s initialized.\n", "session relay");
        }
    }

    /* セッション管理の初期化をゾーン毎に行ないます。*/
    if (action == ACT_START) {
        int zone_c;
        int i;

        zone_c = vect_count(g_conf->zone_table);
        for (i = 0; i < zone_c; i++) {
            struct appzone_t* z;

            z = vect_get(g_conf->zone_table, i);
            if (z == NULL)
                return -1;
            if (z->max_session != 0) {
                /* セッション管理を作成します。*/
                z->zone_session = ssn_initialize(z->zone_name,
                                                 z->max_session,
                                                 z->session_timeout,
                                                 g_session_relay);
                if (z->zone_session == NULL)
                    return -1;
                TRACE("%s zone initialized.\n", z->zone_name);
            }
        }
    }

    /* 初期化 API の呼び出し */
    if (action == ACT_START) {
        if (g_conf->init_api_count > 0) {
            if (call_init_api(g_conf->init_api_count, g_conf->init_api_table) != 0)
                return -1;
            TRACE("called initial APIs(%d).\n", g_conf->init_api_count);
        }
    }

    /* 割り込みハンドラーの登録 */
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
#ifndef _WIN32
    signal(SIGPIPE, sig_handler);
#endif
    return 0;
}

static int parse(int argc, char* argv[])
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp("-start", argv[i]) == 0) {
            action = ACT_START;
        } else if (strcmp("-stop", argv[i]) == 0) {
            action = ACT_STOP;
        } else if (strcmp("-status", argv[i]) == 0) {
            action = ACT_STATUS;
        } else if (strcmp("-trace", argv[i]) == 0) {
            action = ACT_TRACE;
            if (++i < argc) {
                if (stricmp(argv[i], "on") && stricmp(argv[i], "off"))
                    return -1;
                act_value = argv[i];
            } else {
                return -1;
            }
        } else if (strcmp("-version", argv[i]) == 0 ||
                   strcmp("--version", argv[i]) == 0) {
            version();
            return 1;
        } else if (strcmp("-f", argv[i]) == 0) {
            if (++i < argc)
                conf_file = argv[i];
            else {
                fprintf(stdout, "no config file.\n");
                return -1;
            }
        } else {
            return -1;
        }
    }
    return 0;
}

static int parse_config()
{
    /* コンフィグ領域の確保 */
    g_conf = (struct http_conf_t*)calloc(1, sizeof(struct http_conf_t));
    if (g_conf == NULL) {
        fprintf(stderr, "no memory.\n");
        return -1;
    }

    /* デフォルトのポート番号を設定します。*/
    g_conf->port_no = DEFAULT_PORT;

    /* デフォルトのbacklogを設定します。*/
    g_conf->backlog = DEFAULT_BACKLOG;

    /* デフォルトのworker_threadsを設定します。*/
    g_conf->worker_threads = DEFAULT_WORKER_THREADS;

    g_conf->worker_thread_timeout = DEFAULT_WORKER_THREAD_TIMEOUT;
    g_conf->worker_thread_check_interval = DEFAULT_WORKER_THREAD_CHECK_INTERVAL;

    /* デフォルトのkeep-aliveを設定します。*/
    g_conf->keep_alive_timeout = DEFAULT_KEEP_ALIVE_TIMEOUT;
    g_conf->keep_alive_requests = DEFAULT_KEEP_ALIVE_REQUESTS;

    /* コンフィグファイル名がパラメータで指定されていない場合は
       デフォルトのファイル名を使用します。*/
    if (conf_file == NULL)
        conf_file = DEFAULT_CONF_FILE;

    /* アプリケーション・ゾーンの領域を確保します。*/
    g_conf->zone_table = vect_initialize(ZONE_CAPACITY);
    if (g_conf->zone_table == NULL) {
        fprintf(stderr, "no memory.\n");
        return -1;
    }

    /* コンフィグファイルに記述されているhook関数の数を調べます。*/
    if (action == ACT_START) {
        int n;

        n = config_name_count(conf_file, ".api");
        if (n > 0)
            g_conf->api_table = (struct hook_api_t*)calloc(n, sizeof(struct hook_api_t));

        n = config_name_count(conf_file, ".init_api");
        if (n > 0)
            g_conf->init_api_table = (HOOK_FUNCPTR*)calloc(n, sizeof(HOOK_FUNCPTR));

        n = config_name_count(conf_file, ".term_api");
        if (n > 0)
            g_conf->term_api_table = (HOOK_FUNCPTR*)calloc(n, sizeof(HOOK_FUNCPTR));
    }

    /* コンフィグファイルの解析 */
    if (config(conf_file, (action == ACT_START)) < 0)
        return -1;

    /* ワーカースレッド数の調整（最小、最大）*/
    if (g_conf->extend_worker_threads > 0) {
        g_conf->min_worker_threads = g_conf->worker_threads;
        g_conf->max_worker_threads = g_conf->worker_threads + g_conf->extend_worker_threads;
    } else {
        g_conf->min_worker_threads =
        g_conf->max_worker_threads = g_conf->worker_threads;
    }

    /* セッション・リレーが指定されている場合で
       パラメータに値が設定されていない場合は初期値を設定します。*/
    if (g_conf->session_relay_host[0]) {
        if (g_conf->session_relay_port == 0)
            g_conf->session_relay_port = DEFAULT_SESSION_RELAY_PORT;
        if (g_conf->session_relay_backlog == 0)
            g_conf->session_relay_backlog = DEFAULT_SESSION_RELAY_BACKLOG;
        if (g_conf->session_relay_worker_threads == 0)
            g_conf->session_relay_worker_threads = DEFAULT_SESSION_RELAY_WORKER_THREADS;
        if (g_conf->session_relay_check_interval == 0)
            g_conf->session_relay_check_interval = DEFAULT_SESSION_RELAY_CHECK_INTERVAL;

        /* セッション・コピーの初期値を設定します。*/
        {
            int i;

            if (g_conf->session_relay_copy_count > MAX_SESSION_RELAY_COPY)
                g_conf->session_relay_copy_count = MAX_SESSION_RELAY_COPY;
            for (i = 0; i < g_conf->session_relay_copy_count; i++) {
                if (g_conf->session_relay_copy_port[i] == 0)
                    g_conf->session_relay_copy_port[i] = DEFAULT_SESSION_RELAY_PORT;
            }
        }
    }
    return 0;
}

int main(int argc, char* argv[])
{
    int ret;
    int i;

    /* パラメータ解析 */
    if (ret = parse(argc, argv)) {
        if (ret < 0)
            usage();
        return 1;
    }

    /* コンフィグファイルの処理 */
    if (parse_config() < 0)
        return 1;

#ifndef WIN32
    if (action == ACT_START) {
        /* ユーザーの切換 */
        if (getuid() == 0 || geteuid() == 0) {
            struct passwd* pw;

            if (g_conf->username[0] == '\0') {
                fprintf(stderr, "can't run as root, please user switch -u\n");
                return 1;
            }
            if ((pw = getpwnam(g_conf->username)) == 0) {
                fprintf(stderr, "can't find the user %s\n", g_conf->username);
                return 1;
            }
            if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0) {
                fprintf(stderr, "change user failed, %s\n", g_conf->username);
                return 1;
            }
        }
    }
#endif

#ifndef _WIN32
    if (action == ACT_START) {
        if (g_conf->daemonize) {
#ifdef MAC_OSX
            if (daemon(1, 0) != 0)
                fprintf(stderr, "daemon() error\n");
#else
            if (daemon(0, 0) != 0)
                fprintf(stderr, "daemon() error\n");
#endif
        }
    }
#endif

    /* 初期処理 */
    if (startup() < 0)
        return 1;

    if (action == ACT_START)
        http_server();
    else if (action == ACT_STOP)
        stop_server();
    else if (action == ACT_STATUS)
        status_server();
    else if (action == ACT_TRACE)
        trace_mode_server(act_value);

    /* 後処理 */
    cleanup();
    for (i = 0; i < g_conf->u_param.count; i++)
        free_item(&g_conf->u_param.vt[i]);

    if (g_conf->api_table != NULL)
        free(g_conf->api_table);
    if (g_conf->init_api_table != NULL)
        free(g_conf->init_api_table);
    if (g_conf->term_api_table != NULL)
        free(g_conf->term_api_table);

    free(g_conf);

#ifdef WIN32
    _CrtDumpMemoryLeaks();
#endif
    return 0;
}
