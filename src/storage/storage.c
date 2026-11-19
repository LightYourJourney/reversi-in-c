#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
/* fsync(fileno(...)) needs POSIX declarations; newer libc/clang hide them
 * under -std=c11 otherwise. */
#define _POSIX_C_SOURCE 200809L
#endif

#include "storage/storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#include <wchar.h>
#include <windows.h>
#define REVERSI_STORAGE_IS_DIR(mode) (((mode) & _S_IFDIR) != 0)
#define REVERSI_STORAGE_IS_REG(mode) (((mode) & _S_IFREG) != 0)
typedef struct _stat StorageStat;
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define REVERSI_STORAGE_IS_DIR(mode) S_ISDIR(mode)
#define REVERSI_STORAGE_IS_REG(mode) S_ISREG(mode)
typedef struct stat StorageStat;
#endif

#define REVERSI_STORAGE_PATH_CAP 1024

/*
 * Paths are UTF-8 strings everywhere in the program. On Windows the ANSI
 * file APIs would reinterpret them in the active code page, so a profile
 * directory such as C:\Users\Zoë or C:\Users\张三 could not be reached;
 * every file system call below converts to UTF-16 and uses the wide API.
 */
#ifdef _WIN32
static bool to_wide(const char *text, wchar_t *out, int cap)
{
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out,
                               cap) > 0;
}

static bool from_wide(const wchar_t *text, char *out, size_t cap)
{
    return WideCharToMultiByte(CP_UTF8, 0, text, -1, out, (int)cap, NULL,
                               NULL) > 0;
}
#endif

static int path_stat(const char *path, StorageStat *info)
{
#ifdef _WIN32
    wchar_t wide[REVERSI_STORAGE_PATH_CAP];
    return to_wide(path, wide, REVERSI_STORAGE_PATH_CAP)
        ? _wstat(wide, info) : -1;
#else
    return stat(path, info);
#endif
}

static int path_mkdir(const char *path)
{
#ifdef _WIN32
    wchar_t wide[REVERSI_STORAGE_PATH_CAP];
    if (!to_wide(path, wide, REVERSI_STORAGE_PATH_CAP)) {
        errno = EINVAL;
        return -1;
    }
    return _wmkdir(wide);
#else
    return mkdir(path, 0700);
#endif
}

static FILE *path_fopen(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t wide[REVERSI_STORAGE_PATH_CAP];
    wchar_t wide_mode[8];
    if (!to_wide(path, wide, REVERSI_STORAGE_PATH_CAP)
        || !to_wide(mode, wide_mode, 8)) {
        return NULL;
    }
    return _wfopen(wide, wide_mode);
#else
    return fopen(path, mode);
#endif
}

static int path_remove(const char *path)
{
#ifdef _WIN32
    wchar_t wide[REVERSI_STORAGE_PATH_CAP];
    return to_wide(path, wide, REVERSI_STORAGE_PATH_CAP)
        ? _wremove(wide) : -1;
#else
    return remove(path);
#endif
}

/* Replaces `path` with `temp` in one step. */
static bool path_replace(const char *temp, const char *path)
{
#ifdef _WIN32
    wchar_t wide_temp[REVERSI_STORAGE_PATH_CAP];
    wchar_t wide_path[REVERSI_STORAGE_PATH_CAP];
    return to_wide(temp, wide_temp, REVERSI_STORAGE_PATH_CAP)
        && to_wide(path, wide_path, REVERSI_STORAGE_PATH_CAP)
        && MoveFileExW(wide_temp, wide_path, MOVEFILE_REPLACE_EXISTING);
#else
    return rename(temp, path) == 0;
#endif
}

static bool copy_text(char *out, size_t cap, const char *text)
{
    if (out == NULL || cap == 0 || text == NULL) {
        return false;
    }
    const size_t length = strlen(text);
    if (length + 1 > cap) {
        return false;
    }
    memcpy(out, text, length + 1);
    return true;
}

static bool path_is_dir(const char *path)
{
    StorageStat info;
    if (path_stat(path, &info) != 0) {
        return false;
    }
    return REVERSI_STORAGE_IS_DIR(info.st_mode);
}

static bool make_dir_recursive(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    if (path_is_dir(path)) {
        return true;
    }

    char parent[REVERSI_STORAGE_PATH_CAP];
    if (!copy_text(parent, sizeof(parent), path)) {
        return false;
    }

    char *cut = strrchr(parent, '/');
#ifdef _WIN32
    char *backslash = strrchr(parent, '\\');
    if (backslash != NULL && (cut == NULL || backslash > cut)) {
        cut = backslash;
    }
#endif
    if (cut != NULL && cut != parent) {
        /* Keep the trailing separator out of the parent path. */
        const bool drive_root = cut == parent + 2 && parent[1] == ':';
        if (!drive_root) {
            *cut = '\0';
            if (!make_dir_recursive(parent)) {
                return false;
            }
        }
    }

    if (path_mkdir(path) == 0) {
        return true;
    }
    return errno == EEXIST && path_is_dir(path);
}

bool storage_user_dir(char *out, size_t cap)
{
#ifdef _WIN32
    /* The wide environment holds the real profile path; getenv would give
     * its code-page approximation. */
    char base_utf8[REVERSI_STORAGE_PATH_CAP];
    char profile_utf8[REVERSI_STORAGE_PATH_CAP];
    const wchar_t *wide_base = _wgetenv(L"APPDATA");
    const wchar_t *wide_profile = _wgetenv(L"USERPROFILE");
    const char *base = wide_base != NULL && wide_base[0] != L'\0'
            && from_wide(wide_base, base_utf8, sizeof(base_utf8))
        ? base_utf8 : NULL;
    if (base == NULL) {
        const char *profile = wide_profile != NULL
                && wide_profile[0] != L'\0'
                && from_wide(wide_profile, profile_utf8,
                             sizeof(profile_utf8))
            ? profile_utf8 : NULL;
        if (profile == NULL) {
            return false;
        }
        char path[REVERSI_STORAGE_PATH_CAP];
        const int written = snprintf(path, sizeof(path),
                                     "%s\\AppData\\Roaming\\Reversi",
                                     profile);
        return written > 0 && (size_t)written < sizeof(path)
            && copy_text(out, cap, path);
    }
    char path[REVERSI_STORAGE_PATH_CAP];
    const int written = snprintf(path, sizeof(path), "%s\\Reversi", base);
    return written > 0 && (size_t)written < sizeof(path)
        && copy_text(out, cap, path);
#else
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return false;
    }
    char path[REVERSI_STORAGE_PATH_CAP];
#if defined(__APPLE__)
    const int written = snprintf(path, sizeof(path),
                                 "%s/Library/Application Support/Reversi",
                                 home);
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    int written;
    if (xdg != NULL && xdg[0] != '\0') {
        written = snprintf(path, sizeof(path), "%s/reversi", xdg);
    } else {
        written = snprintf(path, sizeof(path),
                           "%s/.local/share/reversi", home);
    }
#endif
    return written > 0 && (size_t)written < sizeof(path)
        && copy_text(out, cap, path);
#endif
}

bool storage_make_user_dir(void)
{
    char dir[REVERSI_STORAGE_PATH_CAP];
    if (!storage_user_dir(dir, sizeof(dir))) {
        return false;
    }
    return make_dir_recursive(dir);
}

bool storage_path(const char *name, char *out, size_t cap)
{
    if (name == NULL || name[0] == '\0' || strcmp(name, ".") == 0
        || strcmp(name, "..") == 0 || strchr(name, '/') != NULL
#ifdef _WIN32
        || strchr(name, '\\') != NULL
#endif
    ) {
        return false;
    }
    char dir[REVERSI_STORAGE_PATH_CAP];
    if (!storage_user_dir(dir, sizeof(dir))) {
        return false;
    }
#ifdef _WIN32
    const char *separator = "\\";
#else
    const char *separator = "/";
#endif
    const int written = snprintf(out, cap, "%s%s%s", dir, separator, name);
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

    StorageStat info;
    if (path_stat(path, &info) != 0
        || !REVERSI_STORAGE_IS_REG(info.st_mode)) {
        return false;
    }

    FILE *file = path_fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    const long length = ftell(file);
    if (length < 0 || (size_t)length > max_size
        || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    const size_t size = (size_t)length;
    char *buffer = malloc(size + 1);
    if (buffer == NULL) {
        fclose(file);
        return false;
    }
    if (size > 0 && fread(buffer, 1, size, file) != size) {
        free(buffer);
        fclose(file);
        return false;
    }
    buffer[size] = '\0';
    if (fclose(file) != 0) {
        free(buffer);
        return false;
    }

    *out = buffer;
    *out_size = size;
    return true;
}

bool storage_write_atomic(const char *path, const char *data, size_t size)
{
    if (path == NULL || path[0] == '\0' || (data == NULL && size > 0)) {
        return false;
    }

    char temp[REVERSI_STORAGE_PATH_CAP];
    const int written = snprintf(temp, sizeof(temp), "%s.tmp", path);
    if (written <= 0 || (size_t)written >= sizeof(temp)) {
        return false;
    }

    (void)path_remove(temp);
    FILE *file = path_fopen(temp, "wb");
    if (file == NULL) {
        return false;
    }

    bool ok = true;
    if (size > 0 && fwrite(data, 1, size, file) != size) {
        ok = false;
    }
    if (ok && fflush(file) != 0) {
        ok = false;
    }
#ifndef _WIN32
    if (ok && fsync(fileno(file)) != 0) {
        ok = false;
    }
#else
    if (ok && _commit(_fileno(file)) != 0) {
        ok = false;
    }
#endif
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok || !path_replace(temp, path)) {
        (void)path_remove(temp);
        return false;
    }
    return true;
}

bool storage_remove(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    return path_remove(path) == 0 || !storage_exists(path);
}

bool storage_exists(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    StorageStat info;
    return path_stat(path, &info) == 0;
}
