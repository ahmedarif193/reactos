#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE 1
#define _LARGEFILE64_SOURCE 1

#include "mkntfsimg.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

int g_mkntfsimg_verbose = 0;

NTSTATUS
ntfsimg_io_open(NTFSIMG_IO *io, const char *path)
{
    io->fd = open(path, O_RDWR | O_CLOEXEC);
    if (io->fd < 0)
    {
        fprintf(stderr, "mkntfsimg: open('%s'): %s\n", path, strerror(errno));
        return STATUS_NO_SUCH_FILE;
    }
    io->sector_size = 512;
    return STATUS_SUCCESS;
}

void
ntfsimg_io_close(NTFSIMG_IO *io)
{
    if (io->fd >= 0)
    {
        close(io->fd);
        io->fd = -1;
    }
}

NTSTATUS
ntfsimg_io_read(NTFSIMG_IO *io, uint64_t byte_offset, uint32_t length, void *buf)
{
    uint8_t *dst = (uint8_t*)buf;
    uint32_t remaining = length;
    off_t off = (off_t)byte_offset;

    while (remaining > 0)
    {
        ssize_t got = pread(io->fd, dst, remaining, off);
        if (got < 0)
        {
            if (errno == EINTR) continue;
            fprintf(stderr, "mkntfsimg: pread@%llu len=%u: %s\n",
                    (unsigned long long)byte_offset, length, strerror(errno));
            return STATUS_UNSUCCESSFUL;
        }
        if (got == 0)
            return STATUS_END_OF_FILE;
        dst += got;
        off += got;
        remaining -= (uint32_t)got;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ntfsimg_io_write(NTFSIMG_IO *io, uint64_t byte_offset, uint32_t length, const void *buf)
{
    const uint8_t *src = (const uint8_t*)buf;
    uint32_t remaining = length;
    off_t off = (off_t)byte_offset;

    while (remaining > 0)
    {
        ssize_t put = pwrite(io->fd, src, remaining, off);
        if (put < 0)
        {
            if (errno == EINTR) continue;
            fprintf(stderr, "mkntfsimg: pwrite@%llu len=%u: %s\n",
                    (unsigned long long)byte_offset, length, strerror(errno));
            return STATUS_UNSUCCESSFUL;
        }
        if (put == 0)
            return STATUS_DISK_FULL;
        src += put;
        off += put;
        remaining -= (uint32_t)put;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ntfsimg_io_flush(NTFSIMG_IO *io)
{
    if (fsync(io->fd) != 0)
        return STATUS_UNSUCCESSFUL;
    return STATUS_SUCCESS;
}
