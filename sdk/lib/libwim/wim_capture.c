/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * wim_capture.c - Directory tree walker for WIM capture (pure C)
 *
 */

#define _POSIX_C_SOURCE 200809L

#include "wim_capture.h"
#include "wim_io.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#else
#include <io.h>
#endif

#ifdef _WIN32
typedef struct _stat64 capture_stat_t;
typedef __int64 capture_off_t;
typedef int capture_read_t;

static unsigned int capture_read_size(size_t size)
{
    return (size > UINT_MAX) ? UINT_MAX : (unsigned int)size;
}

#define capture_lstat(path, st) _stat64((path), (st))
#define capture_stat(path, st) _stat64((path), (st))
#define capture_open(path) _open((path), _O_RDONLY | _O_BINARY)
#define capture_read(fd, buffer, size) _read((fd), (buffer), capture_read_size(size))
#define capture_close(fd) _close(fd)
#define capture_is_dir(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#define capture_is_reg(mode) (((mode) & _S_IFMT) == _S_IFREG)
#define capture_strdup(string) _strdup(string)
#else
typedef struct stat capture_stat_t;
typedef off_t capture_off_t;
typedef ssize_t capture_read_t;

#define capture_lstat(path, st) lstat((path), (st))
#define capture_stat(path, st) stat((path), (st))
#define capture_open(path) open((path), O_RDONLY)
#define capture_read(fd, buffer, size) read((fd), (buffer), (size))
#define capture_close(fd) close(fd)
#define capture_is_dir(mode) S_ISDIR(mode)
#define capture_is_reg(mode) S_ISREG(mode)
#define capture_strdup(string) strdup(string)
#endif

#define MMAP_CAPTURE_THRESHOLD (1u << 20)

/* Buffer deleters for the ownership-transfer callback. */
#ifndef _WIN32
static void cap_free_munmap(void* p, size_t sz)
{
    munmap(p, sz);
}
#endif

static void cap_free_plain(void* p, size_t sz)
{
    (void)sz;
    free(p);
}

/* Simple qsort comparator for WimDentry by name */
static int dentry_name_cmp(const void* a, const void* b)
{
    const WimDentry* da = (const WimDentry*)a;
    const WimDentry* db = (const WimDentry*)b;
    if (!da->name_utf8 && !db->name_utf8) return 0;
    if (!da->name_utf8) return -1;
    if (!db->name_utf8) return 1;
    return strcmp(da->name_utf8, db->name_utf8);
}

static int capture_regular_file(const char* full_path, capture_off_t file_size,
                                WimDentry* dentry, wim_blob_writer_fn writer, void* user)
{
    int fd;
    int ret;

    if (file_size <= 0)
        return 0;
    if ((uint64_t)file_size > SIZE_MAX)
        return -1;

    fd = capture_open(full_path);
    if (fd < 0) {
        fprintf(stderr, "Warning: Cannot read '%s', skipping\n", full_path);
        return 0;
    }

#ifndef _WIN32
    if ((uint64_t)file_size >= MMAP_CAPTURE_THRESHOLD) {
        void* map = mmap(NULL, (size_t)file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        capture_close(fd); /* mmap retains its own reference, safe to close fd now */
        if (map != MAP_FAILED) {
#ifdef POSIX_MADV_SEQUENTIAL
            (void)posix_madvise(map, (size_t)file_size, POSIX_MADV_SEQUENTIAL);
#endif
            ret = writer((uint8_t*)map, (uint64_t)file_size,
                         cap_free_munmap, map, dentry->sha1, user);
            if (ret != 0)
                cap_free_munmap(map, (size_t)file_size);
            return ret;
        }
        /* mmap failed; fall back to the read path. */
        fd = capture_open(full_path);
        if (fd < 0) {
            fprintf(stderr, "Warning: Cannot re-open '%s' after mmap failure\n", full_path);
            return 0;
        }
    }
#endif

    {
        uint8_t* data = (uint8_t*)malloc((size_t)file_size);
        size_t total = 0;

        if (!data) {
            capture_close(fd);
            return -1;
        }

        while (total < (size_t)file_size) {
            capture_read_t nread = capture_read(fd, data + total, (size_t)file_size - total);
            if (nread <= 0)
                break;
            total += (size_t)nread;
        }
        capture_close(fd);

        if (total != (size_t)file_size) {
            fprintf(stderr, "Warning: Short read on '%s'\n", full_path);
            dentry->file_size = total;
        }

        ret = writer(data, (uint64_t)total,
                     cap_free_plain, data, dentry->sha1, user);
        if (ret != 0)
            cap_free_plain(data, (size_t)total);
        return ret;
    }
}

static int capture_recursive(const char* full_path, const char* name,
                             WimDentry* dentry, wim_blob_writer_fn writer, void* user)
{
    capture_stat_t st;
    if (capture_lstat(full_path, &st) != 0) {
        fprintf(stderr, "Warning: Cannot stat '%s', skipping\n", full_path);
        return 0;
    }

    wim_dentry_init(dentry);
    dentry->name_utf8 = capture_strdup(name);
    utf8_to_utf16le(name, &dentry->name_utf16, &dentry->name_utf16_len);
    dentry->creation_time = unix_to_filetime(st.st_ctime);
    dentry->last_access_time = unix_to_filetime(st.st_atime);
    dentry->last_write_time = unix_to_filetime(st.st_mtime);

    if (capture_is_dir(st.st_mode)) {
        dentry->attributes = WIM_FILE_ATTRIBUTE_DIRECTORY;

        DIR* dir = opendir(full_path);
        if (!dir) {
            fprintf(stderr, "Warning: Cannot open directory '%s', skipping\n", full_path);
            return 0;
        }

        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;

            WimDentry child;
            wim_dentry_init(&child);
            size_t path_len = strlen(full_path) + 1 + strlen(ent->d_name) + 1;
            char* child_path = (char*)malloc(path_len);
            if (!child_path) {
                closedir(dir);
                return -1;
            }
            snprintf(child_path, path_len, "%s/%s", full_path, ent->d_name);

            int ret = capture_recursive(child_path, ent->d_name, &child, writer, user);
            free(child_path);
            if (ret != 0) {
                wim_dentry_free(&child);
                closedir(dir);
                return ret;
            }
            if (wim_dentry_add_child(dentry, child) != 0) {
                wim_dentry_free(&child);
                closedir(dir);
                return -1;
            }
        }
        closedir(dir);

        /* Sort children by name */
        if (dentry->child_count > 1) {
            qsort(dentry->children, dentry->child_count,
                  sizeof(WimDentry), dentry_name_cmp);
        }
    } else if (capture_is_reg(st.st_mode)) {
        dentry->attributes = WIM_FILE_ATTRIBUTE_ARCHIVE;
        dentry->file_size = (uint64_t)st.st_size;

        if (st.st_size > 0) {
            int ret = capture_regular_file(full_path, st.st_size, dentry, writer, user);
            if (ret != 0)
                return ret;
        }
        /* else: zero-size file, sha1 stays all-zeros */
    } else {
        /* Symlinks and other special files: skip */
        dentry->attributes = WIM_FILE_ATTRIBUTE_NORMAL;
    }

    return 0;
}

int wim_capture_dir(const char* source_dir, WimDentry* root,
                    wim_blob_writer_fn writer, void* user)
{
    capture_stat_t st;
    if (capture_stat(source_dir, &st) != 0) {
        fprintf(stderr, "Error: Cannot stat '%s'\n", source_dir);
        return -1;
    }

    /* Init root as directory */
    wim_dentry_init(root);
    root->attributes = WIM_FILE_ATTRIBUTE_DIRECTORY;
    root->creation_time = unix_to_filetime(st.st_ctime);
    root->last_access_time = unix_to_filetime(st.st_atime);
    root->last_write_time = unix_to_filetime(st.st_mtime);
    /* Root has empty name */

    /* Enumerate children */
    DIR* dir = opendir(source_dir);
    if (!dir) {
        fprintf(stderr, "Error: Cannot open directory '%s'\n", source_dir);
        return -1;
    }

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        WimDentry child;
        wim_dentry_init(&child);
        size_t path_len = strlen(source_dir) + 1 + strlen(ent->d_name) + 1;
        char* full_path = (char*)malloc(path_len);
        if (!full_path) {
            closedir(dir);
            return -1;
        }
        snprintf(full_path, path_len, "%s/%s", source_dir, ent->d_name);

        int ret = capture_recursive(full_path, ent->d_name, &child, writer, user);
        free(full_path);
        if (ret != 0) {
            wim_dentry_free(&child);
            closedir(dir);
            return ret;
        }
        if (wim_dentry_add_child(root, child) != 0) {
            wim_dentry_free(&child);
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);

    /* Sort children by name */
    if (root->child_count > 1) {
        qsort(root->children, root->child_count,
              sizeof(WimDentry), dentry_name_cmp);
    }

    return 0;
}
