// SPDX-License-Identifier: GPL-3.0-only
// port/include/linux/direct.h: MSVC directory functions the game uses. Linux only.
#ifndef PORT_DIRECT_H
#define PORT_DIRECT_H
#ifdef __cplusplus
extern "C" {
#endif
int _mkdir(const char *path);
int _chdir(const char *path);
char *_getcwd(char *buffer, int maxlen);
#ifdef __cplusplus
}
#endif
#endif
