#ifndef REVERSI_STORAGE_STORAGE_H
#define REVERSI_STORAGE_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Resolves the OS-appropriate per-user data directory (never the program
 * installation directory). Returns false when the environment provides no
 * usable home directory.
 */
bool storage_user_dir(char *out, size_t cap);

/* Creates the user data directory and any missing parents. */
bool storage_make_user_dir(void);

/* Joins a file name under the user data directory. */
bool storage_path(const char *name, char *out, size_t cap);

/*
 * Reads a whole file into a NUL-terminated heap buffer. Fails for missing
 * files, non-regular files, empty paths, sizes above max_size, and I/O
 * errors. The caller owns the buffer.
 */
bool storage_read(const char *path, char **out, size_t *out_size,
                  size_t max_size);

/*
 * Writes via a sibling temporary file followed by an atomic rename. The
 * destination is either fully replaced or left unchanged.
 */
bool storage_write_atomic(const char *path, const char *data, size_t size);

bool storage_remove(const char *path);
bool storage_exists(const char *path);

#endif
