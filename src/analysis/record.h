#ifndef REVERSI_ANALYSIS_RECORD_H
#define REVERSI_ANALYSIS_RECORD_H

#include "analysis/analysis.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Text record format, version 1. See docs/RECORD_FORMAT.md for the public
 * grammar. The format is line based, ASCII, and strict:
 *
 *   reversi-record 1
 *   result B+2                 (optional; "-" means ongoing)
 *   moves d3 c3 f4 pass ...
 *   variation 3 f5 g5 ...      (0..8 lines, ply is 0-based)
 *
 * The main line always starts from the standard opening. Variations branch
 * from a ply on the main line and are exported one level deep.
 */

/*
 * Writes the session's main line and its one-level variations. Returns the
 * number of bytes written excluding the terminator, or 0 when the buffer is
 * too small or the session is invalid.
 */
size_t analysis_record_write(const AnalysisSession *session, char *buffer,
                             size_t capacity);

/*
 * Parses a record into a fresh session. On failure returns false, writes a
 * human-readable reason to error (when provided), and leaves the session
 * unchanged.
 */
bool analysis_record_read(AnalysisSession *session, const char *text,
                          char *error, size_t error_size);

#endif
