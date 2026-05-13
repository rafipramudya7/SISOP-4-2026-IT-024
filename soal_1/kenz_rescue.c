#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

static char source_dir[PATH_MAX];

static void fullpath(char out[PATH_MAX], const char *fuse_path)
{
    snprintf(out, PATH_MAX, "%s%s", source_dir, fuse_path);
}

static char *build_tujuan(size_t *out_len)
{
    const char *prefix = "Tujuan Mas Amba: ";
    char fragments[4096] = {0};
    size_t frag_pos = 0;

    for (int i = 1; i <= 7; i++)
    {
        char path[PATH_MAX * 2];
        snprintf(path, sizeof(path), "%s/%d.txt", source_dir, i);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;

        char line[1024];
        while (fgets(line, sizeof(line), f))
        {
            if (strncmp(line, "KOORD: ", 7) == 0)
            {
                char *val = line + 7;
                size_t vlen = strlen(val);
                while (vlen > 0 &&
                       (val[vlen - 1] == '\n' || val[vlen - 1] == '\r'))
                    vlen--;

                if (frag_pos + vlen + 1 < sizeof(fragments))
                {
                    memcpy(fragments + frag_pos, val, vlen);
                    frag_pos += vlen;
                }
                break;
            }
        }
        fclose(f);
    }
    fragments[frag_pos] = '\0';

    size_t total = strlen(prefix) + frag_pos + 1 + 1;
    char *result = malloc(total);
    if (!result)
        return NULL;

    int written = snprintf(result, total, "%s%s\n", prefix, fragments);
    *out_len = (size_t)written;
    return result;
}

static int kenz_getattr(const char *path, struct stat *st,
                        struct fuse_file_info *fi)
{
    (void)fi;
    memset(st, 0, sizeof(*st));

    if (strcmp(path, "/tujuan.txt") == 0)
    {
        size_t len;
        char *content = build_tujuan(&len);
        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 1;
        st->st_size = (off_t)len;
        free(content);
        return 0;
    }

    char fpath[PATH_MAX];
    fullpath(fpath, path);

    if (lstat(fpath, st) == -1)
        return -errno;
    return 0;
}

static int kenz_readdir(const char *path, void *buf,
                        fuse_fill_dir_t filler, off_t offset,
                        struct fuse_file_info *fi,
                        enum fuse_readdir_flags flags)
{
    (void)offset;
    (void)fi;
    (void)flags;

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    DIR *dp = opendir(source_dir);
    if (!dp)
        return -errno;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL)
    {
        if (de->d_name[0] == '.')
            continue;
        filler(buf, de->d_name, NULL, 0, 0);
    }
    closedir(dp);

    filler(buf, "tujuan.txt", NULL, 0, 0);

    return 0;
}

static int kenz_open(const char *path, struct fuse_file_info *fi)
{
    if (strcmp(path, "/tujuan.txt") == 0)
    {
        if ((fi->flags & O_ACCMODE) != O_RDONLY)
            return -EACCES;
        return 0;
    }

    char fpath[PATH_MAX];
    fullpath(fpath, path);

    int fd = open(fpath, fi->flags);
    if (fd == -1)
        return -errno;

    close(fd);
    return 0;
}

static int kenz_read(const char *path, char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    (void)fi;

    if (strcmp(path, "/tujuan.txt") == 0)
    {
        size_t len;
        char *content = build_tujuan(&len);
        if (!content)
            return -ENOMEM;

        int bytes_read = 0;
        if ((size_t)offset < len)
        {
            size_t available = len - (size_t)offset;
            bytes_read = (int)(available < size ? available : size);
            memcpy(buf, content + offset, bytes_read);
        }
        free(content);
        return bytes_read;
    }

    char fpath[PATH_MAX];
    fullpath(fpath, path);

    int fd = open(fpath, O_RDONLY);
    if (fd == -1)
        return -errno;

    int res = (int)pread(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    close(fd);
    return res;
}

static const struct fuse_operations kenz_ops = {
    .getattr = kenz_getattr,
    .readdir = kenz_readdir,
    .open = kenz_open,
    .read = kenz_read,
};

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr,
                "Usage: %s <source_directory> <mount_directory> [fuse_options]\n",
                argv[0]);
        return 1;
    }

    if (!realpath(argv[1], source_dir))
    {
        perror("realpath(source_directory)");
        return 1;
    }

    int fuse_argc = argc - 1;
    char **fuse_argv = malloc((fuse_argc + 1) * sizeof(char *));
    if (!fuse_argv)
    {
        perror("malloc");
        return 1;
    }

    fuse_argv[0] = argv[0];
    for (int i = 1; i < fuse_argc; i++)
        fuse_argv[i] = argv[i + 1];
    fuse_argv[fuse_argc] = NULL;

    printf("kenz_rescue: source='%s'  mount='%s'\n", source_dir, fuse_argv[1]);
    printf("Hint: to unmount later run:  fusermount3 -u %s\n", fuse_argv[1]);

    int ret = fuse_main(fuse_argc, fuse_argv, &kenz_ops, NULL);
    free(fuse_argv);
    return ret;
}