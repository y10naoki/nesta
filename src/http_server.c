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

#include "http_server.h"

#ifdef WIN32
static HANDLE queue_cond;
#else
static pthread_mutex_t queue_mutex;
static pthread_cond_t queue_cond;
#endif

static CS_DEF(worker_thread_info_lock);

static API_FUNCPTR get_api(const char* content_name, struct appzone_t** zone)
{
    int n;
    struct hook_api_t* api;

    n = g_conf->api_count;
    api = g_conf->api_table;
    while (n--) {
        if (strcmp(api->content_name, content_name) == 0) {
            *zone = api->app_zone;
            return api->func_ptr;
        }
        api++;
    }
    return NULL;
}

static void break_signal()
{
    SOCKET c_socket;
    const char dummy = 0x30;

    c_socket = sock_connect_server("127.0.0.1", g_conf->port_no);
    if (c_socket == INVALID_SOCKET) {
        err_write("break_signal: can't open socket: %s", strerror(errno));
        return;
    }
    send_data(c_socket, &dummy, sizeof(dummy));
    SOCKET_CLOSE(c_socket);
}

static int is_command(struct request_t* req)
{
    char ip_addr[256];

    mt_inet_ntoa(req->addr, ip_addr);
    if (strcmp(ip_addr, "127.0.0.1") != 0)
        return 0;
    if (req->content_name[0] != '\0')
        return 0;
    return (get_qparam_count(req) == 1 && get_qparam(req, "cmd"));
}

static int64 get_total_request()
{
    int i;
    int64 n = 0;

    CS_START(&worker_thread_info_lock);
    for (i = 0; i < g_conf->max_worker_threads; i++)
        n += g_worker_thread_tbl[i].count;
    CS_END(&worker_thread_info_lock);

    return n;
}

static char* get_local_datetime(int64 usec, char* buf, size_t bufsize)
{
    time_t sec;
    struct tm t;

    sec = (time_t)(usec / 1000000);
    mt_localtime(&sec, &t);
    snprintf(buf, bufsize, "%d/%02d/%02d %02d:%02d:%02d",
             t.tm_year+1900, t.tm_mon+1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return buf;
}

static void do_server_status(char* buf)
{
    char tbuf[256];
    int64 total_request;
    char stimebuf[256];
    int i;

    total_request = get_total_request();
    get_local_datetime(g_http_start_time, stimebuf, sizeof(stimebuf));

    sprintf(buf, "start %s  total %lld requests.\n\n",
            stimebuf, total_request);
    strcat(buf, "[thread info]\n");
    strcat(buf, "   No status last-access              count\n");
    strcat(buf, "----- ------ ------------------- ----------\n");

    /* スレッド情報の表示なのでロックは行ないません。*/
    for (i = 0; i < g_conf->max_worker_threads; i++) {
        struct worker_thread_info_t* th_info;
        char* status = "";
        char timebuf[256];
        char countbuf[20];

        th_info = &g_worker_thread_tbl[i];

        /* status */
        if (th_info->status == WORKER_THREAD_UNUSE) {
            status = "unuse";
        } else if (th_info->status == WORKER_THREAD_SLEEPING) {
            status = "sleep";
        } else  if (th_info->status == WORKER_THREAD_RUNNING) {
            /* コマンド実行中は sleep とします。*/
            if (th_info->command_flag)
                status = "sleep";
            else
                status = "run";
        }

        /* last access */
        if (th_info->status != WORKER_THREAD_UNUSE && th_info->last_access > 0)
            get_local_datetime(th_info->last_access, timebuf, sizeof(timebuf));
        else
            strcpy(timebuf, "N/A");

        /* access count */
        if (th_info->status == WORKER_THREAD_UNUSE)
            strcpy(countbuf, "         -");
        else
            sprintf(countbuf, "%10lu", th_info->count);

        sprintf(tbuf, "%5d %-6s %-19s %s\n", i+1, status, timebuf, countbuf);
        strcat(buf, tbuf);
    }
}

static int do_command(SOCKET socket, struct request_t* req, int* content_len)
{
    char* cmd;
    char* send_buff;

    cmd = get_qparam(req, "cmd");
    if (strcmp(cmd, "stop") == 0) {
        g_shutdown_flag = 1;
        send_buff = (char*)alloca(BUF_SIZE);
        strcpy(send_buff, "stopped.\n");
    } else if (strcmp(cmd, "status") == 0) {
        int n;
        n = BUF_SIZE + g_conf->max_worker_threads * 100;
        send_buff = (char*)alloca(n);
        do_server_status(send_buff);
    } else if (strcmp(cmd, "trace_on") == 0) {
        g_trace_mode = 1;
        send_buff = (char*)alloca(BUF_SIZE);
        strcpy(send_buff, "trace mode on.\n");
    } else if (strcmp(cmd, "trace_off") == 0) {
        g_trace_mode = 0;
        send_buff = (char*)alloca(BUF_SIZE);
        strcpy(send_buff, "trace mode off.\n");
    } else {
        send_buff = "";
    }

    *content_len = strlen(send_buff);
    if (*content_len > 0) {
        send_data(socket, send_buff, *content_len);
        if (g_shutdown_flag) {
            /* 自分自身にシグナルを送りループを抜けさせます。*/
            break_signal();
        }
    }
    return HTTP_OK;
}

static int request_proc(SOCKET socket,
                        struct request_t* req,
                        struct in_addr addr,
                        int keep_alive_requests,
                        int* content_size,
                        int* is_keep_alive)
{
    int status;
    struct appzone_t* z;
    API_FUNCPTR funcptr;

    *is_keep_alive = 0;
    funcptr = get_api(req->content_name, &z);
    if (funcptr == NULL) {
        /* ドキュメントを送信します。*/
        if (check_file(req->content_name)) {
            /* error */
            err_log(addr, "file check error (%s)", req->content_name);
            status = error_handler(socket, HTTP_NOTFOUND, content_size);
        } else {
            if (*g_conf->document_root == '\0') {
                /* error */
                err_log(addr, "document root is empty!");
                status = error_handler(socket, HTTP_NOTFOUND, content_size);
            } else {
                /* ドキュメントの送信 */
                status = doc_send(socket,
                                  addr,
                                  g_conf->document_root,
                                  req->content_name,
                                  &req->header,
                                  g_conf->keep_alive_timeout,
                                  keep_alive_requests,
                                  content_size);
                if (keep_alive_requests > 0)
                    *is_keep_alive = 1;
            }
        }
    } else {
        struct response_t* resp;

        /* アプリケーション・ゾーンのセッション管理構造体を設定します。*/
        req->zone = z->zone_session;
        if (req->zone) {
            /* リクエストヘッダーからセッションを識別するキーワードを検索して
               セッション構造体のポインタを設定します。*/
            req->session = get_http_session(z->zone_session, &req->header);
        }
        /* レスポンス構造体を作成します。*/
        resp = resp_initialize(socket);
        if (resp == NULL) {
            /* error */
            err_log(addr, "resp_initialize(): no memory!");
            status = error_handler(socket, HTTP_INTERNAL_SERVER_ERROR, content_size);
        } else {
            /* APIを実行します。*/
            status = (*funcptr)(req, resp, &g_conf->u_param);
            *content_size = resp->content_size;
            resp_finalize(resp);
        }
    }
    return status;
}

static int is_timeout_thread(struct worker_thread_info_t* th_info)
{
    int64 now_time;

    now_time = system_time();

    if (th_info->status == WORKER_THREAD_SLEEPING) {
        int elap;

        elap = (int)((now_time - th_info->last_access) / 1000000L);
        if (elap > g_conf->worker_thread_timeout)
            return 1;  /* timeout */
    }
    return 0;
}

/* スレッドをあらかじめプールしておいて空いているスレッドに
   処理を割り当てるボス・ワーカー方式 */
static void http_thread(void* argv)
{
    struct worker_thread_info_t* th_info;
    struct thread_args_t* th_args;
    SOCKET socket;
    struct in_addr addr;
    struct request_t* req;
    int status = HTTP_OK;
    int content_size;
    int keep_alive_mode = 0;
    int keep_alive_requests;
    int timeout_end_flag = 0;

#ifdef _WIN32
    int timeout = INFINITE;
#elif defined(MAC_OSX)
    int timeout = -1;
    struct timeval tv;
    struct timespec ts;
#else
    int timeout = -1;
    struct timespec ts;
#endif

    th_info = (struct worker_thread_info_t*)argv;

#if 0
    if (g_conf->min_worker_threads != g_conf->max_worker_threads) {
        if (th_info->thread_no > g_conf->min_worker_threads) {
            /* タイムアウト（秒）時間を算出します。*/
            timeout = g_conf->worker_thread_check_interval;
#ifdef _WIN32
            /* windowsはミリ秒になります。*/
            timeout *= 1000;
#elif defined(MAC_OSX)
            gettimeofday(&tv, NULL);
            ts.tv_sec = tv.tv_sec + timeout;
            ts.tv_nsec = tv.tv_usec * 1000;
#else
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout;
#endif
        }
    }
#endif

    while (! g_shutdown_flag) {
#ifndef WIN32
        pthread_mutex_lock(&queue_mutex);
#endif
        /* キューにデータが入るまで待機します。*/
        th_info->status = WORKER_THREAD_SLEEPING;
        while (que_empty(g_queue)) {
#ifdef WIN32
            if (WaitForSingleObject(queue_cond, timeout) == WAIT_TIMEOUT) {
                /* タイムアウトで抜けてきた場合はスレッド終了を判定します。*/
                if (is_timeout_thread(th_info)) {
                    timeout_end_flag = 1;
                    break;
                }
            }
#else
            if (timeout < 0) {
                pthread_cond_wait(&queue_cond, &queue_mutex);
            } else {
                if (pthread_cond_timedwait(&queue_cond, &queue_mutex, &ts) == ETIMEDOUT) {
                    if (is_timeout_thread(th_info)) {
                        timeout_end_flag = 1;
                        break;
                    }
                }
            }
#endif
        }
#ifndef WIN32
        pthread_mutex_unlock(&queue_mutex);
#endif

        if (timeout_end_flag)
            break;

        /* キューからデータを取り出します。*/
        th_args = (struct thread_args_t*)que_pop(g_queue);
        if (th_args == NULL)
            continue;

        addr = th_args->sockaddr.sin_addr;
        socket = th_args->client_socket;

        th_info->status = WORKER_THREAD_RUNNING;

        keep_alive_requests = g_conf->keep_alive_requests;

        do {
            keep_alive_mode = 0;
            th_info->command_flag = 0;

            /* リクエストデータの取得 */
            req = get_request(socket, addr, &status);
            if (status == HTTP_OK && req != NULL) {
                if (*req->method == 'H') {
                    /* HEAD のレスポンスを返します。*/
                    status = head_handler(socket, &content_size);
                } else {
                    if (*req->content_name == '\0') {
                        /* コマンドか調べます。*/
                        if (is_command(req)) {
                            th_info->command_flag = 1;
                            status = do_command(socket, req, &content_size);
                        } else {
                            status = error_handler(socket, HTTP_NOTFOUND, &content_size);
                        }
                    } else {
                        char* val;

                        /* ヘッダーに Keep-Alive が指定されているか調べます。*/
                        val = get_http_header(&req->header, "Connection");
                        if (val != NULL && stricmp(val, "Keep-Alive") == 0) {
                            /* Keep-Aliveモード */
                            keep_alive_mode = 1;
                        }

                        /* リクエストを処理します。*/
                        /* 2009/10/05
                           実際に Keep-Alive に対応したかを返してもらいます。*/
                        status = request_proc(socket,
                                              req,
                                              addr,
                                              (keep_alive_mode)? keep_alive_requests : 0,
                                              &content_size,
                                              &keep_alive_mode);
                    }
                }
                if (keep_alive_mode) {
                    if (keep_alive_requests > 0)
                        keep_alive_requests--;
                }
            } else {
                /* get_request() error */
                error_handler(socket, status, &content_size);
            }

            if (req != NULL) {
                if (! th_info->command_flag) {
                    /* アクセスログ出力 */
                    log_write(req, status, content_size);
                    /* 最終アクセス日時を更新します。*/
                    th_info->last_access = system_time();
                    /* 処理したリクエスト数をインクリメントします。*/
                    th_info->count++;
                }
                /* リクエストデータの解放 */
                req_free(req);

                /* Keep-Alive が指定されていてリクエスト回数がリミットに達していない場合は
                   現在のソケットから次のリクエストを読み込みます。*/
                if (keep_alive_mode) {
                    if (keep_alive_requests <= 0)
                        keep_alive_mode = 0;

                    if (keep_alive_mode) {
                        /* 指定秒待ってデータが来ないようであればソケットをクローズします。*/
                        if (! wait_recv_data(socket, g_conf->keep_alive_timeout * 1000))
                            keep_alive_mode = 0;
                    }
                }
            }
        } while (keep_alive_mode);

        /* パラメータ領域の解放 */
        free(th_args);

#ifdef WIN32
        /* sleep(1ms)しないとベンチマークにてパフォーマンスが上がらないため(2008/11/13)。*/
        if (content_size > 100)
            Sleep(1);
#endif

        /* ソケットをクローズします。*/
        SOCKET_CLOSE(socket);
    }

    /* スレッドを終了します。*/
    /* スレッドの状態を未使用にします。*/
    th_info->status = WORKER_THREAD_UNUSE;

    CS_START(&worker_thread_info_lock);
    g_conf->worker_threads--;
    CS_END(&worker_thread_info_lock);

#ifdef _WIN32
    _endthread();
#endif
}

static int get_empty_worker_thread()
{
    int i;

    /* 未使用のワーカースレッドを探します。*/
    for (i = g_conf->min_worker_threads; i < g_conf->max_worker_threads; i++) {
        if (g_worker_thread_tbl[i].status == WORKER_THREAD_UNUSE)
            return i;
    }
    return -1;
}

static void worker_thread_create(int index)
{
#ifdef _WIN32
    uintptr_t thread_id;
    thread_id = _beginthread(http_thread, 0, &g_worker_thread_tbl[index]);
#else
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, (void*)http_thread, &g_worker_thread_tbl[index]);
    /* スレッドの使用していた領域を終了時に自動的に解放します。*/
    pthread_detach(thread_id);
#endif

    /* スレッドを休眠中にします。*/
    g_worker_thread_tbl[index].status = WORKER_THREAD_SLEEPING;
}

static void worker_thread_extend()
{
    int index;

    CS_START(&worker_thread_info_lock);
    index = get_empty_worker_thread();
    if (index >= 0) {
        worker_thread_create(index);
        g_conf->worker_threads++;
    }
    CS_END(&worker_thread_info_lock);
}

static int request_http()
{
    struct sockaddr_in sockaddr;
    int n;
    SOCKET client_socket;
    struct thread_args_t* th_args;

    n = sizeof(struct sockaddr);
    client_socket = accept(g_listen_socket, (struct sockaddr*)&sockaddr, (socklen_t*)&n);
    if (client_socket < 0)
        return 0;
    if (g_shutdown_flag) {
        SOCKET_CLOSE(client_socket);
        return -1;
    }

    /* スレッドへ渡す情報を作成します */
    th_args = (struct thread_args_t*)malloc(sizeof(struct thread_args_t));
    if (th_args == NULL) {
        err_log(sockaddr.sin_addr, "No memory.");
        SOCKET_CLOSE(client_socket);
        return 0;
    }
    th_args->client_socket = client_socket;
    th_args->sockaddr = sockaddr;

    /* リクエストされた情報をキューイング(push)します。*/
    que_push(g_queue, th_args);

    /* キューイングされたことをスレッドへ通知します。*/
#ifdef WIN32
    SetEvent(queue_cond);
#else
    pthread_mutex_lock(&queue_mutex);
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
#endif
    return 0;
}

static int do_http_event()
{
    /* HTTPクライアントからの接続を受付 */
    if (g_conf->worker_threads < g_conf->max_worker_threads) {
        if (! que_empty(g_queue)) {
            /* ワーカースレッドに拡張性があり、
            キューにデータがある場合はワーカースレッドを増やします。*/
            worker_thread_extend();
        }
    }
    return request_http();
}

static int is_shutdown()
{
    return g_shutdown_flag;
}

void http_server()
{
    int i;
    struct sockaddr_in sockaddr;
    char ip_addr[256];
    int sc = 1;
    SOCKET sockets[2];
    SOCK_EVENT_CB cbfuncs[2];

    g_http_start_time = system_time();

    CS_INIT(&worker_thread_info_lock);
    if (is_session_relay()) {
        /* セッション・リレー用のワーカースレッドを起動します。*/
        if (session_relay_server() < 0)
            return;
    }

    /* HTTPリスニングソケットの作成 */
    g_listen_socket = sock_listen(INADDR_ANY,
                                  g_conf->port_no,
                                  g_conf->backlog,
                                  &sockaddr);
    if (g_listen_socket == INVALID_SOCKET)
        return;  /* error */

    /* 自分自身の IPアドレスを取得します。*/
    sock_local_addr(ip_addr);

    /* スターティングメッセージの表示 */
    TRACE("http port: %d on %s listening ... %d threads\n\n",
        g_conf->port_no, ip_addr, g_conf->worker_threads);

    /* キューイング制御の初期化 */
#ifdef WIN32
    queue_cond = CreateEvent(NULL, FALSE, FALSE, NULL);
#else
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&queue_cond, NULL);
#endif

    /* ワーカースレッドを生成します。 */
    for (i = 0; i < g_conf->worker_threads; i++) {
        /* スレッドを作成します。
           生成されたスレッドはリクエストキューが空のため、
           待機状態に入ります。*/
        worker_thread_create(i);
    }

    sockets[0] = g_listen_socket;
    cbfuncs[0] = do_http_event;
    if (g_session_relay_socket != INVALID_SOCKET) {
        sockets[1] = g_session_relay_socket;
        cbfuncs[1] = request_session_relay;
        sc++;
    }
    sock_event(sc, sockets, cbfuncs, is_shutdown);

#ifdef WIN32
    CloseHandle(queue_cond);
#else
    pthread_cond_destroy(&queue_cond);
    pthread_mutex_destroy(&queue_mutex);
#endif

    if (is_session_relay())
        session_relay_close();

    if (g_conf->worker_threads < 0) {
        /* ワーカスレッドがすべて終了していたら削除します。
           スレッドが終了中の場合は参照するため削除しません。*/
        CS_DELETE(&worker_thread_info_lock);
    }
}
