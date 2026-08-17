#ifndef PLATFORM_FS_H
#define PLATFORM_FS_H

#include <stdbool.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/types.h>

int platform_fs_open(const char *path, int flags);
ssize_t platform_fs_read(int fd, void *buffer, size_t size);
off_t platform_fs_seek(int fd, off_t offset, int whence);
int platform_fs_size(int fd);
int platform_fs_close(int fd);
bool platform_fs_init(void);

#endif
