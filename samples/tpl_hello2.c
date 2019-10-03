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

/* HTTPサーバーから関数を呼び出すために必要になります。*/
#include "expapi.h"

/* 各種ライブラリ関数のヘッダーファイルです。*/
#include "nestalib.h"

/* テンプレート構造体 */
static struct template_t* g_tpl;
/* テンプレートファイルの文字エンコーディング名 */
static char* g_t_encoding;

/*
 * HTTPサーバーの初期化中に呼び出される関数です。
 *
 * シングルスレッドの状態でこの関数が呼び出されます。
 * アプリケーションの初期化をここで行なうことができます。
 * HTTPポートのリスニングが開始される前に呼び出されます。
 *
 * テンプレートファイルはユーザーパラメータとしてコンフィグファイルで指定します。
 *
 * 【サンプルで使用するユーザーパラメータ】
 *     template.dir = テンプレートファイルがあるディレクトリ名
 *     template.file = テンプレートファイル名
 *     template.enc = テンプレートファイルの文字エンコーディング名(Shift_JISなど）
 *
 * u_param(IN): ユーザーパラメータ構造体のポインタ
 *
 * 戻り値
 *  正常に終了した場合はゼロを返します。
 *  エラーが発生した場合はゼロ以外を返します。
 */
EXPAPI int tpl_init_hello(struct user_param_t* u_param)
{
    char* t_dirname;            /* テンプレートファイルのあるディレクトリ名 */
    char* t_filename;           /* テンプレートファイル名 */

    /* ユーザーパラメータからテンプレートの情報を取得します。*/
    t_dirname = get_user_param(u_param, "template.dir");
    t_filename = get_user_param(u_param, "template.file");
    g_t_encoding = get_user_param(u_param, "template.enc");

    if (t_dirname == NULL || t_filename == NULL) {
        /* ユーザーパラメータが取得できないため、エラーログに出力します。*/
        err_write("samples/tpl_init_hello(): not found user parameter(template.dir, template.file)");
        return -1;
    }

    /* テンプレートファイルをオープンしてテンプレートを識別する構造体の
       ポインタを取得します。*/
    g_tpl = tpl_open(t_dirname, t_filename, g_t_encoding);
    if (g_tpl == NULL) {
        /* テンプレートがオープンできないため、エラーログに出力します。*/
        err_write("samples/tpl_init_hello(): can't open template(%s)", t_filename);
        return -1;
    }
    return 0;
}

/*
 * テンプレートエンジンを使用したサンプルです。
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
EXPAPI int tpl_helloworld2(struct request_t* req,
                           struct response_t* resp,
                           struct user_param_t* u_param)
{
    struct template_t* tpl;     /* テンプレート構造体 */
    struct http_header_t* hdr;  /* HTTPヘッダー構造体 */
    char* body;                 /* HTMLボディ */
    int len;                    /* コンテンツサイズ */

    /* オープン済みのテンプレートファイルを再オープンして
       テンプレートを識別する構造体のポインタを取得します。*/
    tpl = tpl_reopen(g_tpl);
    if (tpl == NULL) {
        /* テンプレートが再オープンできないため、エラーログに出力します。*/
        err_log(req->addr, "samples/tpl_helloworld2(): can't open tpl_reopen()");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* テンプレートのプレイスホルダ message に文字列を設定します。*/
    tpl_set_value(tpl, "message", "Hello World, Template!");

    /* テンプレート処理を行い文字列を作成します。*/
    tpl_render(tpl);

    /* テンプレート処理された文字列を取得します。
       ここで取得した文字列はテンプレートをクローズするまで有効です。*/
    body = tpl_get_data(tpl, g_t_encoding, &len);

    /* HTTPヘッダーの初期化 */
    hdr = alloc_http_header();
    if (hdr == NULL) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/helloworld(): no memory.");
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
        err_log(req->addr, "samples/helloworld(): send header error");
        tpl_close(tpl);
        free_http_header(hdr);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* HTTP ヘッダー領域を開放します。*/
    free_http_header(hdr);

    /* HTTPボディをクライアントに送信します。*/
    if (resp_send_body(resp, body, len) < 0) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/tpl_helloworld(): send error");
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
EXPAPI int tpl_term_hello(struct user_param_t* u_param)
{
    /* テンプレート構造体をクローズします。*/
    if (g_tpl != NULL)
        tpl_close(g_tpl);
    return 0;
}
