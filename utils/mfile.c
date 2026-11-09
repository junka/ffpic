#define _DARWIN_C_SOURCE 1
#define _GNU_SOURCE 1

#include "mfile.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifndef EIO
#define EIO 5           /* not every platform's errno.h defines it */
#endif

/* mmap I/O is opt-in: it only pays off where read() is expensive relative
 * to a page fault (measured on macOS/APFS it is a wash), so the default
 * stream is plain fopen().  Set FFPIC_MMAP=1 to route reads through mmap. */
static int
mf_use_mmap(void)
{
    static int use_mmap = -1;
    if (use_mmap < 0) {
        const char *e = getenv("FFPIC_MMAP");
        use_mmap = (e != NULL && e[0] != '\0' && e[0] != '0');
    }
    return use_mmap;
}

/* Read the whole file into a malloc'd block: the copy path every platform
 * and every mmap bail-out falls back to. */
static int
mf_copy_file(const char *path, struct mfile_buf *out)
{
    FILE *f = fopen(path, "rb");
    long size;
    void *block;

    out->data = NULL;
    out->size = 0;
    out->map = NULL;

    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0) {
        fclose(f);
        errno = EIO;
        return -1;
    }
    block = malloc(size ? (size_t)size : 1);
    if (!block) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0 ||
        fread(block, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(block);
        errno = EIO;
        return -1;
    }
    fclose(f);
    out->data = block;
    out->size = (size_t)size;
    return 0;
}

void
mfile_unmap(struct mfile_buf *out)
{
#ifndef _WIN32
    if (out->map) {
        munmap(out->map, out->size);
        out->map = NULL;
    } else
#endif
        free((void *)out->data);
    out->data = NULL;
    out->size = 0;
}

#ifndef _WIN32
/* 64 KiB of stdio buffering on top of the mapping keeps the per-call
 * overhead of the FILE layer negligible without pinning much memory. */
#define MFILE_BUFSZ (64 * 1024)

struct mfile_cookie {
    void *base;
    size_t size;
    size_t off;
};

#if defined(__APPLE__)
#define MF_READ_RET int
#define MF_READ_LEN int len
#define MF_SEEK_RET fpos_t
#define MF_SEEK_ARG fpos_t off
#elif defined(__GLIBC__)
#define MF_READ_RET ssize_t
#define MF_READ_LEN size_t len
#define MF_SEEK_RET int
#define MF_SEEK_ARG off64_t *off
#else
#define MF_READ_RET ssize_t
#define MF_READ_LEN size_t len
#define MF_SEEK_RET int
#define MF_SEEK_ARG off_t *off
#endif

static MF_READ_RET mf_read(void *cookie, char *buf, MF_READ_LEN)
{
    struct mfile_cookie *ck = cookie;
    size_t avail;

    if (ck->off >= ck->size)
        return 0;
    avail = ck->size - ck->off;
    if ((size_t)len < avail)
        avail = len;
    memcpy(buf, (const char *)ck->base + ck->off, avail);
    ck->off += avail;
    return (MF_READ_RET)avail;
}

static MF_SEEK_RET mf_seek(void *cookie, MF_SEEK_ARG, int whence)
{
    struct mfile_cookie *ck = cookie;
    int64_t pos;

    switch (whence) {
    case SEEK_SET:
        pos = (int64_t)off;
        break;
    case SEEK_CUR:
        pos = (int64_t)ck->off + (int64_t)off;
        break;
    case SEEK_END:
        pos = (int64_t)ck->size + (int64_t)off;
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    /* seeking past EOF is allowed, like a real file */
    if (pos < 0) {
        errno = EINVAL;
        return -1;
    }
    ck->off = (size_t)pos;
#if defined(__APPLE__)
    return (fpos_t)ck->off;
#elif defined(__GLIBC__)
    *off = (off64_t)ck->off;
    return 0;
#else
    *off = (off_t)ck->off;
    return 0;
#endif
}

static int mf_close(void *cookie)
{
    struct mfile_cookie *ck = cookie;

    munmap(ck->base, ck->size);
    free(ck);
    return 0;
}

int
mfile_map_file(const char *path, struct mfile_buf *out)
{
    struct stat st;
    void *base;
    int fd;

    out->data = NULL;
    out->size = 0;
    out->map = NULL;

    if (!mf_use_mmap())
        return mf_copy_file(path, out);

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        close(fd);
        return mf_copy_file(path, out);
    }
    base = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (base == MAP_FAILED)
        return mf_copy_file(path, out);

    out->data = base;
    out->size = (size_t)st.st_size;
    out->map = base;
    return 0;
}

FILE *mfile_open(const char *path)
{
    struct mfile_cookie *ck;
    struct stat st;
    void *base;
    FILE *f;
    int fd;

    if (!mf_use_mmap())
        return fopen(path, "rb");

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return NULL;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        /* not mappable: hand out a plain buffered stream on the same fd */
        f = fdopen(fd, "rb");
        if (!f)
            close(fd);
        return f;
    }

    base = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (base == MAP_FAILED) {
        f = fdopen(fd, "rb");
        if (!f)
            close(fd);
        return f;
    }
    close(fd);

    ck = malloc(sizeof(*ck));
    if (!ck) {
        munmap(base, (size_t)st.st_size);
        return NULL;
    }
    ck->base = base;
    ck->size = (size_t)st.st_size;
    ck->off = 0;

#if defined(__APPLE__)
    f = funopen(ck, mf_read, NULL, mf_seek, mf_close);
#else
    {
        cookie_io_functions_t io = {0};
        io.read = mf_read;
        io.seek = mf_seek;
        io.close = mf_close;
        f = fopencookie(ck, "r", io);
    }
#endif
    if (!f) {
        munmap(base, ck->size);
        free(ck);
        return NULL;
    }
    setvbuf(f, NULL, _IOFBF, MFILE_BUFSZ);
    return f;
}

#else /* _WIN32 */

FILE *mfile_open(const char *path)
{
    return fopen(path, "rb");
}

/* no mapping here, so whole-file access always takes the copy path */
int
mfile_map_file(const char *path, struct mfile_buf *out)
{
    out->data = NULL;
    out->size = 0;
    out->map = NULL;
    return mf_copy_file(path, out);
}

#endif
