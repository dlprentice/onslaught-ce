/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual launcher functions with a frozen clock and narrowly injected filesystem failures. All paths are fixtures. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <signal.h>
#include <sys/prctl.h>
#endif

static int fixture_race, fixture_collisions, fixture_fail_flush, fixture_fail_close, fixture_fail_fdopen;
static FILE *fixture_metadata;
static char fixture_collision[4096];
static time_t fixture_time(time_t *value) { time_t now = 1700000000; if (value) *value = now; return now; }
static int fixture_open(const char *path, int flags, ...);
static FILE *fixture_fdopen(int fd, const char *mode);
static int fixture_fflush(FILE *f);
static int fixture_fclose(FILE *f);
static FILE *fixture_fopen(const char *path, const char *mode);
static int unused_launcher_main(int argc, char **argv);
#ifndef _WIN32
static int fixture_fail_pipe;
static int fixture_pipe(int fd[2]);
#define pipe fixture_pipe
#endif
#define time fixture_time
#ifdef _WIN32
#define _open fixture_open
#define _fdopen fixture_fdopen
#else
#define open fixture_open
#define fdopen fixture_fdopen
#endif
#define fflush fixture_fflush
#define fclose fixture_fclose
#define fopen fixture_fopen
#define main unused_launcher_main
#ifndef CE_LAUNCH_SOURCE
#define CE_LAUNCH_SOURCE "../ce_launch.c"
#endif
#include CE_LAUNCH_SOURCE
#undef main
#undef time
#undef open
#undef _open
#undef fdopen
#undef _fdopen
#undef fflush
#undef fclose
#undef fopen
#undef pipe

#ifndef _WIN32
static int fixture_pipe(int fd[2])
{
    if (fixture_fail_pipe) { errno = EMFILE; return -1; }
    return pipe(fd);
}
#endif

static void collide(const char *path)
{
    fixture_race = 0;
    snprintf(fixture_collision, sizeof fixture_collision, "%s", path);
    FILE *f = fopen(path, "wb");
    if (!f) exit(90);
    fputs("race metadata sentinel", f);
    fclose(f);
    fixture_collisions++;
}

static FILE *fixture_fopen(const char *path, const char *mode)
{
    size_t n = strlen(path);
    if (fixture_race && n > 10 && !strcmp(path + n - 10, ".tape.json") && strchr(mode, 'w')) collide(path);
    return fopen(path, mode);
}

static int fixture_open(const char *path, int flags, ...)
{
    va_list ap;
    va_start(ap, flags);
    int mode = va_arg(ap, int);
    va_end(ap);
    if (fixture_race && (flags & O_EXCL))
    {
        collide(path);
    }
#ifdef _WIN32
    return _open(path, flags, mode);
#else
    return open(path, flags, (mode_t)mode);
#endif
}

static FILE *fixture_fdopen(int fd, const char *mode)
{
    if (fixture_fail_fdopen) { errno = ENOMEM; return NULL; }
#ifdef _WIN32
    fixture_metadata = _fdopen(fd, mode);
#else
    fixture_metadata = fdopen(fd, mode);
#endif
    return fixture_metadata;
}

static int fixture_fflush(FILE *f)
{
    if (fixture_fail_flush && f == fixture_metadata) { errno = ENOSPC; return EOF; }
    return fflush(f);
}

static int fixture_fclose(FILE *f)
{
    int fail = fixture_fail_close && f == fixture_metadata;
    if (f == fixture_metadata) fixture_metadata = NULL;
    int result = fclose(f);
    if (fail) { errno = ENOSPC; return EOF; }
    return result;
}

static void create_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f || fputs(text, f) < 0 || fclose(f)) exit(91);
}

static int has_text(const char *path, const char *text)
{
    size_t length = 0;
    char *bytes = read_all(path, &length);
    int ok = bytes && length == strlen(text) && !memcmp(bytes, text, length);
    free(bytes);
    return ok;
}

#ifndef REPRO_BASELINE
static void long_failure(const char *prefix)
{
    for (size_t i = 0; prefix[i]; ++i) game_output(prefix + i, 1);
    char large[2 * PATHN]; memset(large, 'x', sizeof large);
    game_output(large, sizeof large);
    game_output("\n", 1);
}
#endif

int main(int argc, char **argv)
{
    if (argc != 3 || strlen(argv[2]) >= sizeof sUser) return 2;
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#else
    if (prctl(PR_SET_DUMPABLE, 0)) return 94;
#endif
    snprintf(sUser, sizeof sUser, "%s", argv[2]);
    unset_env("BEA_PORT_TAPE"); unset_env("BEA_PORT_RECORD"); unset_env("BEA_PORT_CLOCK");
    unset_env("BEA_AUDIO_MONITOR");
    const char *mode = argv[1];
#ifdef _WIN32
    if (!strncmp(mode, "child-", 6) || !strcmp(mode, "write-failure") || !strcmp(mode, "symlink"))
    {
        fputs("this fixture mode requires Linux\n", stderr);
        return 2;
    }
#endif
#ifndef REPRO_BASELINE
    if (!strcmp(mode, "ordinary") || !strcmp(mode, "ordinary-failure"))
        return finish_recording(!strcmp(mode, "ordinary") ? 0 : 7);
#endif
    fixture_race = !strcmp(mode, "race");
    fixture_fail_flush = !strcmp(mode, "flush-failure");
    fixture_fail_close = !strcmp(mode, "close-failure");
    fixture_fail_fdopen = !strcmp(mode, "fdopen-failure");
    char occupied[PATHN + 8] = "";
    if (!strcmp(mode, "tape-only") || !strcmp(mode, "json-only") || !strcmp(mode, "exhausted"))
    {
        char dir[PATHN], stamp[32];
        time_t now = fixture_time(NULL);
        struct tm local;
#ifdef _WIN32
        localtime_s(&local, &now);
        unsigned pid = (unsigned)GetCurrentProcessId();
#else
        localtime_r(&now, &local);
        unsigned pid = (unsigned)getpid();
#endif
        strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);
        snprintf(dir, sizeof dir, "%s" SEP "tapes", sUser);
        make_dirs(dir);
        unsigned count = !strcmp(mode, "exhausted") ? 256 : 1;
        for (unsigned i = 0; i < count; ++i)
        {
#ifdef REPRO_BASELINE
            snprintf(occupied, sizeof occupied, "%s" SEP "%s.tape%s", dir, stamp,
                !strcmp(mode, "tape-only") ? "" : ".json");
            (void)pid;
#else
            snprintf(occupied, sizeof occupied, "%s" SEP "%s-%08x-%08x.tape%s", dir, stamp, pid, i,
                !strcmp(mode, "tape-only") ? "" : ".json");
#endif
            create_file(occupied, "preexisting sentinel");
        }
    }
#ifndef _WIN32
    if (!strcmp(mode, "write-failure"))
    {
        struct rlimit limit = {256, 256};
        if (setrlimit(RLIMIT_FSIZE, &limit)) return 93;
        signal(SIGXFSZ, SIG_IGN);
    }
#endif
    char *args[] = {"-level", "100", "quote\"and\\slash"};
    start_recording(args, 3);
    const char *record = getenv("BEA_PORT_RECORD");
    if (fixture_fail_flush || fixture_fail_close || fixture_fail_fdopen || !strcmp(mode, "write-failure") ||
        !strcmp(mode, "exhausted"))
    {
        if ((record && *record) || getenv("BEA_PORT_CLOCK") || getenv("BEA_AUDIO_MONITOR")) return 10;
#ifndef REPRO_BASELINE
        return finish_recording(0);
#else
        return 0;
#endif
    }
    if (!record || !*record) return 11;
    char path[PATHN], json[PATHN + 8];
    snprintf(path, sizeof path, "%s", record);
    snprintf(json, sizeof json, "%s.json", path);
    if (*occupied)
    {
        puts(path);
        return has_text(occupied, "preexisting sentinel") && strcmp(path, occupied) && strcmp(json, occupied) ? 0 : 15;
    }
    if (!strcmp(mode, "pair"))
    {
        size_t length = 0;
        char *before = read_all(json, &length);
        create_file(path, "first tape sentinel");
        unset_env("BEA_PORT_RECORD");
        char *next[] = {"second"};
        start_recording(next, 1);
        const char *second = getenv("BEA_PORT_RECORD");
        size_t after_length = 0;
        char *after = read_all(json, &after_length);
        int ok = before && after && length == after_length && !memcmp(before, after, length) &&
            second && strcmp(path, second) && has_text(path, "first tape sentinel");
        printf("%s\n%s\n", path, second ? second : "");
        free(before); free(after);
        return ok ? 0 : 12;
    }
    if (!strcmp(mode, "race"))
    {
        printf("%s\n%s\n", path, fixture_collision);
        return fixture_collisions == 1 && strcmp(json, fixture_collision) &&
            has_text(fixture_collision, "race metadata sentinel") ? 0 : 13;
    }
    if (!strcmp(mode, "start")) { puts(path); return 0; }
#ifndef REPRO_BASELINE
#ifndef _WIN32
    if (!strncmp(mode, "child-", 6))
    {
        fixture_fail_pipe = !strcmp(mode, "child-pipe-failure");
        snprintf(sHere, sizeof sHere, "%s" SEP "bin", sUser);
        char *child_args[] = {(char *)mode};
        return finish_recording(start_game(child_args, 1, sUser));
    }
#endif
    if (strcmp(mode, "missing") && strcmp(mode, "stage") && strcmp(mode, "symlink"))
        create_file(path, "final fixture tape");
    if (!strcmp(mode, "stage"))
    {
        char stage[PATHN + 64];
        snprintf(stage, sizeof stage, "%s.recording-1-0.incomplete", path);
        create_file(stage, "interrupted stage");
    }
#ifndef _WIN32
    if (!strcmp(mode, "symlink"))
    {
        char target[PATHN + 16];
        snprintf(target, sizeof target, "%s.target", path);
        create_file(target, "foreign tape");
        if (symlink(target, path)) return 92;
    }
#endif
    char line[PATHN + 128];
    snprintf(line, sizeof line, "onslaught-ce: recording finalized: final tape published %s%s\r\n",
        path, !strcmp(mode, "wrong-path") ? ".foreign" : "");
    if (!strcmp(mode, "overlong"))
    {
        char large[2 * PATHN]; memset(large, 'x', sizeof large);
        game_output(large, sizeof large);
    }
    if (!strcmp(mode, "long-ordinary"))
    {
        char large[2 * PATHN]; memset(large, 'x', sizeof large);
        game_output(large, sizeof large);
        game_output("\n", 1);
    }
    int long_failed = !strncmp(mode, "long-failed-", 12);
    const char *failure_prefix = strstr(mode, "-ce-") ? "onslaught-ce: recording failed: " :
        "bea-port: recording failed: ";
    if (long_failed && strstr(mode, "-before")) long_failure(failure_prefix);
    if (strcmp(mode, "foreign"))
        for (size_t i = 0; line[i]; ++i) game_output(line + i, 1); /* every status byte crosses a read boundary */
    if (long_failed && strstr(mode, "-after")) long_failure(failure_prefix);
    if (!strcmp(mode, "failed"))
    {
        const char failure[] = "bea-port: recording failed: injected finalization failure\n";
        game_output(failure, sizeof failure - 1);
    }
    int rc = finish_recording(!strcmp(mode, "game-failure") ? 7 : 0);
    printf("%s\n", path);
    return rc;
#else
    return 14;
#endif
}
