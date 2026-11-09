
#ifndef _MFILE_H_
#define _MFILE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Open "path" for reading.  By default this is a plain fopen("rb") stream;
 * with FFPIC_MMAP=1 the file is mapped into memory and reads are served
 * from the mapped pages instead of read() syscalls.  Either way the
 * returned FILE* behaves like a normal stream (fread/fseek/ftell/fclose
 * all work) and is closed with plain fclose().
 *
 * The FILE* stream cannot expose the mapping to its caller (there is no
 * portable fgetcookie()), so a loader that wants the bytes addressable
 * uses mfile_map_file() instead.
 */
FILE *mfile_open(const char *path);

/*
 * Address the whole file.  With FFPIC_MMAP=1 the bytes are the file
 * mapping itself and are handed out without a copy; otherwise they are
 * read into a malloc'd block.  Both are released with mfile_unmap(), so
 * callers must not free "data" themselves.
 *
 * Returns 0 on success, -1 with errno set when the file cannot be read.
 */
struct mfile_buf {
    const uint8_t *data;
    size_t size;
    void *map;    /* internal: mapping base when mmap'd, NULL when malloc'd */
};

int mfile_map_file(const char *path, struct mfile_buf *out);
void mfile_unmap(struct mfile_buf *out);

#ifdef __cplusplus
}
#endif

#endif /*_MFILE_H_*/
