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

static char* header_template_200 = 
    "HTTP/1.1 200 OK\r\n"
    "Date: %s\r\n"
    "Server: %s\r\n"
    "Content-Type: %s\r\n"
    "Content-Length: %d\r\n"
    "Last-Modified: %s\r\n"
    "Connection: close\r\n"
    "\r\n";

static char* header_keep_alive_200 = 
    "HTTP/1.1 200 OK\r\n"
    "Date: %s\r\n"
    "Server: %s\r\n"
    "Content-Type: %s\r\n"
    "Content-Length: %d\r\n"
    "Last-Modified: %s\r\n"
    "Keep-Alive: timeout=%d, max=%d\r\n"
    "Connection: Keep-Alive\r\n"
    "\r\n";

/* MIME type*/
struct mime_type {
    char *extension;
    char *type;
};

static struct mime_type mime_table[] = {
    { "html", "text/html" },
    { "htm", "text/html" },
    { "hdml", "text/x-hdml" },
    { "css", "text/css" },
    { "txt", "text/plain" },
    { "gif", "image/gif" },
    { "jpe", "image/jpeg" },
    { "jpeg", "image/jpeg" },
    { "jpg", "image/jpeg" },
    { "png", "image/png" },
    { "xbm", "image/x-bitmap" },
    { "au", "audio/basic" },
    { "snd", "audio/basic" },
    { "wav", "audio/x-wav" },
    { "aif", "audio/aiff" },
    { "aiff", "audio/aiff" },
    { "mp2", "audio/x-mpeg" },
    { "mp3", "audio/mpeg" },
    { "ram", "audio/x-pn-realaudio" },
    { "rm", "audio/x-pn-realaudio" },
    { "rm", "audio/x-pn-realaudio" },
    { "ra", "audio/x-pn-realaudio" },
    { "qt", "video/quicktime" },
    { "mov", "video/quicktime" },
    { "mpeg", "video/mpeg" },
    { "mpg", "video/mpeg" },
    { "mpe", "video/mpeg" },
    { "avi", "video/x-msvideo" },
    { "pdf", "application/vnd.pdf" },
    { "fdf", "application/vnd.fdf" },
    { "json", "text/plain" },
    { NULL, NULL }
}; 

static char* get_mime_type(char* ext_name)
{
    int i;

    for (i = 0; mime_table[i].extension != NULL; i++) {
        if (! strcmp(mime_table[i].extension, ext_name))
            return mime_table[i].type;
    }
    return NULL;
}

/*
 * リクエストされたファイル名をチェックします。
 * 上位ディレクトリ".."が指定されていた場合はベースよりも上位の場合はエラーになります。
 *
 * 0: okay
 * 1: error
 */
int check_file(const char* request_file)
{
    int ret_code = 0;
    int base = 0;
    char uri[MAX_URI_LENGTH];
    char** list_ptr;
    char** st;

    if (*request_file == '\0')
        return 1;   /* error */
    strcpy(uri, request_file);
    st = list_ptr = split(uri, '/');
    while (*st != NULL) {
        if (strcmp(*st, "..") == 0) {
            base--;
            if (base < 0) {
                ret_code = 1;
                break;   /* error */
            }
        } else if (**st == '.') {
            /* ignore */
        } else {
            base++;
        }
        st++;
    }
    list_free(list_ptr);
    return ret_code;
}

int doc_send(SOCKET socket,
             struct in_addr addr,
             const char* root,
             const char* file_name,
             struct http_header_t* hdr,
             int keep_alive_timeout,
             int keep_alive_requests,
             int* content_size)
{
    char fpath[MAX_PATH];
    int fd;
    struct stat file_stat;
    struct mmap_t* map;
    char send_buff[BUF_SIZE];
    int total_size = 0;
    int index;
    char ext_name[MAX_PATH];
    char* mime_type = NULL;
    char default_mime_type[256];

    char now_date[256]; 
    struct tm modify_gmt;
    char modify_date[256];
    char* head_date;

    /* フルパスのファイル名を生成します。*/
    snprintf(fpath, sizeof(fpath), "%s/%s", root, file_name);
#ifdef _WIN32
    /* パス区切り文字を置換する */
    chrep(fpath, '/', '\\');
#endif

    /* ファイル情報の取得 */
    if (stat(fpath, &file_stat) < 0) {
        err_log(addr, "fstat error (%s): %s", file_name, strerror(errno));
        /* Not Found(404)を送信 */
        return error_handler(socket, HTTP_NOTFOUND, content_size);
    }

    /* ディレクトリか調べます。*/
    if (S_ISDIR(file_stat.st_mode)) {
        /* Not Found(404)を送信 */
        return error_handler(socket, HTTP_NOTFOUND, content_size);
    }

    /* ファイル更新日時をヘッダー文字列(GMT)に変換します。*/
    mt_gmtime(&file_stat.st_mtime, &modify_gmt);
    gmtstr(modify_date, sizeof(modify_date), &modify_gmt);

    /* If-Modified-Since ヘッダーがあるか調べます。*/
    head_date = get_http_header(hdr, "If-Modified-Since");
    if (head_date != NULL) {
        if (strcmp(head_date, modify_date) == 0) {
            /* クライアントにキャッシュされているものを使用するように通知します。*/
            return forward_handler(socket, HTTP_NOT_MODIFIED, content_size);
        }
    }

    /* ファイルの拡張子からMIME/typeを決定 */
    ext_name[0] = '\0';
    index = lastindexof(fpath, '.');
    if (index > 0) {
        substr(ext_name, fpath, index+1, -1);
        mime_type = get_mime_type(ext_name);
    }
    if (mime_type == NULL) {
        snprintf(default_mime_type, sizeof(default_mime_type), "application/%s", ext_name);
        mime_type = default_mime_type;
    }

    /* 現在時刻をGMTで取得 */
    now_gmtstr(now_date, sizeof(now_date));

    /* ヘッダーの編集 */
    if (keep_alive_requests > 0) {
        snprintf(send_buff, sizeof(send_buff), header_keep_alive_200,
                 now_date, SERVER_NAME, mime_type, file_stat.st_size, modify_date,
                 keep_alive_timeout, keep_alive_requests);
    } else {
        snprintf(send_buff, sizeof(send_buff), header_template_200,
                 now_date, SERVER_NAME, mime_type, file_stat.st_size, modify_date);
    }

    /* ヘッダーの送信 */
    if (send_data(socket, send_buff, strlen(send_buff)) < 0) {
        err_log(addr, "document send error (%s): %s", file_name, strerror(errno)); 
    }

    if (g_file_cache != NULL) {
        char* cache_data;

        /* ファイルキャッシュからデータを取得します。*/
        cache_data = fc_get(g_file_cache, fpath, file_stat.st_mtime, file_stat.st_size);
        if (cache_data != NULL) {
            /* キャッシュ内容（ボディ）の送信 */
            *content_size = send_data(socket, cache_data, file_stat.st_size);
            return HTTP_OK;
        }
    }

    /* ファイルをオープンします。*/
    if ((fd = FILE_OPEN(fpath, O_RDONLY|O_BINARY, S_IREAD)) < 0) {
        /* Not Found(404)を送信 */
        err_log(addr, "request file can't open (%s): %s", file_name, strerror(errno));
        return error_handler(socket, HTTP_NOTFOUND, content_size);
    }

    /* メモリマップドファイル */
    map = mmap_open(fd, MMAP_READONLY, MMAP_AUTO_SIZE);
    if (map) {
        if (g_file_cache != NULL) {
            /* ファイル内容をキャッシュに設定します。*/
            fc_set(g_file_cache, fpath, file_stat.st_mtime, (int)map->size, map->ptr);
        }
        /* ボディの送信 */
        total_size = send_data(socket, map->ptr, (int)map->size);
        if (total_size < 0)
            err_log(addr, "document cache send error (%s): %s", file_name, strerror(errno)); 
        mmap_close(map);
    } else {
        if (g_file_cache != NULL) {
            char* data;

            /* ファイル内容をメモリに読み込んでキャッシュに設定します。*/
            data = (char*)malloc(file_stat.st_size);
            if (data != NULL) {
                if (FILE_READ(fd, data, file_stat.st_size) == file_stat.st_size) {
                    fc_set(g_file_cache, fpath, file_stat.st_mtime, file_stat.st_size, data);
                    /* ボディの送信 */
                    total_size = send_data(socket, data, file_stat.st_size);
                    if (total_size < 0)
                        err_log(addr, "document cache send error (%s): %s", file_name, strerror(errno)); 
                }
                free(data);
            }
        }

        /* キャッシュ設定がないのでファイルを読んでボディを送信します。*/
        if (total_size == 0) {
            int length;

            while ((length = FILE_READ(fd, send_buff, sizeof(send_buff))) > 0) {
                /* 読み込めたデータを送信 */
                if ((length = send_data(socket, send_buff, length)) < 0) {
                    err_log(addr, "document send error (%s): %s", file_name, strerror(errno)); 
                    break;
                }
                total_size += length;
            }
        }
    }

    /* 送信データサイズのチェック */
    if (file_stat.st_size != total_size) {
        err_log(addr, "file read size error (%s)", file_name);
    }
    /* ファイルクローズ */
    FILE_CLOSE(fd);
    *content_size = total_size;
    return HTTP_OK;
}
