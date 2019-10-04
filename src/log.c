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
#include <time.h>

/*
 * OUTPUT FORMAT:
 *   ipaddr [DATE TIME] "method uri protocol" status content-length times(us)
 *
 * 'X-Forwarded-For'ヘッダーがある場合は最初のアドレスを記録します。
 * X-Forwarded-For: 172.16.1.1, 192.168.1.1
 *
 * LOG FILE NAME:
 * non daily:  basename.extname
 * daily mode: basename_YYYY-MM-DD.extname
 */
static int log_daily_flag = 0;  // daily is 1.
static char log_cur_date[20];   // yyyy-mm-dd
static char log_basename[MAX_PATH+1];
static char log_extname[MAX_PATH+1];  // include dot '.'

static int log_fd = -1;

static CS_DEF(log_critical_section);

static void log_open()
{
    char file_name[MAX_PATH+1];

    if (log_daily_flag) {
        snprintf(file_name, sizeof(file_name), "%s_%s%s", log_basename, log_cur_date, log_extname);
    } else {
        strcpy(file_name, log_basename);
    }
    log_fd = FILE_OPEN(file_name, O_WRONLY|O_APPEND|O_CREAT, CREATE_MODE);
    if (log_fd < 0) {
        fprintf(stderr, "log file can't open [%d]: ", errno);
        perror("");
    }
}

static void log_close()
{
    if (log_fd >= 0) {
        FILE_CLOSE(log_fd);
        log_fd = -1;
    }
}

static char* set_cur_date(char* dt, int dt_size)
{
    time_t timebuf;
    struct tm now; 

    /* 現在時刻の取得 */
    time(&timebuf);
    mt_localtime(&timebuf, &now);
    snprintf(dt, dt_size, "%d-%02d-%02d",
             now.tm_year+1900, now.tm_mon+1, now.tm_mday);
    return dt;
}

void log_initialize(const char* fname, int daily_flag)
{
    if (fname != NULL && *fname != '\0') {
        log_daily_flag = daily_flag;
        if (log_daily_flag) {
            int index;

            set_cur_date(log_cur_date, sizeof(log_cur_date));
            index = lastindexof(fname, '.');
            if (index < 0) {
                strcpy(log_basename, fname);
                log_extname[0] = '\0';
            } else {
                substr(log_basename, fname, 0, index);
                substr(log_extname, fname, index, -1);
            }
            log_open();
        } else {
            strcpy(log_basename, fname);
            log_open();
        }
    }
    /* クリティカルセクションの初期化 */
    CS_INIT(&log_critical_section);
}

void log_finalize()
{
    /* ファイルクローズ */
    log_close();

    /* クリティカルセクションの削除 */
    CS_DELETE(&log_critical_section);
}

void log_write(struct request_t* req, int status, int content_len)
{
    char date_buf[20];
    time_t timebuf;
    struct tm now;
    char* user_agent = "-";
    char* method = "-";
    char* uri = "-";
    char* protocol = "-";
    char* remote_ip_addr;
    char ip_addr[256];
    int lap_time;
    char outbuf[1024];

    if (log_fd < 0)
        return; 
    if (req == NULL)
        return; 

    /* 現在時刻の取得 */
    time(&timebuf);
    mt_localtime(&timebuf, &now);

    /* クリティカルセクションの開始 */
    CS_START(&log_critical_section);

    if (log_daily_flag) {
        snprintf(date_buf, sizeof(date_buf), "%d-%02d-%02d",
                 now.tm_year+1900, now.tm_mon+1, now.tm_mday);

        if (strcmp(log_cur_date, date_buf)) {
            /* ファイルを新しい日付で作成し直します。*/
            log_close();
            strcpy(log_cur_date, date_buf);
            log_open();
        }
    }

    user_agent = get_http_header(&req->header, "user-agent");
    method = req->method;
    uri = req->uri;
    protocol = req->protocol;

    /* IPアドレスを取得します。*/
    remote_ip_addr = get_http_header(&req->header, "X-Forwarded-For");
    if (remote_ip_addr == NULL) {
        mt_inet_ntoa(req->addr, ip_addr);
    } else {
        int index;

        /* プロキシサーバーを経由しているため
           クライアントに近い IPアドレスを取得します。*/
        index = indexof(remote_ip_addr, ',');
        if (index >= 0) {
            if (index > sizeof(ip_addr)-1)
                strcpy(ip_addr, "unknown");
            else
                substr(ip_addr, remote_ip_addr, 0, index);
        } else {
            if (strlen(remote_ip_addr) > sizeof(ip_addr)-1)
                strcpy(ip_addr, "unknown");
            else
                strcpy(ip_addr, remote_ip_addr);
        }
        trim(ip_addr);
    }

    /* リクエスト処理時間(usec) */
    lap_time = (int)(system_time() - req->start_time);

    /* ログの出力 */
    snprintf(outbuf, sizeof(outbuf), "%s [%d/%02d/%02d %02d:%02d:%02d] \"%s %s %s\" \"%s\" %d %d %d\n",
             ip_addr,
             now.tm_year+1900, now.tm_mon+1, now.tm_mday,
             now.tm_hour, now.tm_min, now.tm_sec,
             method, uri, protocol, user_agent,
             status, content_len, lap_time);
    FILE_WRITE(log_fd, outbuf, strlen(outbuf));

    /* クリティカルセクションの終了 */
    CS_END(&log_critical_section);
}
