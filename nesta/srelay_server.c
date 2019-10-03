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

#define CMD_HELLO_SERVER   1  /* HS(Hello Server) */
#define CMD_REQ_SESSION    2  /* RS(Request Session) */
#define CMD_CHG_OWNER      3  /* CO(Change Owner) */
#define CMD_QRY_TIMESTAMP  4  /* QT(Query Timestamp) */
#define CMD_DEL_SESSION    5  /* DS(Delete Session) */
#define CMD_COPY_SESSION   6  /* CS(Copy Session) */

#ifdef WIN32
static HANDLE srelay_queue_cond;
#else
static pthread_mutex_t srelay_queue_mutex;
static pthread_cond_t srelay_queue_cond;
#endif

static struct appzone_t* get_zone(const char* zonename)
{
    int n;
    int i;

    n = vect_count(g_conf->zone_table);
    for (i = 0; i < n; i++) {
        struct appzone_t* z;

        z = (struct appzone_t*)vect_get(g_conf->zone_table, i);
        if (z != NULL) {
            if (strcmp(z->zone_name, zonename) == 0)
                return z;
        }
    }
    return NULL;
}

static int get_length_string(SOCKET socket, int bufsize, char* buff)
{
    int status;
    short len;

    /* lengthを取得します。*/
    len = recv_short(socket, &status);
    if (status != 0)
        return -1;
    if (len < 1)
        return -1;
    if (len > bufsize-1)
        return -1;

    /* 名称を取得します。*/
    recv_char(socket, buff, len, &status);
    if (status != 0)
        return -1;
    buff[len] = '\0';
    return 0;
}

static int get_session_copy_server(SOCKET socket,
                                   struct session_copy_t* s_cp)
{
    int status;
    int i;

    /* 領域をクリアします。*/
    memset(s_cp, '\0', sizeof(struct session_copy_t));

    /* 個数を取得します。*/
    s_cp->count = recv_short(socket, &status);
    if (status != 0)
        return -1;

    for (i = 0; i < s_cp->count; i++) {
        char server_name[MAX_HOSTNAME];

        /* サーバー名を取得します。*/
        if (get_length_string(socket, MAX_HOSTNAME, server_name) < 0)
            return -1;
        s_cp->addr[i] = inet_addr(server_name);

        /* ポート番号（バイナリ）を取得します。*/
        s_cp->port[i] = (ushort)recv_short(socket, &status);
        if (status != 0)
            return -1;
        if (s_cp->port[i] == 0)
            return -1;
    }
    return 0;
}

static struct zone_session_t* get_zone_session(SOCKET socket, char* skey)
{
    char zonename[MAX_ZONENAME];
    struct appzone_t* z;

    /* ゾーン名を取得します。*/
    if (get_length_string(socket, MAX_ZONENAME, zonename) < 0)
        return NULL;

    /* セッションキー名を取得します。*/
    if (get_length_string(socket, SESSION_KEY_SIZE+1, skey) < 0)
        return NULL;

    /* アプリケーション・ゾーン構造体を取得します。*/
    z = get_zone(zonename);
    if (z == NULL) {
        err_write("session relay: not found zone(%s).", zonename);
        return NULL;
    }
    /* セッション管理構造体 */
    return z->zone_session;
}

static struct session_t* get_session(struct zone_session_t* zs,
                                     const char* skey)
{
    return (struct session_t*)hash_get(zs->s_tbl, skey);
}

static struct session_t* get_session_create(struct zone_session_t* zs,
                                            const char* skey,
                                            const char* sid)
{
    struct session_t* s;

    s = get_session(zs, skey);
    if (s == NULL)
        s = ssn_copy_create(zs, skey, sid);
    return s;
}

static short count_session_data(struct session_t* s,
                                const char** list)
{
    int n = 0;

    while (*list) {
        struct session_data_t* sdata;

        /* セッションデータ構造体 */
        sdata = (struct session_data_t*)hash_get(s->sdata, *list);
        if (sdata != NULL) {
            /* サイズが1以上の場合のみカウントします。*/
            if (sdata->size > 0)
                n++;
        }
        list++;
    }
    return (short)n;
}

/* HS: HELLOサーバー */
static int hello_server(SOCKET socket)
{
    /* 応答データ */

    /* "OK"を送信します。*/
    if (send_data(socket, "OK", 2) < 0) {
        err_write("session relay: hello server error(%s).", strerror(errno));
        return -1;
    }
    return 0;
}

/* RS: セッション転送 */
static int request_session(SOCKET socket)
{
    struct zone_session_t* zs;
    struct session_t* s;
    char skey[SESSION_KEY_SIZE+1];
    char hostname[MAX_HOSTNAME];
    char** key_list = NULL;
    int status;
    short sc = 0;
    short i;
    int result = -1;

    zs = get_zone_session(socket, skey);
    if (zs == NULL)
        return -1;

    /* 該当のセッション構造体を取得します。*/
    s = get_session(zs, skey);
    if (s == NULL)
        return -1;

    if (! s->owner_flag) {
        if (s->owner_addr != 0) {
            char my_hostname[MAX_HOSTNAME];

            /* オーナー権がないので現在のオーナーに問い合わせます。*/
            mt_inet_addr(s->owner_addr, hostname);
            mt_inet_addr(zs->rsvr->host_addr, my_hostname);
            srelay_get_session(s,
                               skey,
                               zs->zone_name,
                               hostname,
                               s->owner_port,
                               my_hostname,
                               zs->rsvr->host_port,
                               &zs->rsvr->s_cp,
                               &s->owner_s_cp);
        }
    }

    /* 新オーナーのホスト名を取得します。*/
    if (get_length_string(socket, MAX_HOSTNAME, hostname) < 0)
        return -1;
    s->owner_addr = inet_addr(hostname);

    /* 新オーナーのポート番号（バイナリ）を取得します。*/
    s->owner_port = (ushort)recv_short(socket, &status);
    if (status != 0)
        return -1;
    if (s->owner_port == 0)
        return -1;

    /* 新オーナーのセッションのコピーを保持しているサーバー情報を取得します。*/
    if (get_session_copy_server(socket, &s->owner_s_cp) < 0)
        return -1;

    /* 応答データ */

    if (s->sdata != NULL) {
        /* セッションのキーを列挙します。*/
        key_list = hash_keylist(s->sdata);
        if (key_list != NULL) {
            /* セッションのキー数を求めます。
               値のサイズがゼロのものは対象外とします。*/
            sc = count_session_data(s, (const char**)key_list);
        }
    }

    /* タイムスタンプを送信します。*/
    if (send_int64(socket, s->last_update) < 0)
        goto final;

    /* セッションのキー数を送信します。*/
    if (send_short(socket, sc) < 0)
        goto final;

    /* キーと値のペアを送信します。*/
    for (i = 0; i < sc; i++) {
        short len;
        struct session_data_t* sdata;

        /* キーのlengthを送信します。*/
        len = (short)strlen(key_list[i]);
        if (send_short(socket, len) < 0)
            goto final;

        /* キーを送信します。*/
        if (send_data(socket, key_list[i], len) < 0)
            goto final;

        /* セッションデータ構造体 */
        sdata = (struct session_data_t*)hash_get(s->sdata, key_list[i]);
        if (sdata == NULL)
            goto final;
        if (sdata->size > 0) {
            /* 値のlengthを送信します。*/
            if (send_short(socket, (short)sdata->size) < 0)
                goto final;
            /* 値を送信します。*/
            if (send_data(socket, sdata->data, sdata->size) < 0)
                goto final;
        }
    }
    /* オーナーフラグをクリアします。*/
    s->owner_flag = 0;
    result = 0;

final:
    if (key_list != NULL)
        hash_list_free((void**)key_list);
    if (result < 0)
        err_write("session relay: session send error(%s).", strerror(errno));
    return result;
}

/* CO: オーナー移行 */
static int change_owner(SOCKET socket)
{
    struct zone_session_t* zs;
    struct session_t* s;
    char skey[SESSION_KEY_SIZE+1];
    char hostname[MAX_HOSTNAME];
    int status;

    zs = get_zone_session(socket, skey);
    if (zs == NULL)
        return -1;

    /* 該当のセッション構造体を取得します。*/
    s = get_session(zs, skey);
    if (s == NULL)
        return -1;

    /* 新オーナーのホスト名を取得します。*/
    if (get_length_string(socket, MAX_HOSTNAME, hostname) < 0)
        return -1;
    s->owner_addr = inet_addr(hostname);

    /* 新オーナーのポート番号（バイナリ）を取得します。*/
    s->owner_port = (ushort)recv_short(socket, &status);
    if (status != 0)
        return -1;
    if (s->owner_port == 0)
        return -1;

    /* 新オーナーのセッションのコピーを保持しているサーバー情報を取得します。*/
    if (get_session_copy_server(socket, &s->owner_s_cp) < 0)
        return -1;

    /* オーナーフラグをクリアします。*/
    s->owner_flag = 0;

    /* 応答データなし */
    return 0;
}

/* QT: セッション・タイムスタンプ問い合わせ */
static int query_timestamp(SOCKET socket)
{
    struct zone_session_t* zs;
    struct session_t* s;
    char skey[SESSION_KEY_SIZE+1];
    int64 ts;

    zs = get_zone_session(socket, skey);
    if (zs == NULL)
        return -1;

    /* 該当のセッション構造体を取得します。*/
    s = get_session(zs, skey);
    if (s == NULL)
        return -1;

    /* タイムスタンプ */
    ts = s->last_update;

    if (! s->owner_flag) {
        if (s->owner_addr != 0) {
            /* オーナー権がないのでオーナーに問い合わせます。*/
            char hostname[MAX_HOSTNAME];

            mt_inet_addr(s->owner_addr, hostname);
            ts = srelay_timestamp(skey, zs->zone_name, hostname, s->owner_port, &s->owner_s_cp);
        }
    }

    /* 応答データ */
    /* タイムスタンプを送信します。*/
    if (send_data(socket, &ts, sizeof(ts)) < 0) {
        err_write("session relay: timestamp send error.");
        return -1;
    }
    return 0;
}

/* DS: セッション削除 */
static int delete_session(SOCKET socket)
{
    char skey[SESSION_KEY_SIZE+1];
    struct zone_session_t* zs;
    struct session_t* s;

    /* ゾーンセッション構造体 */
    zs = get_zone_session(socket, skey);
    if (zs == NULL)
        return -1;

    /* セッションデータの解放 */
    s = get_session(zs, skey);
    if (s)
        ssn_free_nolock(s);

    /* 応答データなし */
    return hash_delete(zs->s_tbl, skey);
}

/* CS: コピーセッション */
static int copy_session(SOCKET socket)
{
    struct zone_session_t* zs;
    struct session_t* s;
    char skey[SESSION_KEY_SIZE+1];
    char sid[MAX_SESSIONID];
    char hostname[MAX_HOSTNAME];
    int status;
    int64 ts;
    short n;
    int result = -1;

    zs = get_zone_session(socket, skey);
    if (zs == NULL)
        return -1;

    /* セッション識別子を取得します。*/
    if (get_length_string(socket, MAX_SESSIONID, sid) < 0)
        return -1;

    /* 該当のセッション構造体を取得します。
       存在しない場合は作成します。*/
    s = get_session_create(zs, skey, sid);
    if (s == NULL)
        return -1;
    /* セッションデータをクリアします。*/
    ssn_delete_all(s);

    /* オーナーのホスト名を取得します。*/
    if (get_length_string(socket, MAX_HOSTNAME, hostname) < 0)
        return -1;
    s->owner_addr = inet_addr(hostname);

    /* オーナーのポート番号（バイナリ）を取得します。*/
    s->owner_port = (ushort)recv_short(socket, &status);
    if (status != 0)
        return -1;
    if (s->owner_port == 0)
        return -1;

    /* オーナーのセッションのコピーを保持しているサーバー情報を取得します。*/
    if (get_session_copy_server(socket, &s->owner_s_cp) < 0)
        return -1;

    /* タイムスタンプを取得します。*/
    ts = recv_int64(socket, &status);
    if (status != 0)
        goto final;
    s->last_update = ts;

    /* 個数を取得します。*/
    n = recv_short(socket, &status);
    if (status != 0)
        goto final;

    /* 個数分のセッション名と値を取得します。*/
    while (n--) {
        short len;
        char key[MAX_HASH_KEYSIZE+1];
        int size;
        void* data;

        /* 名称のlengthを取得します。*/
        len = recv_short(socket, &status);
        if (status != 0)
            goto final;
        if (len < 1 || len > MAX_HASH_KEYSIZE)
            goto final;

        /* 名称を取得します。*/
        recv_char(socket, key, len, &status);
        if (status != 0)
            goto final;
        key[len] = '\0';

        /* 値のlengthを取得します。*/
        size = recv_short(socket, &status);
        if (status != 0)
            goto final;
        if (size < 1)
            goto final;

        /* 値を取得します。*/
        data = malloc(size);
        if (data == NULL) {
            err_write("session relay: session copy error no memory.");
            return -1;
        }
        recv_char(socket, data, size, &status);
        if (status != 0) {
            free(data);
            return -1;
        }

        /* セッション情報を置換します。*/
        ssn_put_nolock(s, key, data, size);
        free(data);
    }

    /* 応答データなし */

    /* コピーされたセッションなのでオーナーにはなりません。*/
    s->owner_flag = 0;
    result = 0;

final:
    if (result < 0)
        err_write("session relay: session copy error(%s).", strerror(errno));
    return result;
}

static int get_command(SOCKET socket)
{
    int len;
    char buf[2];
    int status;

    len = recv_char(socket, buf, sizeof(buf), &status);
    if (status != 0)
        return 0;
    if (memcmp(buf, "HS", sizeof(buf)) == 0)
        return CMD_HELLO_SERVER;
    if (memcmp(buf, "RS", sizeof(buf)) == 0)
        return CMD_REQ_SESSION;
    if (memcmp(buf, "CO", sizeof(buf)) == 0)
        return CMD_CHG_OWNER;
    if (memcmp(buf, "QT", sizeof(buf)) == 0)
        return CMD_QRY_TIMESTAMP;
    if (memcmp(buf, "DS", sizeof(buf)) == 0)
        return CMD_DEL_SESSION;
    if (memcmp(buf, "CS", sizeof(buf)) == 0)
        return CMD_COPY_SESSION;
    err_write("session relay invalid command(%c%c).", buf[0], buf[1]);
    return 0;  /* command notfound */
}

static void session_relay_thread(void* argv)
{
    /* argv is unuse */
    struct thread_args_t* th_args;
    SOCKET socket;
    struct in_addr addr;

    while (! g_shutdown_flag) {
        int cmd;

#ifndef WIN32
        pthread_mutex_lock(&srelay_queue_mutex);
#endif
        /* キューにデータが入るまで待機します。*/
        while (que_empty(g_session_relay_queue)) {
#ifdef WIN32
            WaitForSingleObject(srelay_queue_cond, INFINITE);
#else
            pthread_cond_wait(&srelay_queue_cond, &srelay_queue_mutex);
#endif
        }
#ifndef WIN32
        pthread_mutex_unlock(&srelay_queue_mutex);
#endif
        /* キューからデータを取り出します。*/
        th_args = (struct thread_args_t*)que_pop(g_session_relay_queue);
        if (th_args == NULL)
            continue;

        addr = th_args->sockaddr.sin_addr;
        socket = th_args->client_socket;

        /* コマンドを受信します。*/
        cmd = get_command(socket);
        switch (cmd) {
            case CMD_HELLO_SERVER:
                hello_server(socket);
                break;
            case CMD_REQ_SESSION:
                request_session(socket);
                break;
            case CMD_CHG_OWNER:
                change_owner(socket);
                break;
            case CMD_QRY_TIMESTAMP:
                query_timestamp(socket);
                break;
            case CMD_DEL_SESSION:
                delete_session(socket);
                break;
            case CMD_COPY_SESSION:
                copy_session(socket);
                break;
        }
        /* パラメータ領域の解放 */
        free(th_args);

        /* ソケットをクローズします。*/
        SOCKET_CLOSE(socket);
    }

#ifdef _WIN32
    _endthread();
#endif
}

int request_session_relay()
{
    struct sockaddr_in sockaddr;
    int n;
    SOCKET client_socket;
    struct thread_args_t* th_args;

    n = sizeof(struct sockaddr);
    client_socket = accept(g_session_relay_socket, (struct sockaddr*)&sockaddr, (socklen_t*)&n);
    if (client_socket < 0)
        return 0;
    if (g_shutdown_flag) {
        SOCKET_CLOSE(client_socket);
        return -1;
    }

    /* スレッドへ渡す情報を作成します */
    th_args = (struct thread_args_t*)malloc(sizeof(struct thread_args_t));
    if (th_args == NULL) {
        err_log(sockaddr.sin_addr, "no memory.");
        SOCKET_CLOSE(client_socket);
        return 0;
    }
    th_args->client_socket = client_socket;
    th_args->sockaddr = sockaddr;

    /* リクエストされた情報をキューイング(push)します。*/
    que_push(g_session_relay_queue, th_args);

    /* キューイングされたことをスレッドへ通知します。*/
#ifdef WIN32
    SetEvent(srelay_queue_cond);
#else
    pthread_mutex_lock(&srelay_queue_mutex);
    pthread_cond_signal(&srelay_queue_cond);
    pthread_mutex_unlock(&srelay_queue_mutex);
#endif
    return 0;
}

int session_relay_server()
{
    int i;
    struct sockaddr_in sockaddr;
#ifdef _WIN32
    uintptr_t thread_id;
#else
    pthread_t thread_id;
#endif

    /* セッション・リレー リスニングソケットの作成 */
    g_session_relay_socket = sock_listen(inet_addr(g_conf->session_relay_host),
                                         g_conf->session_relay_port,
                                         g_conf->session_relay_backlog,
                                         &sockaddr);
    if (g_session_relay_socket == INVALID_SOCKET)
        return -1;  /* error */

    /* スターティングメッセージの表示 */
    TRACE("session relay port: %d on %s listening ... %d threads\n",
        g_conf->session_relay_port, g_conf->session_relay_host, g_conf->session_relay_worker_threads);

    /* セッションコピーサーバーの情報 */
    for (i = 0; i < g_conf->session_relay_copy_count; i++) {
        TRACE("  copy session to %s:%d\n",
            g_conf->session_relay_copy_host[i], g_conf->session_relay_copy_port[i]);
    }

    /* キューイング制御の初期化 */
#ifdef WIN32
    srelay_queue_cond = CreateEvent(NULL, FALSE, FALSE, NULL);
#else
    pthread_mutex_init(&srelay_queue_mutex, NULL);
    pthread_cond_init(&srelay_queue_cond, NULL);
#endif

    /* ワーカースレッドを生成します。 */
    for (i = 0; i < g_conf->session_relay_worker_threads; i++) {
        /* スレッドを作成します。
           生成されたスレッドはリクエストキューが空のため、
           待機状態に入ります。*/
#ifdef _WIN32
        thread_id = _beginthread(session_relay_thread, 0, NULL);
#else
        pthread_create(&thread_id, NULL, (void*)session_relay_thread, NULL);
        /* スレッドの使用していた領域を終了時に自動的に解放します。*/
        pthread_detach(thread_id);
#endif
    }
    return 0;
}

void session_relay_close()
{
#ifdef WIN32
    CloseHandle(srelay_queue_cond);
#else
    pthread_cond_destroy(&srelay_queue_cond);
    pthread_mutex_destroy(&srelay_queue_mutex);
#endif
}
