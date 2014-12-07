/**
 * Author: Jason White
 *
 * Description:
 * Mirrors the root file system within the FUSE file system. This is useful for
 * chroot-ing into the FUSE file system and intercepting various file
 * operations, such as read and write.
 *
 * Usage:
 * ./mirrorfs /tmp/mirrorfs -f
 */

#define FUSE_USE_VERSION 26

#define _GNU_SOURCE

#include <fuse.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <sys/ioctl.h>


/**
 * Checks if the file can be accessed with the given mode.
 */
static int mirror_access(const char *path, int mode)
{
    if (access(path, mode) == -1)
        return -errno;

    return 0;
}

/**
 * Gets file attributes.
 */
static int mirror_getattr(const char *path, struct stat *stbuf)
{
    if (stat(path, stbuf) == -1)
        return -errno;

    return 0;
}

/**
 * Gets attributes from an open file.
 */
static int mirror_fgetattr(const char *path, struct stat *stbuf,
        struct fuse_file_info *fi)
{
    if (fstat(fi->fh, stbuf) == -1)
        return -errno;

    return 0;
}

/**
 */
static int mirror_readlink(const char *path, char *buf, size_t bufsize)
{
    if (readlink(path, buf, bufsize) == -1)
        return -errno;

    return 0;
}

/**
 * Creates a file node.
 */
static int mirror_mknod(const char *path, mode_t mode, dev_t dev)
{
    if (mknod(path, mode, dev) == -1)
        return -errno;

    return 0;
}

/**
 * Creates a directory.
 */
static int mirror_mkdir(const char *path, mode_t mode)
{
    if (mkdir(path, mode) == -1)
        return -errno;

    return 0;
}

/**
 * Removes a file.
 */
static int mirror_unlink(const char *path)
{
    if (unlink(path) == -1)
        return -errno;

    return 0;
}

/**
 * Removes a directory.
 */
static int mirror_rmdir(const char *path)
{
    if (rmdir(path) == -1)
        return -errno;

    return 0;
}

/**
 * Creates a hard link.
 */
static int mirror_link(const char *oldpath, const char *newpath)
{
    if (link(oldpath, newpath) == -1)
        return -errno;

    return 0;
}

/**
 * Creates a symbolic link.
 */
static int mirror_symlink(const char *target, const char *linkpath)
{
    printf("Symlink '%s' -> '%s'", target, linkpath);

    if (symlink(target, linkpath) == -1)
        return -errno;

    return 0;
}

/**
 * Changes the name/location of a file.
 */
static int mirror_rename(const char *oldpath, const char *newpath)
{
    if (rename(oldpath, newpath) == -1)
        return -errno;

    return 0;
}

/**
 * Changes file permissions.
 */
static int mirror_chmod(const char *path, mode_t mode)
{
    if (chmod(path, mode) == -1)
        return -errno;

    return 0;
}

/**
 * Changes file ownership.
 */
static int mirror_chown(const char *path, uid_t user, gid_t group)
{
    if (chown(path, user, group) == -1)
        return -errno;

    return 0;
}

/**
 * Truncates a file to the specified length.
 */
static int mirror_truncate(const char *path, off_t length)
{
    if (truncate(path, length) == -1)
        return -errno;

    return 0;
}

/**
 * Truncates a file to the specified length.
 */
static int mirror_ftruncate(const char *path, off_t length,
        struct fuse_file_info *fi)
{
    if (ftruncate(fi->fh, length) == -1)
        return -errno;

    return 0;
}

/**
 */
static int mirror_opendir(const char *path, struct fuse_file_info *fi)
{
    DIR *dir = opendir(path);
    if (!dir)
        return -errno;

    fi->fh = (uint64_t)dir;
    return 0;
}

/**
 */
static int mirror_releasedir(const char *path, struct fuse_file_info *fi)
{
    if (closedir((DIR*)fi->fh) == -1)
        return -errno;

    return 0;
}

/**
 * Reads the contents of a directory. This function ignores the offset and reads
 * the entire directory at once.
 */
static int mirror_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi)
{
    DIR *dir;
    struct dirent* entry;

    dir = (DIR*)fi->fh;

    seekdir(dir, offset);

    while ((entry = readdir(dir)))
    {
        if (filler(buf, entry->d_name, NULL, entry->d_off))
            break;
    }

    return 0;
}

/**
 * File open operation.
 */
static int mirror_open(const char *path, struct fuse_file_info *fi)
{
    if ((fi->fh = open(path, fi->flags)) != 0)
        return -errno;

    return 0;
}

/**
 * Creates and opens a file.
 */
static int mirror_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    if ((fi->fh = creat(path, mode)) == -1)
        return -errno;

    return 0;
}

/**
 * Releases an open file. This is called for every open() operation. The return
 * value is ignored by fuse.
 */
static int mirror_release(const char *path, struct fuse_file_info *fi)
{
    close(fi->fh);
    return 0;
}

/**
 * Reads data from an open file.
 */
static int mirror_read(const char *path, char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi)
{
    ssize_t bytes_read = pread(fi->fh, buf, size, offset);
    if (bytes_read == -1)
        return 0;

    /* FIXME: Can this overflow an integer? */
    return (int)bytes_read;
}

/**
 * Writes to a file.
 */
static int mirror_write(const char *path, const char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi)
{
    ssize_t written;

    if ((written = pwrite(fi->fh, buf, size, offset)) == -1)
        return -errno;

    return (int)written;
}

/**
 * Allocates or deallocates disk space for the file.
 */
static int mirror_fallocate(const char *path, int mode, off_t offset, off_t len,
        struct fuse_file_info *fi)
{
    if (fallocate(fi->fh, mode, offset, len) == -1)
        return -errno;

    return 0;
}

/**
 * Locks or unlocks a file.
 */
static int mirror_lock(const char *path, struct fuse_file_info *fi, int cmd,
        struct flock *lock)
{
    if (fcntl(fi->fh, cmd, lock) == -1)
        return -errno;

    return 0;
}

/**
 * Locks or unlocks a file.
 */
static int mirror_flock(const char *path, struct fuse_file_info *fi, int op)
{
    if (flock(fi->fh, op) == -1)
        return -errno;

    return 0;
}

/**
 * Synchronize file contents with the disk.
 */
static int mirror_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
    int res;

    if (datasync)
        res = fdatasync(fi->fh);
    else
        res = fsync(fi->fh);

    if (res == -1)
        return -errno;

    return 0;
}

/**
 * Gets extended attributes.
 */
static int mirror_getxattr(const char *path, const char *name, char *value, size_t size)
{
    ssize_t valsize;
    if ((valsize = getxattr(path, name, value, size)) == -1)
        return -errno;

    return (int)valsize;
}

/**
 * Sets an extended attribute.
 */
static int mirror_setxattr(const char *path, const char *name,
        const char *value, size_t size, int flags)
{
    if (setxattr(path, name, value, size, flags) == -1)
        return -errno;

    return 0;
}

/**
 * Removes an extended attribute.
 */
static int mirror_removexattr(const char *path, const char *name)
{
    if (removexattr(path, name) == -1)
        return -errno;

    return 0;
}

/**
 * List extended attributes.
 */
static int mirror_listxattr(const char *path, char *list, size_t size)
{
    ssize_t listsize;
    if ((listsize = listxattr(path, list, size)) == -1)
        return -errno;

    return (int)listsize;
}

/**
 */
static int mirror_ioctl(const char *path, int request, void *arg,
        struct fuse_file_info *fi, unsigned int flags, void *data)
{
    if (ioctl(fi->fh, request, arg) == -1)
        return -errno;

    return 0;
}

/**
 */
/*static int mirror_poll(const char *path, struct fuse_file_info *fi, struct fuse_pollhandle *ph, unsigned *reventsp)
{
    return 0;
}*/

/**
 * Get file system statistics.
 */
static int mirror_statfs(const char *path, struct statvfs *buf)
{
    if (statvfs(path, buf) == -1)
        return -errno;

    return 0;
}

/**
 * Changes file timestamps with nanosecond precision.
 */
static int mirror_utimens(const char *path, const struct timespec tv[2])
{
    /* The file descriptor is ignored if path is absolute, which is always the
     * case in fuse. */
    if (utimensat(-1, path, tv, 0) == -1)
        return -errno;

    return 0;
}

static struct fuse_operations mirror_oper = {
    .flag_nopath = 1,
    .access      = mirror_access,
    .getattr     = mirror_getattr,
    .fgetattr    = mirror_fgetattr,
    .readlink    = mirror_readlink,
    .mknod       = mirror_mknod,
    .mkdir       = mirror_mkdir,
    .unlink      = mirror_unlink,
    .rmdir       = mirror_rmdir,
    .link        = mirror_link,
    .symlink     = mirror_symlink,
    .rename      = mirror_rename,
    .chmod       = mirror_chmod,
    .chown       = mirror_chown,
    .truncate    = mirror_truncate,
    .ftruncate   = mirror_ftruncate,
    .opendir     = mirror_opendir,
    .releasedir  = mirror_releasedir,
    .readdir     = mirror_readdir,
    .open        = mirror_open,
    .create      = mirror_create,
    .release     = mirror_release,
    .read        = mirror_read,
    .write       = mirror_write,
    .fallocate   = mirror_fallocate,
    .lock        = mirror_lock,
    .flock       = mirror_flock,
    .fsync       = mirror_fsync,
    .getxattr    = mirror_getxattr,
    .setxattr    = mirror_setxattr,
    .removexattr = mirror_removexattr,
    .listxattr   = mirror_listxattr,
    .ioctl       = mirror_ioctl,
    .statfs      = mirror_statfs,
    .utimens     = mirror_utimens
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &mirror_oper, NULL);
}
