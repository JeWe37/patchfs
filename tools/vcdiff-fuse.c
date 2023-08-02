/*
 * fuseflect - A FUSE filesystem for local directory mirroring
 *
 * Copyright (c) 2007 Theodoros V. Kalamatianos <nyb@users.sourceforge.net>
 * Copyright (c) 2023 Jendrik Weise <jewe37@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/limits.h>
#define DEBUG        0

#define _GNU_SOURCE

#define FUSE_USE_VERSION 26

#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <search.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <fuse.h>

#include "vcdiff_incremental.h"


static const char* patchFsVersion = "2023.08.01";

#if DEBUG
#define DBGMSG(M, ...)    fprintf(stderr, "%s:%s:%i " M "\n", __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__); fflush(stderr);
#else
#define DBGMSG(M, ...)
#endif

static char *src = NULL;
static int src_len;
static char *base = NULL;
static int base_len;

#define SRC(p)        char __##p [PATH_MAX + 1]; \
            strncpy(__##p , src, PATH_MAX); \
            strncat(__##p , p, PATH_MAX - src_len); \
                        p = __##p ;


#define TRY(x, s, f, r) int (r) = (x); if ((r) >= 0) { s; } else { int __e = -errno; f; return __e; }
#define RET(x, s, f)    TRY(x, s, f, __r); return 0;


#define IDRET(x, s, f)    struct fuse_context *ctx = fuse_get_context(); \
            int ouid __attribute__ ((unused)) = setfsuid(ctx->uid); \
            int ogid __attribute__ ((unused)) = setfsgid(ctx->gid); \
            RET(x, \
                s; setfsuid(ouid); setfsgid(ogid), \
                f; setfsuid(ouid); setfsgid(ogid) \
            )

struct patch_handle {
    int fd_source, fd_raw;
    struct target_stream target;
    struct source_stream source;
};

static int correct_stat_size(const char* path, struct stat *stbuf) {
    char src_size[32];
    int length = getxattr(path, "user.diff_src_size", src_size, 31);
    if (length < 0) {
        if (errno == ENODATA)
            return 0;
        return -errno;
    }
    stbuf->st_size = strtoul(src_size, NULL, 10);
    return 0;
}

static int patchfs_getattr(const char *path, struct stat *stbuf) {
    SRC(path)
    RET(lstat(path, stbuf),TRY(correct_stat_size(path, stbuf),,,r2),)
}

static int patchfs_access(const char *path, int mask) {
    SRC(path)
    RET(access(path, mask),,)
}

static int patchfs_readlink(const char *path, char *buf, size_t size) {
    SRC(path)
    RET(readlink(path, buf, size - 1), buf[__r] = '\0',)
}

static int patchfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    (void)offset;
    (void)fi;

    DIR *dp;
    struct dirent *de;

    SRC(path)

    dp = opendir(path);
    if (dp == NULL)
        return -errno;

    while ((de = readdir(dp)) != NULL) {
            struct stat st;
            memset(&st, 0, sizeof(st));
            st.st_ino = de->d_ino;
            st.st_mode = de->d_type << 12;
            if (filler(buf, de->d_name, &st, 0))
                break;
    }

    closedir(dp);
    return 0;
}

static int patchfs_mknod(const char *path, mode_t mode, dev_t rdev) {
    (void)path;
    (void)mode;
    (void)rdev;
    return -EROFS;
}

static int patchfs_mkdir(const char *path, mode_t mode) {
    (void)path;
    (void)mode;
    return -EROFS;
}

static int patchfs_unlink(const char *path) {
    (void)path;
    return -EROFS;
}

static int patchfs_rmdir(const char *path) {
    (void)path;
    return -EROFS;
}

static int patchfs_symlink(const char *from, const char *to) {
    (void)from;
    (void)to;
    return -EROFS;
}

static int patchfs_rename(const char *from, const char *to) {
    (void)from;
    (void)to;
    return -EROFS;
}

static int patchfs_link(const char *from, const char *to) {
    (void)from;
    (void)to;
    return -EROFS;
}

static int patchfs_chmod(const char *path, mode_t mode) {
    (void)path;
    (void)mode;
    return -EROFS;
}

static int patchfs_chown(const char *path, uid_t uid, gid_t gid) {
    (void)path;
    (void)uid;
    (void)gid;
    return -EROFS;
}

static int patchfs_truncate(const char *path, off_t size) {
    (void)path;
    (void)size;
    return -EROFS;
}

static int patchfs_utimens(const char *path, const struct timespec ts[2]) {
    (void)path;
    (void)ts;
    return -EROFS;
}

static int patchfs_open(const char *path, struct fuse_file_info *fi) {
    SRC(path)
    struct patch_handle *handle = malloc(sizeof(struct patch_handle));
    int fd_delta = open(path, O_RDONLY);
    if (fd_delta < 0) {
        free(handle);
        return -errno;
    }

    char base_path[PATH_MAX + 1];
    strncpy(base_path, base, PATH_MAX);
    int length = fgetxattr(fd_delta, "user.diff_src", base_path+base_len, PATH_MAX-base_len);
    if (length < 0) {
        if (errno == ENODATA) {
            handle->fd_source = -1;
            handle->fd_raw = fd_delta;
            fi->fh = (uint64_t) handle;
            return 0;
        }
        return -errno;
    }
    base_path[base_len+length] = '\0';

    handle->fd_source = open(base_path, O_RDONLY);
    if (handle->fd_source < 0) {
        close(fd_delta);
        free(handle);
        return -errno;
    }

    int rc = load_diff(&handle->target, &handle->source, handle->fd_source, fd_delta);
    if (rc < 0) {
        close(fd_delta);
        close(handle->fd_source);
        free(handle);
        return rc;
    }

    rc = close(fd_delta);
    if (rc < 0) {
        free_data(&handle->target, &handle->source);
        close(handle->fd_source);
        free(handle);
        return -errno;
    }

    handle->fd_raw = -1;
    fi->fh = (uint64_t) handle;
    // can return 0 here because we do not need the fd anymore
    return 0;
}

static int patchfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)path;
    struct patch_handle *handle = (struct patch_handle *) fi->fh;

    if (handle->fd_source < 0) {
        RET(pread(handle->fd_raw, buf, size, offset),return __r,)
    }

    return read_range(&handle->target, offset, size, (uint8_t*)buf);
}

static int patchfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)path;
    (void)buf;
    (void)size;
    (void)offset;
    (void)fi;
    return -EROFS;
}

static int patchfs_release(const char *path, struct fuse_file_info *finfo) {
    (void)path;
    struct patch_handle *handle = (struct patch_handle *) finfo->fh;
    int rc;

    if (handle->fd_source < 0) {
        rc = close(handle->fd_raw);
        if (rc < 0)
            return -errno;
    } else {
        rc = free_data(&handle->target, &handle->source);
        if (rc < 0)
            return -errno;
        rc = close(handle->fd_source);
        if (rc < 0)
            return -errno;
    }
    free(handle);

    return 0;
}

static int patchfs_statfs(const char *path, struct statvfs *stbuf) {
    SRC(path)
    RET(statvfs(path, stbuf),,)
}

static int patchfs_setxattr(const char *path, const char *name, const char *value, size_t size, int flags) {
    (void)path;
    (void)name;
    (void)value;
    (void)size;
    (void)flags;
    return -EROFS;
}

static int patchfs_getxattr(const char *path, const char *name, char *value, size_t size) {
    SRC(path)
    RET(lgetxattr(path, name, value, size),,)
}

static int patchfs_listxattr(const char *path, char *list, size_t size) {
    SRC(path)
    RET(llistxattr(path, list, size),,)
}

static int patchfs_removexattr(const char *path, const char *name) {
    (void)path;
    (void)name;
    return -EROFS;
}

#define OP(x)        . x = patchfs_##x ,

static struct fuse_operations patchfs_oper = {
    OP(getattr)
    OP(access)
    OP(readlink)
    OP(readdir)
    OP(mknod)
    OP(mkdir)
    OP(symlink)
    OP(unlink)
    OP(rmdir)
    OP(rename)
    OP(link)
    OP(chmod)
    OP(chown)
    OP(truncate)
    OP(utimens)
    OP(open)
    OP(read)
    OP(write)
    OP(release)
    OP(statfs)
    OP(setxattr)
    OP(getxattr)
    OP(listxattr)
    OP(removexattr)
};

enum {
    KEY_HELP,
    KEY_VERSION,
};

static void usage(const char* progname) {
    fprintf(stdout,
"usage: %s readwritepath mountpoint [options]\n"
"\n"
"   Mounts readwritepath as a read-only mount at mountpoint\n"
"   Applies patches based on user.diff_src xattr\n"
"\n"
"general options:\n"
"   -o base=source,[opt...]     mount options\n"
"   -h  --help                 print help\n"
"   -V  --version              print version\n"
"\n", progname);
}

static int patchfs_parse_opt(void *data, const char *arg, int key, struct fuse_args *outargs) {
    (void) data;

    switch (key)
    {
        case FUSE_OPT_KEY_NONOPT:
            if (src == 0) {
                src = strdup(arg);
                src_len = strlen(src);
                return 0;
            } else
                return 1;
        case FUSE_OPT_KEY_OPT:
            return 1;
        case KEY_HELP:
            usage(outargs->argv[0]);
            exit(0);
        case KEY_VERSION:
            fprintf(stdout, "patchFs version %s\n", patchFsVersion);
            exit(0);
        default:
            fprintf(stderr, "see `%s -h' for usage\n", outargs->argv[0]);
            exit(1);
    }
    return 1;
}

static struct fuse_opt patchfs_opts[] = {
    FUSE_OPT_KEY("-h",          KEY_HELP),
    FUSE_OPT_KEY("--help",      KEY_HELP),
    FUSE_OPT_KEY("-V",          KEY_VERSION),
    FUSE_OPT_KEY("--version",   KEY_VERSION),
    { "base=%s", 0, 0 },
    FUSE_OPT_END
};

int main(int argc, char *argv[]) {
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    int res;

    res = fuse_opt_parse(&args, &base, patchfs_opts, patchfs_parse_opt);
    if (res != 0) {
        fprintf(stderr, "Invalid arguments\n");
        fprintf(stderr, "see `%s -h' for usage\n", argv[0]);
        exit(1);
    }
    if (src == 0) {
        fprintf(stderr, "Missing readwritepath\n");
        fprintf(stderr, "see `%s -h' for usage\n", argv[0]);
        exit(1);
    }
    if (base == 0) {
        fprintf(stderr, "Missing basedir\n");
        fprintf(stderr, "see `%s -h' for usage\n", argv[0]);
        exit(1);
    }
    base_len = strlen(base);
    if (base[base_len-1] != '/') {
        if (base_len >= PATH_MAX) {
            fprintf(stderr, "basedir too long\n");
            exit(1);
        }
        base[base_len] = '/';
        base[base_len+1] = '\0';
        base_len++;
    }

    fuse_main(args.argc, args.argv, &patchfs_oper, NULL);

    return 0;
}
