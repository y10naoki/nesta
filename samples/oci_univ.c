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

#ifdef HAVE_ORACLE_OCI

/* HTTPサーバーから関数を呼び出すために必要になります。*/
#include "expapi.h"

/* 各種ライブラリ関数のヘッダーファイルです。*/
#include "nestalib.h"

/* Oracle OCI のアクセスヘッダーです。*/
#include "ociio.h"

/* 共有されるテンプレート構造体 */
static struct template_t* g_tpl;

/* テンプレートファイルの文字エンコーディング名 */
static char* g_t_encoding;

/* データベース・コネクションプール構造体 */
static struct pool_t* g_conn_pool;

/* OCI 環境構造体 */
static struct oci_env_t* g_oci_env;

static char* g_dbname;
static char* g_dbusername;
static char* g_dbpassword;

#define MAX_UNIV_NAME_SIZE  80   /* 大学名サイズ */
#define MAX_UNIV_KANA_SIZE  80   /* 大学名カナサイズ */
#define MAX_PREF_SIZE       10   /* 都道府県サイズ */
#define MAX_URL_SIZE        100  /* URLサイズ */
#define MAX_UNIV_COUNT      800  /* 最大大学数 */

/* 
 * データベースに接続するコールバック関数です。
 * pool_initialize()から呼び出されます。
 *
 * コネクションを関数値として返すことでプーリングされて利用されます。
 */
void* db_conn()
{
    struct oci_conn_t* conn;

    conn = oci_logon(g_oci_env, g_dbusername, g_dbpassword, g_dbname);
    if (conn == NULL) {
        err_write("samples/oci_init_univ(): error oci_logon");
        return NULL;
    }
    return conn;
}

/* 
 * データベースから接続を切断するコールバック関数です。
 * pool_finalize()から呼び出されます。
 */
void db_disconn(void* conn)
{
    oci_logoff((struct oci_conn_t*)conn);
}

/*
 * HTTPサーバーの初期化中に呼び出される関数です。
 *
 * シングルスレッドの状態でこの関数が呼び出されます。
 * アプリケーションの初期化をここで行なうことができます。
 *
 * HTTPポートのリスニングが開始される前に呼び出されます。
 *
 * このサンプルでは、リクエスト毎にデータベースへ接続するのではなく、
 * 初期処理で複数の接続をプーリングしておきます。
 * リクエストがあった場合にプールされている接続を取り出して使用するようにします。
 * このようにすることでデータベースへの接続コストが下げられるのでパフォーマンスにも
 * 良い結果がもたらせます。
 * 
 * テンプレートファイルはユーザーパラメータとしてコンフィグファイルで指定します。
 *
 * 【サンプルで使用するユーザーパラメータ】
 *     dbname = データベース名
 *     username = データベースのユーザー名
 *     password = データベースのパスワード
 *     pool_conn_count = プーリングするコネクション数
 *     template.dir = テンプレートファイルがあるディレクトリ名
 *     template.univ = テンプレートファイル名
 *     template.enc = テンプレートファイルの文字エンコーディング名(Shift_JISなど）
 *
 * u_param(IN): ユーザーパラメータ構造体のポインタ
 *
 * 戻り値
 *  正常に終了した場合はゼロを返します。
 *  エラーが発生した場合はゼロ以外を返します。
 */
EXPAPI int oci_init_univ(struct user_param_t* u_param)
{
    int pool_conn_count;
    char* t_dirname;            /* テンプレートファイルのあるディレクトリ名 */
    char* t_filename;           /* テンプレートファイル名 */

    /* ユーザーパラメータからデータベースの情報を取得します。*/
    g_dbname = get_user_param(u_param, "dbname");
    g_dbusername = get_user_param(u_param, "username");
    g_dbpassword = get_user_param(u_param, "password");
    pool_conn_count = atoi(get_user_param(u_param, "pool_conn_count"));

    /* ユーザーパラメータからテンプレートの情報を取得します。*/
    t_dirname = get_user_param(u_param, "template.dir");
    t_filename = get_user_param(u_param, "template.univ");
    g_t_encoding = get_user_param(u_param, "template.enc");

    if (t_dirname == NULL || t_filename == NULL) {
        /* ユーザーパラメータが取得できないため、エラーログに出力します。*/
        err_write("samples/oci_init_univ(): not found user parameter(template.dir, template.univ)");
        return -1;
    }

    /* テンプレートファイルをオープンしてテンプレートを識別する構造体の
       ポインタを取得します。*/
    g_tpl = tpl_open(t_dirname, t_filename, g_t_encoding);
    if (g_tpl == NULL) {
        /* テンプレートがオープンできないため、エラーログに出力します。*/
        err_write("samples/oci_init_univ(): can't open template(%s)", t_filename);
        return -1;
    }

    /* OCI を初期化します。*/
    g_oci_env = oci_initialize();
    if (g_oci_env == NULL) {
        err_write("samples/oci_init_univ(): error oci_initialize");
        return -1;
    }

    /* データベースのコネクション・プールを作成します。*/
    g_conn_pool = pool_initialize(pool_conn_count, 0, db_conn, db_disconn, POOL_NOTIMEOUT, 0);
    if (g_conn_pool == NULL) {
        err_write("samples/oci_init_univ(): error pool_initialize");
        return -1;
    }
    return 0;
}

/*
 * OCI を使用したサンプルです。
 *
 * リクエスト要求があった場合にプールされているデータベースの接続を取得します。
 *
 * テンプレートファイルを初期化処理でオープンしておき、リクエスト時には
 * オープンされているテンプレートを tpl_reopen() して使用します。
 * テンプレートファイルが更新されていた場合は tpl_reopen() の中でテンプレートが
 * 最新の状態に更新されます。
 *
 * req(IN): リクエスト構造体のポインタ
 * resp(IN): レスポンス構造体のポインタ
 * u_param(IN): ユーザーパラメータ構造体のポインタ
 *
 * 戻り値
 *  HTTPステータスを返します。
 */
EXPAPI int oci_univ(struct request_t* req,
                    struct response_t* resp,
                    struct user_param_t* u_param)
{
    struct template_t* tpl;     /* テンプレート構造体 */
    struct http_header_t* hdr;    /* HTTPヘッダー構造体 */
    char* body;                 /* HTMLボディ */
    int len;                    /* コンテンツサイズ */
    struct oci_conn_t* conn;
    struct oci_stmt_t* stmt;
    /* 該当する都道府県の「学校名・カナ・都道府県名・URL」を取得する SQL です。*/
    char* seluniv = "SELECT a.univ_name, a.univ_kana, b.pref_name, a.url"
                    "  FROM t_univ a, t_pref b"
                    " WHERE b.pref_cd = a.pref_cd"
                    "   AND a.pref_cd = :PREFCD"
                    " ORDER BY a.univ_cd";
    char* req_prefcd;           /* 都道府県コード */
    char* name_array;           /* 大学名 */
    char* kana_array;           /* 大学名カナ */
    char* pref_array;           /* 都道府県名 */
    char* url_array;            /* URL */
    int status;
    int rows = 0;

    /* データベースのコネクションをプールから取得します。*/
    conn = (struct oci_conn_t*)pool_get(g_conn_pool, POOL_NOWAIT);
    if (conn == NULL) {
        /* コネクションがすべて使用中なのでエラーログに出力します。*/
        err_log(req->addr, "samples/oci_univ(): pool_get() error, connection is busy.");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* SQL文を発行するステートメントを作成します。*/
    stmt = oci_stmt(conn);

    /* ステートメントにSQL文を割り当てます。*/
    oci_prepare(stmt, seluniv);

    /* リクエストから prefcd パラメータ（都道府県コード）の値を取得します。*/
    req_prefcd = get_qparam(req, "prefcd");
    if (req_prefcd == NULL)
        req_prefcd = "01";

    /* SQL条件式の都道府県コードを設定します。*/
    oci_bindname_str(stmt, ":PREFCD", req_prefcd, NULL);

    /* SQL文を実行します（この時点では結果セットは作成されません）。*/
    oci_execute2(stmt, 0, 0);

    /* select結果を格納する配列変数を確保します。
       xalloc()で確保したメモリはレスポンス時に自動的に解放されます。*/
    name_array = (char*)xalloc(req, MAX_UNIV_NAME_SIZE * MAX_UNIV_COUNT);
    kana_array = (char*)xalloc(req, MAX_UNIV_KANA_SIZE * MAX_UNIV_COUNT);
    pref_array = (char*)xalloc(req, MAX_PREF_SIZE * MAX_UNIV_COUNT);
    url_array = (char*)xalloc(req, MAX_URL_SIZE * MAX_UNIV_COUNT);

    /* select結果を格納する変数をバインドします。*/
    oci_outbind_str(stmt, 1, name_array, MAX_UNIV_NAME_SIZE);
    oci_outbind_str(stmt, 2, kana_array, MAX_UNIV_KANA_SIZE);
    oci_outbind_str(stmt, 3, pref_array, MAX_PREF_SIZE);
    oci_outbind_str(stmt, 4, url_array, MAX_URL_SIZE);

    /* 結果セットから全件を配列にフェッチします。*/
    status = oci_fetch2(stmt, MAX_UNIV_COUNT, OCI_FETCH_NEXT, 0);
    if (status == OCI_NO_DATA) {
        /* 最後までフェッチした場合に何件フェッチしたか調べます。*/
        rows = oci_fetch_count(stmt);
    }

    /* ステートメントを解放します。*/
    oci_stmt_free(stmt);

    /* データベースのコネクションをプールへ返却します。*/
    pool_release(g_conn_pool, conn);

    /* オープン済みのテンプレートファイルを再オープンして
       テンプレートを識別する構造体のポインタを取得します。*/
    tpl = tpl_reopen(g_tpl);
    if (tpl == NULL) {
        /* テンプレートが再オープンできないため、エラーログに出力します。*/
        err_log(req->addr, "samples/oci_univ(): can't open tpl_reopen()");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* HTMLの文字エンコーディングを設定します。*/
    tpl_set_value(tpl, "encoding", g_t_encoding);

    /* テンプレートのプレイスホルダに文字列配列を設定します。*/
    tpl_set_array(tpl, "name_list", name_array, MAX_UNIV_NAME_SIZE, rows);
    tpl_set_array(tpl, "kana_list", kana_array, MAX_UNIV_KANA_SIZE, rows);
    tpl_set_array(tpl, "pref_list", pref_array, MAX_PREF_SIZE, rows);
    tpl_set_array(tpl, "url_list", url_array, MAX_URL_SIZE, rows);

    /* テンプレート処理を行い文字列を作成します。*/
    tpl_render(tpl);

    /* テンプレート処理された文字列を取得します。
       ここで取得した文字列はテンプレートをクローズするまで有効です。*/
    body = tpl_get_data(tpl, g_t_encoding, &len);

    /* HTTPヘッダーの初期化 */
    hdr = alloc_http_header();
    if (hdr == NULL) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/oci_univ(): no memory.");
        tpl_close(tpl);
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    init_http_header(hdr);

    /* Content-typeヘッダーを追加 */
    set_content_type(hdr, "text/html", g_t_encoding);

    /* Content-lengthヘッダーを追加 */
    set_content_length(hdr, len);

    /* HTTPヘッダーをクライアントに送信します。*/
    if (resp_send_header(resp, hdr) < 0) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/oci_univ(): send header error");
        free_http_header(hdr);
        tpl_close(tpl);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* HTTP ヘッダー領域を開放します。*/
    free_http_header(hdr);

    /* HTTPボディをクライアントに送信します。*/
    if (resp_send_body(resp, body, len) < 0) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/oci_univ(): send error");
        tpl_close(tpl);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* テンプレートをクローズします。*/
    tpl_close(tpl);

    /* HTTPステータスを関数値として返します。*/
    return HTTP_OK;
}

/*
 * HTTPサーバーの終了処理中に呼び出される関数です。
 *
 * シングルスレッドの状態でこの関数が呼び出されます。
 * アプリケーションの終了処理をここで行なうことができます。
 * アプリケーションの終了方法によってはこの関数が呼ばれる保障はありません。
 *
 * HTTPポートのリスニングが終了された時点で呼び出されます。
 *
 * u_param(IN): ユーザーパラメータ構造体のポインタ
 *
 * 戻り値
 *  正常に終了した場合はゼロを返します。
 *  エラーが発生した場合はゼロ以外を返します。
 */
EXPAPI int oci_term_univ(struct user_param_t* u_param)
{
    /* プーリングの使用を終了します。*/
    pool_finalize(g_conn_pool);

    /* OCI の使用を終了します。*/
    oci_finalize(g_oci_env);

    /* テンプレート構造体をクローズします。*/
    if (g_tpl != NULL)
        tpl_close(g_tpl);
    return 0;
}
#endif  /* HAVE_ORACLE_OCI */
