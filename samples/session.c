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
static char* body =
    "<html>\n"
    "<body>\n"
    "<p>%s [%d times]</p>\n"
    "</body>\n"
    "</html>";

/*
 * セッションを利用したサンプルです。
 * セッションを実現するために HTTP Cookie を利用しています。
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
EXPAPI int session(struct request_t* req,
                   struct response_t* resp,
                   struct user_param_t* u_param)
{
    struct http_header_t* hdr;  /* HTTPヘッダー構造体 */
    char* text;
    int times;
    char body_buf[256];
    int len;                    /* コンテンツサイズ */

    /* HTTPヘッダーの初期化 */
    hdr = alloc_http_header();
    if (hdr == NULL) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/session(): no memory.");
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    init_http_header(hdr);

    /* セッションが開始されているか調べます。*/
    if (req->session == NULL) {
        struct session_t* session;  /* セッション構造体 */

        /* 新たなセッションを作成します。*/
        session = ssn_create(req);
        if (session) {
            text = "hello session.";
            times = 1;
            /* セッションに変数を設定します。*/
            ssn_put(session, "msg", text);
            ssn_putdata(session, "times", &times, sizeof(times));
            /* セッションをHTTPヘッダーに設定します。*/
            set_http_session(hdr, session);
        }
    } else {
        void* data;

        /* セッションが確立されている場合は、値を取得します。*/
        text = (char*)ssn_get(req->session, "msg");
        data = ssn_get(req->session, "times");
        if (data) {
            memcpy(&times, data, sizeof(times));
            times++;
        } else
            times = -1;
        /* セッション変数 times を更新します。*/
        ssn_putdata(req->session, "times", &times, sizeof(times));
    }

    /* HTML にセッションの内容を編集します。*/
    snprintf(body_buf, sizeof(body_buf), body, text, times);

    /* Content-typeヘッダーを追加 */
    set_content_type(hdr, "text/html", NULL);

    /* Content-lengthヘッダーを追加 */
    len = strlen(body_buf);
    set_content_length(hdr, len);

    /* HTTPヘッダーをクライアントに送信します。*/
    if (resp_send_header(resp, hdr) < 0) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/session(): send header error");
        free_http_header(hdr);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* HTTP ヘッダー領域を開放します。*/
    free_http_header(hdr);

    /* HTTPボディをクライアントに送信します。*/
    if (resp_send_body(resp, body_buf, len) < 0) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/session(): send error");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* HTTPステータスを関数値として返します。*/
    return HTTP_OK;
}
