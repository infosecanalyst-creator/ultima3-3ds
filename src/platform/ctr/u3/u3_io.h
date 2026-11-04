// u3_io.h - all SD card writes go through one background writer, so the game
// never stalls on the card (a real 3DS SD costs 70-100 ms per operation and
// seconds to create or truncate a file; see the project's SD card notes).
//
// Game files (under U3_GAME_DIR) opened for writing by DOSBox live in memory
// while open; on close their contents are queued. Until the writer has put a
// file on the card, opens, stat and directory listings see the queued copy.
#ifndef U3_IO_H
#define U3_IO_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

void u3w_init();
// Replace a file's contents (queued; a later put for the same file replaces an unstarted one)
void u3w_put(const char *path, const void *data, size_t n);
// Append text to a file (queued)
void u3w_append(const char *path, const char *text);
// Milliseconds the writer has had work outstanding (0 when idle)
uint32_t u3w_busy_ms();
// Wait until everything queued is on the card (or the timeout passes)
void u3w_flush(int timeout_ms);

// DOSBox file layer hooks
FILE *u3w_fopen(const char *path, const char *mode);
int u3w_stat(const char *path, struct stat *st);
int u3w_unlink(const char *path);
int u3w_rename(const char *from, const char *to);
int u3w_ftruncate(FILE *f, long len);

#endif
