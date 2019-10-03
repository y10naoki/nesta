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

/*
 * テンプレートエンジンを使用したサンプルです。
 * テンプレートファイル中のプレースホルダを文字列で置換してクライアントに送信します。
 *
 * テンプレートファイルはユーザーパラメータとしてコンフィグファイルで指定します。
 *
 * 【サンプルで使用するユーザーパラメータ】
 *     template.dir = テンプレートファイルがあるディレクトリ名
 *     template.file = テンプレートファイル名
 *     template.enc = テンプレートファイルの文字エンコーディング名(Shift_JISなど）
 *
 * req(IN): リクエスト構造体のポインタ
 * resp(IN): レスポンス構造体のポインタ
 * u_param(IN): ユーザーパラメータ構造体のポインタ
 *
 * 戻り値
 *  HTTPステータスを返します。
 */
EXPAPI int tpl_helloworld(struct request_t* req,
                          struct response_t* resp,
                          struct user_param_t* u_param)
{
    char* t_dirname;            /* テンプレートファイルのあるディレクトリ名 */
    char* t_filename;           /* テンプレートファイル名 */
    char* t_encoding;           /* テンプレートファイルの文字エンコーディング名 */
    struct template_t* tpl;     /* テンプレート構造体 */
    struct http_header_t* hdr;  /* HTTPヘッダー構造体 */
    char* body;                 /* HTMLボディ */
    int len;                    /* コンテンツサイズ */

    /* ユーザーパラメータからテンプレートの情報を取得します。*/
    t_dirname = get_user_param(u_param, "template.dir");
    t_filename = get_user_param(u_param, "template.file");
    t_encoding = get_user_param(u_param, "template.enc");

    if (t_dirname == NULL || t_filename == NULL) {
        /* ユーザーパラメータが取得できないため、エラーログに出力します。*/
        err_log(req->addr, "samples/tpl_helloworld(): not found user parameter(template.dir, template.file)");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* テンプレートファイルをオープンしてテンプレートを識別する構造体の
       ポインタを取得します。*/
    tpl = tpl_open(t_dirname, t_filename, t_encoding);
    if (tpl == NULL) {
        /* テンプレートがオープンできないため、エラーログに出力します。*/
        err_log(req->addr, "samples/tpl_helloworld(): can't open template(%s)", t_filename);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* テンプレートのプレイスホルダ message に文字列を設定します。*/
    tpl_set_value(tpl, "message", "Hello World, Template!");

    /* テンプレート処理を行い文字列を作成します。*/
    tpl_render(tpl);

    /* テンプレート処理された文字列を取得します。
       ここで取得した文字列はテンプレートをクローズするまで有効です。*/
    body = tpl_get_data(tpl, t_encoding, &len);

    /* HTTPヘッダーの初期化 */
    hdr = alloc_http_header();
    if (hdr == NULL) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/helloworld(): no memory.");
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    init_http_header(hdr);

    /* Content-typeヘッダーを追加 */
    set_content_type(hdr, "text/html", t_encoding);

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
