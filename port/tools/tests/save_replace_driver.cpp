// SPDX-License-Identifier: GPL-3.0-only
// Driver for port/tools/save_replace_tests.py: the real Linux path layer (paths.cpp), file layer (files.cpp) and atomic replace
// (save_replace.cpp) over authored folders. Only handle allocation is stubbed. Fault injection lives in the separate
// save_replace_faults.cpp (linker wrappers that exist only in this test executable).
#include "handles.h"
#include "common/save_replace.h"
#include <io.h>

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

extern "C" FILE *port_fopen(const char *, const char *);
extern "C" long SaveTestTempEexist();    // save_replace_faults.cpp: how often creating a temporary file met a taken name

PortHandle *PortHandleNew(PortHandleKind kind, size_t size)
{
    PortHandle *h = (PortHandle *)calloc(1, size);
    h->kind = kind;
    return h;
}
void PortHandleFree(PortHandle *h) { free(h); }
PortHandle *PortCheck(HANDLE h, int kind)
{
    PortHandle *p = (PortHandle *)h;
    return p && p->kind == kind ? p : NULL;
}
void PortSetLastError(DWORD) {}

static bool Slurp(const char *path, unsigned char **data, unsigned *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = (unsigned char *)malloc(n ? n : 1);
    bool ok = fread(*data, 1, n, f) == (size_t)n;
    fclose(f);
    *size = (unsigned)n;
    return ok;
}

static double Now()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

// One real object at the name `.ce-write-<this pid>-<k>` that a replace by this process draws k-th for its temporary file.
// kind: file (holds PLANTED-<k>), symlink-dangling (to a name nothing has), symlink-file (to ../outside-<k>, which holds
// OUTSIDE-<k>): writing through that link would change a file outside the career folder.
static bool Plant(const char *folder, const char *kind, int k)
{
    char name[600], target[64], outside[600];
    snprintf(name, sizeof(name), "%s/.ce-write-%ld-%d", folder, (long)getpid(), k);
    if (!strcmp(kind, "file"))
    {
        FILE *f = fopen(name, "wb");
        if (!f) return false;
        fprintf(f, "PLANTED-%d", k);
        return fclose(f) == 0;
    }
    if (!strcmp(kind, "symlink-dangling"))
    {
        snprintf(target, sizeof(target), "ce-planted-missing-%d", k);
        return symlink(target, name) == 0;
    }
    if (!strcmp(kind, "symlink-file"))
    {
        snprintf(outside, sizeof(outside), "%s/../outside-%d", folder, k);
        FILE *f = fopen(outside, "wb");
        if (!f) return false;
        fprintf(f, "OUTSIDE-%d", k);
        if (fclose(f) != 0) return false;
        snprintf(target, sizeof(target), "../outside-%d", k);
        return symlink(target, name) == 0;
    }
    return false;
}

struct Racer
{
    const char *path;
    unsigned char *data;
    unsigned size;
    int rounds;
    int failed;
};
static pthread_barrier_t sRaceBarrier;

static void *RaceThread(void *p)
{
    Racer *r = (Racer *)p;
    for (int i = 0; i < r->rounds; i++)
    {
        pthread_barrier_wait(&sRaceBarrier);    // both threads leave together, so both draw a temporary name at once
        if (PortReplaceUserFile(r->path, r->data, r->size, "career") != 0) r->failed++;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    PortPathsInit();
    const char *cmd = argv[1];

    // replace <game path> <payload file>: prints the PortReplaceUserFile result.
    if (!strcmp(cmd, "replace") && argc == 4)
    {
        unsigned char *data;
        unsigned size;
        if (!Slurp(argv[3], &data, &size)) return 2;
        int result = PortReplaceUserFile(argv[2], data, size, "career");
        printf("%d\n", result);
        return 0;
    }
    // collide <career folder> <game path> <payload file> <count> <kind>: first plants <count> real objects (kind: file,
    // symlink-dangling, symlink-file) at the temporary names this process is about to draw (.ce-write-<pid>-1 ... -<count>),
    // then replaces. Prints "<result> <pid>". The fault wrapper's injected EEXIST cannot tell whether the file is created
    // exclusively; real objects can.
    if (!strcmp(cmd, "collide") && argc == 7)
    {
        unsigned char *data;
        unsigned size;
        if (!Slurp(argv[4], &data, &size)) return 2;
        for (int k = 1; k <= atoi(argv[5]); k++)
            if (!Plant(argv[2], argv[6], k)) return 2;
        int result = PortReplaceUserFile(argv[3], data, size, "career");
        printf("%d %ld\n", result, (long)getpid());
        return 0;
    }
    // race <path A> <payload A> <path B> <payload B> <rounds>: two threads replace two careers of one folder at the same
    // moment, <rounds> times. Prints how many replaces failed and how often a temporary name was already taken.
    if (!strcmp(cmd, "race") && argc == 7)
    {
        Racer r[2] = {};
        int rounds = atoi(argv[6]);
        for (int i = 0; i < 2; i++)
        {
            r[i].path = argv[2 + 2 * i];
            r[i].rounds = rounds;
            if (!Slurp(argv[3 + 2 * i], &r[i].data, &r[i].size)) return 2;
        }
        pthread_barrier_init(&sRaceBarrier, NULL, 2);
        pthread_t t[2];
        for (int i = 0; i < 2; i++)
            if (pthread_create(&t[i], NULL, RaceThread, &r[i]) != 0) return 2;
        for (int i = 0; i < 2; i++)
            pthread_join(t[i], NULL);
        printf("rounds=%d failed=%d eexist=%ld\n", rounds, r[0].failed + r[1].failed, SaveTestTempEexist());
        return 0;
    }
    // list <spec>: the game's own search (_findfirst) over both roots.
    if (!strcmp(cmd, "list") && argc == 3)
    {
        _finddata_t d;
        long h = _findfirst(argv[2], &d);
        if (h == -1) return 0;
        do { printf("%s\t%lu\n", d.name, (unsigned long)d.size); } while (_findnext(h, &d) == 0);
        return _findclose(h);
    }
    // read <game path>: the file as the game reads it, to stdout.
    if (!strcmp(cmd, "read") && argc == 3)
    {
        FILE *f = port_fopen(argv[2], "rb");
        if (!f) return 1;
        char b[4096];
        size_t n;
        while ((n = fread(b, 1, sizeof(b), f)) != 0) fwrite(b, 1, n, stdout);
        fclose(f);
        return 0;
    }
    // write-loop <game path> <payload A> <payload B> <count>: alternates the two, every replace must succeed.
    if (!strcmp(cmd, "write-loop") && argc == 6)
    {
        unsigned char *a, *b;
        unsigned na, nb;
        if (!Slurp(argv[3], &a, &na) || !Slurp(argv[4], &b, &nb)) return 2;
        int count = atoi(argv[5]), failed = 0;
        for (int i = 0; i < count; i++)
            if (PortReplaceUserFile(argv[2], i % 2 ? b : a, i % 2 ? nb : na, "career") != 0) failed++;
        printf("replaced=%d failed=%d\n", count, failed);
        return failed ? 1 : 0;
    }
    // read-loop <path> <payload A> <payload B> <milliseconds> [<other payload>]: every read must be exactly A or B. A path
    // starting with / is read as the exact host file; otherwise it is the game's own lookup through both roots. A read that
    // is neither is counted as `other` when it equals the optional last file (a stale answer, but complete), else as bad.
    if (!strcmp(cmd, "read-loop") && (argc == 6 || argc == 7))
    {
        unsigned char *a, *b, *c = NULL;
        unsigned na, nb, nc = 0;
        if (!Slurp(argv[3], &a, &na) || !Slurp(argv[4], &b, &nb) || (argc == 7 && !Slurp(argv[6], &c, &nc))) return 2;
        double end = Now() + atoi(argv[5]) / 1000.0;
        long ok = 0, bad = 0, missing = 0, other = 0;
        size_t room = (na > nb ? na : nb) + (nc > na ? nc : 0) + 4096;
        unsigned char *got = (unsigned char *)malloc(room);
        while (Now() < end)
        {
            FILE *f = port_fopen(argv[2], "rb");
            if (!f) { missing++; continue; }
            size_t n = fread(got, 1, room, f);
            fclose(f);
            if ((n == na && !memcmp(got, a, na)) || (n == nb && !memcmp(got, b, nb))) ok++;
            else if (c && n == nc && !memcmp(got, c, nc)) other++;
            else bad++;
        }
        printf("ok=%ld bad=%ld missing=%ld other=%ld\n", ok, bad, missing, other);
        return bad ? 1 : 0;
    }
    return 2;
}
