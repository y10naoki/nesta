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

/* HTMLボディ */
static char* hello_world_body =
    "<html>\n"
    "<body>\n"
    "<p>Hello, World!</p>\n"
    "</body>\n"
    "</html>";

/*
 * HTTPサーバーの初期化中に呼び出される関数です。
 *
 * シングルスレッドの状態でこの関数が呼び出されます。
 * アプリケーションの初期化をここで行なうことができます。
 *
 * HTTPポートのリスニングが開始される前に呼び出されます。
 *
 * u_param(IN): ユーザーパラメータ構造体のポインタ
 *
 * 戻り値
 *  正常に終了した場合はゼロを返します。
 *  エラーが発生した場合はゼロ以外を返します。
 */
EXPAPI int init_hello(struct user_param_t* u_param)
{
    printf("called Hello World, initial fuction.\n");
    return 0;
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
EXPAPI int term_hello(struct user_param_t* u_param)
{
    printf("called Hello World, terminate fuction.\n");
    return 0;
}

/*
 * HTTPサーバーから呼び出される関数です。
 *
 * パラメータとして受け取ったレスポンスに応答データを送信します。
 *
 * マルチスレッドでこの関数が呼び出されますので、static変数などの
 * 操作は mutexなどを使用して同期する必要があります。
 *
 * req(IN): リクエスト構造体のポインタ
 * resp(IN): レスポンス構造体のポインタ
 * u_param(IN): ユーザーパラメータ構造体のポインタ
 *
 * 戻り値
 *  HTTPステータスを返します。
 */
EXPAPI int helloworld(struct request_t* req,
                      struct response_t* resp,
                      struct user_param_t* u_param)
{
    struct http_header_t* hdr;    /* HTTPヘッダー構造体 */
    int len;                      /* コンテンツサイズ */

    /* HTTPヘッダーの初期化 */
    hdr = alloc_http_header();
    if (hdr == NULL) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/helloworld(): no memory.");
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    init_http_header(hdr);

    /* Content-typeヘッダーを追加 */
    set_content_type(hdr, "text/html", NULL);

    /* Content-lengthヘッダーを追加 */
    len = strlen(hello_world_body);
    set_content_length(hdr, len);

    /* HTTPヘッダーをクライアントに送信します。*/
    if (resp_send_header(resp, hdr) < 0) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/helloworld(): send header error");
        free_http_header(hdr);
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    free_http_header(hdr);

    /* HTTPボディをクライアントに送信します。*/
    if (resp_send_body(resp, hello_world_body, len) < 0) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/helloworld(): send error");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* HTTPステータスを関数値として返します。*/
    return HTTP_OK;
}
