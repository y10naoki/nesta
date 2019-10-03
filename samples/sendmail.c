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

/* テンプレートから取得したHTML文字列 */
static char* g_html_text;

/* HTMLファイルのサイズ */
static int g_html_len;

/* HTMLファイルの文字エンコーディング名 */
static char* g_html_encoding;

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
 *     smtp.template.dir = テンプレートファイルがあるディレクトリ名
 *     smtp.template.file = テンプレートファイル名
 *     smtp.template.enc = テンプレートファイルの文字エンコーディング名(Shift_JISなど）
 *
 * テンプレートから取得する HTML にはプレースホルダがなく、内容が変更されないので
 * 初期処理で HTML を取得しておきます。
 *
 * u_param(IN): ユーザーパラメータ構造体のポインタ
 *
 * 戻り値
 *  正常に終了した場合はゼロを返します。
 *  エラーが発生した場合はゼロ以外を返します。
 */
EXPAPI int init_sendmail(struct user_param_t* u_param)
{
    char* t_dirname;            /* テンプレートファイルのあるディレクトリ名 */
    char* t_filename;           /* テンプレートファイル名 */

    /* ユーザーパラメータからテンプレートの情報を取得します。*/
    t_dirname = get_user_param(u_param, "smtp.template.dir");
    t_filename = get_user_param(u_param, "smtp.template.file");
    g_html_encoding = get_user_param(u_param, "smtp.template.enc");

    if (t_dirname == NULL || t_filename == NULL) {
        /* ユーザーパラメータが取得できないため、エラーログに出力します。*/
        err_write("samples/init_sendmail(): not found user parameter(smtp.template.dir, smtp.template.file)");
        return -1;
    }

    /* テンプレートファイルをオープンしてテンプレートを識別する構造体の
       ポインタを取得します。*/
    g_tpl = tpl_open(t_dirname, t_filename, g_html_encoding);
    if (g_tpl == NULL) {
        /* テンプレートがオープンできないため、エラーログに出力します。*/
        err_write("samples/init_sendmail(): can't open template(%s)", t_filename);
        return -1;
    }

    /* テンプレートからHTMLの文字列を取得します。*/
    g_html_text = tpl_get_data(g_tpl, g_html_encoding, &g_html_len);
    if (g_html_text == NULL) {
        /* テンプレートから HTMLを取得できなかったため、エラーログに出力します。*/
        err_write("samples/init_sendmail(): can't read template(%s)", t_filename);
        tpl_close(g_tpl);
        return -1;
    }
    return 0;
}

/*
 * リクエストパラメータを取得してメール送信を行います。
 * 添付ファイルの処理も行っています。
 *
 * smtp_add_attach()で設定したデータ領域は smtp_send() 内で参照されます。
 * smtp_send() が終了するまでは解放してはいけません。
 *
 * req(IN): リクエスト構造体のポインタ
 * html_enc(IN): HTMLの文字エンコーディング名
 * err_msg(OUT): エラーメッセージ領域のポインタ
 * err_msg_size(IN): エラーメッセージ領域のサイズ
 *
 * 戻り値
 *  メール送信が成功した場合はゼロを返します。
 *  エラーの場合はエラーメッセージ領域にエラー内容を設定して -1 を返します。
 */
static int send_mail_client(struct request_t* req,
                            const char* html_enc,
                            char* err_msg,
                            int err_msg_size)
{
    char* smtp_server;
    char* a_port;
    ushort smtp_port = 25;
    char* to;
    char* from;
    char* cc;
    char* bcc;
    char* subject;
    char* message;
    struct attach_file_t* attach_file1;
    struct attach_file_t* attach_file2;

    struct smtp_session_t* smtp;  /* SMTPを利用するセッション情報 */
    char mime_subject[512];
    int jis_buf_size;
    char* jis_buf;
    char filename1[MAX_PATH];
    char filename2[MAX_PATH];
    char* enc_data1 = NULL;
    char* enc_data2 = NULL;
    int result;

    /* リクエストパラメータから値を取得します。*/
    smtp_server = get_qparam(req, "smtp");
    if (smtp_server == NULL) {
        strncpy(err_msg, "not found 'smtp' parameter.", err_msg_size-1);
        return -1;
    }
    a_port = get_qparam(req, "port");
    if (a_port)
        smtp_port = atoi(a_port);
    to = get_qparam(req, "to");
    from = get_qparam(req, "from");
    if (to == NULL || from == NULL) {
        strncpy(err_msg, "not found 'to' and 'from' parameter.", err_msg_size-1);
        return -1;
    }
    if (strlen(to) == 0 || strlen(from) == 0) {
        strncpy(err_msg, "empty 'to' and 'from' parameter.", err_msg_size-1);
        return -1;
    }
    cc = get_qparam(req, "cc");
    bcc = get_qparam(req, "bcc");
    subject = get_qparam(req, "subject");
    message = get_qparam(req, "message");

    /* 添付ファイルがあればデータを取得します。*/
    attach_file1 = get_attach_file(req, "attachfile1");
    attach_file2 = get_attach_file(req, "attachfile2");

    /* SMTPセッションをオープンします。*/
    smtp = smtp_open(smtp_server, smtp_port);
    if (smtp == NULL) {
        strncpy(err_msg, "can't connect SMTP server.", err_msg_size-1);
        return -1;
    }

    if (isalnumstr(subject)) {
        smtp_set_subject(smtp, subject);
    } else {
        /* 件名に日本語が含まれているため MIME エンコードします。*/
        mime_encode(mime_subject, sizeof(mime_subject), subject, html_enc);
        smtp_set_subject(smtp, mime_subject);
    }

    /* to, from, cc, bcc に日本語が含まれる場合は MIMEエンコードしてから
       関数のパラメータに設定してください。*/

    smtp_set_to(smtp, to);
    smtp_set_from(smtp, from);
    if (cc && strlen(cc) > 0)
        smtp_set_cc(smtp, cc);
    if (bcc && strlen(bcc) > 0)
        smtp_set_bcc(smtp, bcc);

    /* メール本文を ISO-2022-JP に変換します。*/
    jis_buf_size = strlen(message) * 2;
    jis_buf = (char*)alloca(jis_buf_size);
    convert(html_enc, message, strlen(message), "ISO-2022-JP", jis_buf, jis_buf_size);
    smtp_set_body(smtp, jis_buf);
    smtp_set_header(smtp, "Content-Type", "text/plain; charset=ISO-2022-JP");
    smtp_set_header(smtp, "MIME-Version", "1.0");

    /* 添付ファイルがあれば処理します。*/
    if (attach_file1) {
        if (isalnumstr(attach_file1->filename))
            strncpy(filename1, attach_file1->filename, sizeof(filename1)-1);
        else
            mime_encode(filename1, sizeof(filename1), attach_file1->filename, html_enc);

        enc_data1 = (char*)xalloc(req, attach_file1->size*2);
        base64_encode(enc_data1, attach_file1->data, attach_file1->size);

        smtp_add_attach(smtp,
                        attach_file1->mimetype,
                        "base64",
                        filename1,
                        enc_data1);
    }
    if (attach_file2) {
        if (isalnumstr(attach_file2->filename))
            strncpy(filename2, attach_file2->filename, sizeof(filename2)-1);
        else
            mime_encode(filename2, sizeof(filename2), attach_file2->filename, html_enc);

        enc_data2 = (char*)xalloc(req, attach_file2->size*2);
        base64_encode(enc_data2, attach_file2->data, attach_file2->size);

        smtp_add_attach(smtp,
                        attach_file2->mimetype,
                        "base64",
                        filename2,
                        enc_data2);
    }

    /* メールを送信します。*/
    result = smtp_send(smtp);

    /* SMTPセッションをクローズします。*/
    smtp_close(smtp);

    if (enc_data1)
        xfree(req, enc_data1);
    if (enc_data2)
        xfree(req, enc_data2);

    if (result < 0) {
        strncpy(err_msg, "can't send mail.", err_msg_size-1);
        return -1;
    }
    return 0;
}

/*
 * メール送信を行うサンプルです。
 *
 * メール送信を行う HTMLファイルはテンプレートとして登録しておきます。
 * こうすることでプログラム内で HTMLのタグを出力する必要はなくなります。
 * また、HTMLファイルを変更してもプログラムには影響しなくなります。
 *
 * req(IN): リクエスト構造体のポインタ
 * resp(IN): レスポンス構造体のポインタ
 * u_param(IN): ユーザーパラメータ構造体のポインタ
 *
 * 戻り値
 *  HTTPステータスを返します。
 */
EXPAPI int sendmail(struct request_t* req,
                    struct response_t* resp,
                    struct user_param_t* u_param)
{
    struct http_header_t* hdr;    /* HTTPヘッダー構造体 */

    /* リクエストが POST で「メール送信」ボタンが submit された場合は
       メールを送信します。*/
    if (stricmp(req->method, "POST") == 0) {
        if (get_qparam(req, "sendmail")) {
            char err_msg[1024];

            if (send_mail_client(req, g_html_encoding, err_msg, sizeof(err_msg)) < 0) {
                /* エラーメッセージを送信します。*/
                if (resp_send_body(resp, err_msg, strlen(err_msg)) < 0) {
                    err_log(req->addr, "samples/sendmail(): send error");
                    return HTTP_INTERNAL_SERVER_ERROR;
                }
                return HTTP_OK;
            }
        }
    }

    /* HTTPヘッダーの初期化 */
    hdr = alloc_http_header();
    if (hdr == NULL) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/sendmail(): no memory.");
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    init_http_header(hdr);

    /* Content-typeヘッダーを追加 */
    set_content_type(hdr, "text/html", g_html_encoding);

    /* Content-lengthヘッダーを追加 */
    set_content_length(hdr, g_html_len);

    /* HTTPヘッダーをクライアントに送信します。*/
    if (resp_send_header(resp, hdr) < 0) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/sendmail(): send header error");
        free_http_header(hdr);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* HTTP ヘッダー領域を開放します。*/
    free_http_header(hdr);

    /* HTTPボディをクライアントに送信します。*/
    if (resp_send_body(resp, g_html_text, g_html_len) < 0) {
        /* エラーログに出力します。*/
        err_log(req->addr, "samples/sendmail(): send error");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

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
EXPAPI int term_sendmail(struct user_param_t* u_param)
{
    /* テンプレート構造体をクローズします。*/
    if (g_tpl != NULL)
        tpl_close(g_tpl);
    return 0;
}
