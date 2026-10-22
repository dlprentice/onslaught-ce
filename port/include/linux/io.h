// SPDX-License-Identifier: GPL-3.0-only
// port/include/linux/io.h: the MSVC low-level I/O names the game uses, over POSIX (paths go through port.h's
// path layer). Linux only.
#ifndef PORT_IO_H
#define PORT_IO_H
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
struct _finddata_t
{
	unsigned attrib;
	long time_create;
	long time_access;
	long time_write;
	unsigned long size;
	char name[260];
};
#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_SUBDIR 0x10
#define _A_ARCH 0x20
#ifdef __cplusplus
extern "C" {
#endif
long _findfirst(const char *filespec, struct _finddata_t *fileinfo);
int _findnext(long handle, struct _finddata_t *fileinfo);
int _findclose(long handle);
int _access(const char *path, int mode);
#ifdef __cplusplus
}
#endif
#endif
