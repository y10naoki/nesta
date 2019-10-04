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

#ifndef _WIN32
#include <dlfcn.h>
#endif

#ifdef _WIN32
typedef HINSTANCE LIBHANDLE;
#define LIB_OPEN(x)         LoadLibrary((LPCWSTR)x)
#define LIB_CLOSE(x)        FreeLibrary(x)
#define LIB_GETFUNC(x,y)    GetProcAddress(x,y)
#else
typedef void*   LIBHANDLE;
#define LIB_OPEN(x)         dlopen(x,RTLD_LAZY)
#define LIB_CLOSE(x)        dlclose(x)
#define LIB_GETFUNC(x,y)    dlsym(x,y)
#endif

struct lib_info {
    char name[MAX_PATH+1];  /* ライブラリ名 */
    LIBHANDLE handle;       /* ハンドル */
};

static int _lib_count;
static struct lib_info* _lib_tbl;

static LIBHANDLE get_lib_handle(const char* lib_name)
{
    int i;

    for (i = 0; i < _lib_count; i++) {
        if (stricmp(_lib_tbl[i].name, lib_name) == 0)
            return _lib_tbl[i].handle;
    }
    return NULL;
}

static int set_lib_handle(const char* lib_name, LIBHANDLE handle)
{
    if (strlen(lib_name) > MAX_PATH)
        return -1;

    if (_lib_count == 0) {
        _lib_tbl = (struct lib_info*)malloc(sizeof(struct lib_info));
        if (_lib_tbl == NULL) {
            fprintf(stderr, "dynlib: no memory.\n");
            return -1;
        }
    } else {
        struct lib_info* tp;

        tp = (struct lib_info*)realloc(_lib_tbl, sizeof(struct lib_info) * (_lib_count+1));
        if (tp == NULL) {
            fprintf(stderr, "dynlib: no memory.\n");
            return -1;
        }
        _lib_tbl = tp;
    }

    strncpy(_lib_tbl[_lib_count].name, lib_name, MAX_PATH);
    _lib_tbl[_lib_count].handle = handle;
    _lib_count++;
    return _lib_count - 1;
}

static void* dyn_func_load(const char* func_name, const char* lib_name)
{
    LIBHANDLE handle;
    void* func;
    char lib_path[MAX_PATH+1];

    /* 絶対パスに変換します。*/
    get_abspath(lib_path, lib_name, MAX_PATH);

    /* ライブラリ名をキーにすでにロードされているか調べます。*/
    handle = get_lib_handle(lib_path);
    if (handle == NULL) {
        /* ライブラリがロードされていないのでオープンします。*/
#ifdef _WIN32
        /* WIN32環境では LoadLibraryのDLL名を指定するパラメータが
           LPCWSTRのためワイドキャラクタに変換します。*/
        WCHAR wc_name[MAX_PATH+1];
        MultiByteToWideChar(CP_ACP, 0, lib_path, -1, wc_name, MAX_PATH);
        handle = LIB_OPEN(wc_name);
#else
        handle = LIB_OPEN(lib_path);
#endif
        if (handle == NULL) {
            fprintf(stderr, "dynlib: can't load library: %s(%s)\n", lib_name, func_name);
            return NULL;
        }
        /* 管理テーブルに設定します。*/
        if (set_lib_handle(lib_path, handle) < 0) {
            fprintf(stderr, "dynlib: set library error: %s(%s)\n", lib_name, func_name);
            LIB_CLOSE(handle);
            return NULL;
        }
    }

    /* ライブラリから関数のポインタを取得します。*/
    func = LIB_GETFUNC(handle, func_name);
    if (func == NULL) {
#ifdef _WIN32
        fprintf(stderr, "dynlib: get library function error: %s(%s)\n", lib_name, func_name);
#else
        char* func_err;
        func_err = dlerror();
        if (func_err)
            fprintf(stderr, "dynlib: get library function error: %s: %s(%s)\n", func_err, lib_name, func_name);
#endif
        return NULL;
    }
    return func;
}

/*
 * 指定されたダイナミックライブラリをオープンして関数のポインタを
 * 管理テーブルに設定します。
 *
 * zone: アプリケーション・ゾーン構造体のポインタ
 * app_name: アプリケーション名
 * func_name: 関数名
 * lib_name: ダイナミックライブラリ名
 *
 * 戻り値
 *  正常に終了した場合はゼロを返します。
 *  エラーの場合は -1 を返します。
 */
int dyn_api_load(struct appzone_t* zone,
                 const char* app_name,
                 const char* func_name,
                 const char* lib_name)
{
    API_FUNCPTR func;

    if (strlen(app_name) > MAX_CONTENT_NAME) {
        fprintf(stderr, "dynlib: content name too large: %s\n", app_name);
        return -1;
    }

    /* ライブラリから関数のポインタを取得します。*/
    func = (API_FUNCPTR)dyn_func_load(func_name, lib_name);

    /* configにアプリケーション名と関数のポインタを設定します。*/
    strcpy(g_conf->api_table[g_conf->api_count].content_name, app_name);
    g_conf->api_table[g_conf->api_count].app_zone = zone;
    g_conf->api_table[g_conf->api_count].func_ptr = func;
    g_conf->api_count++;

    TRACE("[api] %s in %s loaded.\n", func_name, lib_name);
    return 0;
}

/*
 * 指定されたダイナミックライブラリをオープンして初期化関数のポインタを
 * 管理テーブルに設定します。
 *
 * func_name: 関数名
 * lib_name: ダイナミックライブラリ名
 *
 * 戻り値
 *  正常に終了した場合はゼロを返します。
 *  エラーの場合は -1 を返します。
 */
int dyn_init_api_load(const char* func_name, const char* lib_name)
{
    HOOK_FUNCPTR func;

    /* ライブラリから関数のポインタを取得します。*/
    func = (HOOK_FUNCPTR)dyn_func_load(func_name, lib_name);

    /* configにアプリケーション名と関数のポインタを設定します。*/
    g_conf->init_api_table[g_conf->init_api_count++] = func;

    TRACE("[init api] %s in %s loaded.\n", func_name, lib_name);
    return 0;
}

/*
 * 指定されたダイナミックライブラリをオープンして終了関数のポインタを
 * 管理テーブルに設定します。
 *
 * func_name: 関数名
 * lib_name: ダイナミックライブラリ名
 *
 * 戻り値
 *  正常に終了した場合はゼロを返します。
 *  エラーの場合は -1 を返します。
 */
int dyn_term_api_load(const char* func_name, const char* lib_name)
{
    HOOK_FUNCPTR func;

    /* ライブラリから関数のポインタを取得します。*/
    func = (HOOK_FUNCPTR)dyn_func_load(func_name, lib_name);

    /* configにアプリケーション名と関数のポインタを設定します。*/
    g_conf->term_api_table[g_conf->term_api_count++] = func;

    TRACE("[term api] %s in %s loaded.\n", func_name, lib_name);
    return 0;
}

/*
 * ロードされているすべてのダイナミックライブラリをクローズします。
 *
 * 戻り値
 *  なし
 */
void dyn_unload()
{
    int i;

    for (i = 0; i < _lib_count; i++) {
        if (_lib_tbl[i].handle != NULL) {
            /* ダイナミックライブラリをクローズします。*/
            LIB_CLOSE(_lib_tbl[i].handle);
        }
    }
    free(_lib_tbl);
    _lib_count = 0;
}
