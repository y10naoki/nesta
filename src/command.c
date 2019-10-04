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

static void get_local_url(char* url)
{
    if (g_conf->port_no > 0)
        sprintf(url, "%s:%d/", "http://127.0.0.1", g_conf->port_no);
    else
        strcpy(url, "http://127.0.0.1/");
}

void stop_server()
{
    char url[MAX_URI_LENGTH];
    char* query = "cmd=stop";
    char* res_str;

    get_local_url(url);
    res_str = url_post(url, NULL, query, NULL, 0, NULL);
    if (res_str == NULL) {
        fprintf(stdout, "not running.\n");
    } else {
        fprintf(stdout, "%s\n", res_str);
        recv_free(res_str);
    }
}

void status_server()
{
    char url[MAX_URI_LENGTH];
    char* query = "cmd=status";
    char* res_str;

    get_local_url(url);
    res_str = url_post(url, NULL, query, NULL, 0, NULL);
    if (res_str == NULL) {
        fprintf(stdout, "not running.\n");
    } else {
        fprintf(stdout, "%s\n", res_str);
        recv_free(res_str);
    }
}

void trace_mode_server(const char* mode)
{
    int trace_mode;
    char url[MAX_URI_LENGTH];
    char query[256];
    char* res_str;

    trace_mode = stricmp(mode, "off");
    get_local_url(url);
    sprintf(query, "cmd=trace_%s", (trace_mode)? "on" : "off");
    res_str = url_post(url, NULL, query, NULL, 0, NULL);
    if (res_str == NULL) {
        fprintf(stdout, "not running.\n");
    } else {
        fprintf(stdout, "%s\n", res_str);
        recv_free(res_str);
    }
}
