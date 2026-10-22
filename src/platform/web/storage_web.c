/*
 * Browser storage adapter.
 *
 * The save/settings/stats serializers are unchanged; only the seven
 * storage.h entry points are replaced. Keys are the file names used on
 * desktop ("settings.dat", "autosave.rvs", ...), prefixed with "reversi:".
 * localStorage is synchronous, so the existing load-on-start and
 * write-on-change call sites keep their ordering.
 */
#include "storage/storage.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EM_JS(int, reversi_web_storage_read,
      (const char *name, char **out, size_t *out_size, size_t max_size), {
    try {
        var value = localStorage.getItem('reversi:' + UTF8ToString(name));
        if (value === null) {
            return 0;
        }
        var length = lengthBytesUTF8(value);
        if (length > max_size) {
            return 0;
        }
        var buffer = _malloc(length + 1);
        if (!buffer) {
            return 0;
        }
        stringToUTF8(value, buffer, length + 1);
        HEAPU32[out >> 2] = buffer;
        HEAPU32[out_size >> 2] = length;
        return 1;
    } catch (e) {
        return 0;
    }
})

EM_JS(int, reversi_web_storage_write,
      (const char *name, const char *data, size_t size), {
    try {
        localStorage.setItem('reversi:' + UTF8ToString(name),
                             UTF8ToString(data, size));
        return 1;
    } catch (e) {
        return 0;
    }
})

EM_JS(int, reversi_web_storage_remove, (const char *name), {
    try {
        localStorage.removeItem('reversi:' + UTF8ToString(name));
        return 1;
    } catch (e) {
        return 0;
    }
})

EM_JS(int, reversi_web_storage_exists, (const char *name), {
    try {
        return localStorage.getItem('reversi:' + UTF8ToString(name)) !== null
            ? 1 : 0;
    } catch (e) {
        return 0;
    }
})

static bool valid_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return false;
    }
    return strchr(name, '/') == NULL && strchr(name, '\\') == NULL;
}

bool storage_user_dir(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return false;
    }
    const int written = snprintf(out, cap, "%s", "reversi");
    return written > 0 && (size_t)written < cap;
}

bool storage_make_user_dir(void)
{
    return true;
}

bool storage_path(const char *name, char *out, size_t cap)
{
    if (!valid_name(name) || out == NULL || cap == 0) {
        return false;
    }
    const int written = snprintf(out, cap, "%s", name);
    return written > 0 && (size_t)written < cap;
}

bool storage_read(const char *path, char **out, size_t *out_size,
                  size_t max_size)
{
    if (path == NULL || path[0] == '\0' || out == NULL || out_size == NULL) {
        return false;
    }
    *out = NULL;
    *out_size = 0;
    return reversi_web_storage_read(path, out, out_size, max_size) != 0;
}

bool storage_write_atomic(const char *path, const char *data, size_t size)
{
    if (path == NULL || path[0] == '\0' || (data == NULL && size > 0)) {
        return false;
    }
    return reversi_web_storage_write(path, data, size) != 0;
}

bool storage_remove(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    return reversi_web_storage_remove(path) != 0;
}

bool storage_exists(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    return reversi_web_storage_exists(path) != 0;
}
