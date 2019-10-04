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

#define R_BUF_SIZE 1024
#define CMD_INCLUDE "include"

/*
 * コンフィグファイルのパラメータ名を検索して件数を取得します。
 *
 * ".api", ".init_api" and ".term_api"の設定件数を
 * 調べるのに使用されます。
 */
int config_name_count(const char* conf_fname, const char* pname)
{
    FILE *fp;
    char fpath[MAX_PATH+1];
    char buf[R_BUF_SIZE];
    int count = 0;

    get_abspath(fpath, conf_fname, MAX_PATH);
    if ((fp = fopen(fpath, "r")) == NULL) {
        fprintf(stderr, "file open error: %s\n", conf_fname);
        return 0;
    }

    while (fgets(buf, sizeof(buf), fp) != NULL) {
        int index;
        char name[R_BUF_SIZE];
        char value[R_BUF_SIZE];

        /* コメントの排除 */
        index = indexof(buf, '#');
        if (index >= 0) {
            buf[index] = '\0';
            if (strlen(buf) == 0)
                continue;
        }
        /* 名前と値の分離 */
        index = indexof(buf, '=');
        if (index <= 0)
            continue;

        substr(name, buf, 0, index);
        substr(value, buf, index+1, -1);

        /* 両端のホワイトスペースを取り除きます。*/
        trim(name);
        trim(value);

        if (strstr(name, pname)) {
            count++;
        } else if (stricmp(name, CMD_INCLUDE) == 0) {
            /* 他のconfigファイルを再帰処理で読み込みます。*/
            count += config_name_count(conf_fname, pname);
        }
    }

    fclose(fp);
    return count;
}

static int get_relay_host_index(const char* host)
{
    int i;

    for (i = 0; i < g_conf->session_relay_copy_count; i++) {
        if (stricmp(g_conf->session_relay_copy_host[i], host) == 0)
            return i;
    }
    return -1;  /* notfound */
}

static struct appzone_t* get_appzone(const char* name)
{
    int index;
    char* key;
    struct appzone_t* z;
    int zone_c;
    int i;

    key = (char*)alloca(strlen(name)+1);
    strcpy(key, name);
    index = lastindexof(key, '.');
    if (index < 0)
        return NULL;
    key[index] = '\0';

    zone_c = vect_count(g_conf->zone_table);
    for (i = 0; i < zone_c; i++) {
        z = vect_get(g_conf->zone_table, i);
        if (z != NULL) {
            if (stricmp(z->zone_name, key) == 0)
                return z;
        }
    }
    return NULL;  /* notfound */
}

/*
 * コンフィグファイルを読んでパラメータを設定します。
 * 既知のパラメータ以外はユーザーパラメータとして登録します。
 * パラメータの形式は "name=value"の形式とします。
 *
 * conf_fname: コンフィグファイル名
 * start_mode: 起動パラメータが -start かどうかを示すフラグ
 *             ダイナミックライブラリをロードするかどうかのフラグになる
 *
 * 戻り値
 *  0: 成功
 * -1: 失敗
 *
 * (config parameters)
 * http.daemon = 1 or 0 (default is 0, unix only)
 * http.username = string (default none, unix only)
 * http.port_no = number (default is 8080)
 * http.backlog = number (default is 50)
 * http.worker_thread = number (default is 10)
 * http.extend_worker_thread = number (default is zero)
 * http.worker_thread_timeout = number (default is 600 seconds)
 * http.worker_thread_check_interval = number (default is 1800 seconds)
 * http.keep_alive_timeout = number (default is 3 seconds)
 * http.keep_alive_requests = number (default is 5)
 * http.document_root = path (default is nothing)
 * http.file_cache_size = kbytes (default is not file-cache)
 * http.access_log_fname = path/file (default is nolog)
 * http.daily_log_flag = 1 or 0 (default is 0)
 * http.error_file = path/file (default is stderr)
 * http.output_file = path/file (default is stdout)
 * http.trace_flag = 1 or 0 (default is 0)
 *
 * http.session_relay.host = my IP-address (internal IP)
 * http.session_relay.port = number (default is 9080)
 * http.session_relay.backlog = number (default is 5)
 * http.session_relay.worker_thread = number (default is 1)
 * http.session_relay.check_interval_time = number (default is 300(seconds))
 *
 * http.session_relay.copy.host = COPY_TO_IP-ADDRESS (internal IP)
 * COPY_TO_IP-ADDRESS.session_relay.copy.port = number (default is 9080)
 *
 * http.appzone = ZONE-NAME
 *   ZONE-NAME.max_session = number (0 is session unuse, -1 is unlimited)
 *   ZONE-NAME.session_timeout = number (seconds, -1 is no timeout)
 *   ZONE-NAME.init_api = 関数名, ライブラリ名
 *   ZONE-NAME.api = コンテンツ名, 関数名, ライブラリ名
 *   ZONE-NAME.term_api = 関数名, ライブラリ名
 *
 * include = FILE_NAME
 * ...
 *
 * (others user parameter)
 */
int config(const char* conf_fname, int start_mode)
{
    FILE *fp;
    char fpath[MAX_PATH+1];
    char buf[R_BUF_SIZE];
    int err = 0;

    get_abspath(fpath, conf_fname, MAX_PATH);
    if ((fp = fopen(fpath, "r")) == NULL) {
        fprintf(stderr, "file open error: %s\n", conf_fname);
        return -1;
    }

    while (fgets(buf, sizeof(buf), fp) != NULL) {
        int index;
        char name[R_BUF_SIZE];
        char value[R_BUF_SIZE];

        /* コメントの排除 */
        index = indexof(buf, '#');
        if (index >= 0) {
            buf[index] = '\0';
            if (strlen(buf) == 0)
                continue;
        }
        /* 名前と値の分離 */
        index = indexof(buf, '=');
        if (index <= 0)
            continue;

        substr(name, buf, 0, index);
        substr(value, buf, index+1, -1);

        /* 両端のホワイトスペースを取り除きます。*/
        trim(name);
        trim(value);

        if (strlen(name) > MAX_VNAME_SIZE-1) {
            fprintf(stderr, "parameter name too large: %s\n", buf);
            err = -1;
            break;
        }
        if (strlen(value) > MAX_VVALUE_SIZE-1) {
            fprintf(stderr, "parameter value too large: %s\n", buf);
            err = -1;
            break;
        }

        if (stricmp(name, "http.document_root") == 0) {
            get_abspath(g_conf->document_root, value, sizeof(g_conf->document_root)-1);
        } else if (stricmp(name, "http.port_no") == 0) {
            g_conf->port_no = (ushort)atoi(value);
        } else if (stricmp(name, "http.backlog") == 0) {
            g_conf->backlog = atoi(value);
        } else if (stricmp(name, "http.worker_thread") == 0) {
            g_conf->worker_threads = atoi(value);
        } else if (stricmp(name, "http.extend_worker_thread") == 0) {
            g_conf->extend_worker_threads = atoi(value);
        } else if (stricmp(name, "http.worker_thread_timeout") == 0) {
            g_conf->worker_thread_timeout = atoi(value);
        } else if (stricmp(name, "http.worker_thread_check_interval") == 0) {
            g_conf->worker_thread_check_interval = atoi(value);
        } else if (stricmp(name, "http.keep_alive_timeout") == 0) {
            g_conf->keep_alive_timeout = atoi(value);
        } else if (stricmp(name, "http.keep_alive_requests") == 0) {
            g_conf->keep_alive_requests = atoi(value);
        } else if (stricmp(name, "http.daemon") == 0) {
            g_conf->daemonize = atoi(value);
        } else if (stricmp(name, "http.username") == 0) {
            strncpy(g_conf->username, value, sizeof(g_conf->username)-1);
        } else if (stricmp(name, "http.file_cache_size") == 0) {
            g_conf->file_cache_size = atol(value) * 1024L;
        } else if (stricmp(name, "http.access_log_fname") == 0) {
            get_abspath(g_conf->access_log_fname, value, sizeof(g_conf->access_log_fname)-1);
        } else if (stricmp(name, "http.daily_log_flag") == 0) {
            g_conf->daily_log_flag = atoi(value);
        } else if (stricmp(name, "http.error_file") == 0) {
            get_abspath(g_conf->error_file, value, sizeof(g_conf->error_file)-1);
        } else if (stricmp(name, "http.output_file") == 0) {
            get_abspath(g_conf->output_file, value, sizeof(g_conf->output_file)-1);
        } else if (stricmp(name, "http.trace_flag") == 0) {
            g_trace_mode = atoi(value);
        } else if (stricmp(name, "http.session_relay.host") == 0) {
            strncpy(g_conf->session_relay_host, value, sizeof(g_conf->session_relay_host)-1);
        } else if (stricmp(name, "http.session_relay.port") == 0) {
            g_conf->session_relay_port = (ushort)atoi(value);
        } else if (stricmp(name, "http.session_relay.backlog") == 0) {
            g_conf->session_relay_backlog = atoi(value);
        } else if (stricmp(name, "http.session_relay.worker_thread") == 0) {
            g_conf->session_relay_worker_threads = atoi(value);
        } else if (stricmp(name, "http.session_relay.check_interval_time") == 0) {
            g_conf->session_relay_check_interval = atoi(value);
        } else if (stricmp(name, "http.session_relay.copy.host") == 0) {
            int i = g_conf->session_relay_copy_count;
            strncpy(g_conf->session_relay_copy_host[i], value, sizeof(g_conf->session_relay_copy_host[0])-1);
            g_conf->session_relay_copy_count++;
        } else if (strstr(name, ".session_relay.copy.port")) {
            int i = indexofstr(name, ".session_relay.copy.port");
            if (i >= 0) {
                char host[MAX_HOSTNAME];

                substr(host, name, 0, i);
                i = get_relay_host_index(host);
                if (i >= 0)
                    g_conf->session_relay_copy_port[i] = (ushort)atoi(value);
            }
        } else if (stricmp(name, "http.appzone") == 0) {
            struct appzone_t* z;
            z = (struct appzone_t*)calloc(1, sizeof(struct appzone_t));
            if (z == NULL) {
                fprintf(stderr, "config(): no memory.\n");
                err = -1;
                break;
            }
            strcpy(z->zone_name, value);
            if (vect_append(g_conf->zone_table, z) < 0) {
                fprintf(stderr, "config(): vect_append error\n");
                err = -1;
                break;
            }
        } else if (strstr(name, ".max_session")) {
            struct appzone_t* z;
            z = get_appzone(name);
            if (z == NULL) {
                fprintf(stderr, "undefined appzone name(max_session): %s\n", name);
                err = -1;
                break;
            }
            z->max_session = atoi(value);
        } else if (strstr(name, ".session_timeout")) {
            struct appzone_t* z;
            z = get_appzone(name);
            if (z == NULL) {
                fprintf(stderr, "undefined appzone name(session_timeout): %s\n", name);
                err = -1;
                break;
            }
            z->session_timeout = atoi(value);
        } else if (strstr(name, ".api")) {
            struct appzone_t* z;
            z = get_appzone(name);
            if (z == NULL) {
                fprintf(stderr, "undefined appzone name: %s\n", name);
                err = -1;
                break;
            }
            if (start_mode) {
                /* ライブラリから関数をロードします。*/
                char** list;
                list = split(value, ',');
                if (list != NULL) {
                    int n;
                    n = list_count((const char**)list);
                    if (n == 3)
                        dyn_api_load(z, list[0], list[1], list[2]);
                    else
                        fprintf(stderr, "illegal '%s.api' parameter: %s\n", z->zone_name, value);
                    list_free(list);
                } else
                    fprintf(stderr, "illegal '%s.api' parameter: %s\n", z->zone_name, value);
            }
        } else if (strstr(name, ".init_api") || strstr(name, ".term_api")) {
            struct appzone_t* z;
            z = get_appzone(name);
            if (z == NULL) {
                fprintf(stderr, "undefined appzone name: %s\n", name);
                err = -1;
                break;
            }
            if (start_mode) {
                /* ライブラリから関数をロードします。*/
                char** list;
                list = split(value, ',');
                if (list != NULL) {
                    int n;
                    n = list_count((const char**)list);
                    if (n == 2) {
                        if (strstr(name, ".init_api"))
                            dyn_init_api_load(list[0], list[1]);
                        else
                            dyn_term_api_load(list[0], list[1]);
                    } else
                        fprintf(stderr, "illegal '%s.init_api or %s.term_api' parameter: %s\n", z->zone_name, z->zone_name, value);
                    list_free(list);
                } else
                    fprintf(stderr, "illegal '%s.init_api or %s.term_api' parameter: %s\n", z->zone_name, z->zone_name, value);
            }
        } else if (stricmp(name, CMD_INCLUDE) == 0) {
            /* 他のconfigファイルを再帰処理で読み込みます。*/
            if (config(value, start_mode) < 0)
                break;
        } else {
            /* ユーザーパラメータ */
            int n;

            n = g_conf->u_param.count;
            if (n >= MAX_USER_VARIABLE) {
                fprintf(stderr, "user parameter too many count maximum: %d\n", MAX_USER_VARIABLE);
                err = -1;
                break;
            }

            g_conf->u_param.vt[n].name = (char*)malloc(strlen(name) + strlen(value) + 2);
            if (g_conf->u_param.vt[n].name == NULL) {
                fprintf(stderr, "user parameter no memory: %s=%s\n", name, value);
                err = -1;
                break;
            }
            strcpy(g_conf->u_param.vt[n].name, name);
            g_conf->u_param.vt[n].value = g_conf->u_param.vt[n].name + strlen(name) + 1;
            strcpy(g_conf->u_param.vt[n].value, value);
            g_conf->u_param.count++;
        }
    }

    fclose(fp);
    return err;
}
