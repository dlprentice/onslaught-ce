// SPDX-License-Identifier: GPL-3.0-only
// Include the actual public reader; link the real Linux MMIO and path implementations.
#include WAV_READER_SOURCE
#undef printf
#undef fopen

static unsigned failures, checks, allocations, frees, opens, closes, invalid_calls;
static HMMIO active[16];
static unsigned active_count;
void PortPathsInit();
TCHAR *ToTCHAR(char *text) { return text; }
CMemoryHeap::CMemoryHeap() {}
CMemoryHeap::~CMemoryHeap() {}
CDXMemoryManager::CDXMemoryManager() {}
CDXMemoryManager::~CDXMemoryManager() {}
void *CDXMemoryManager::Alloc(UINT size, EMemoryType, char *, UINT)
{
    void *p = malloc(size);
    if (!p) exit(3);
    memset(p, 0x5a, size); // Every reader is constructed in deliberately nonzero storage.
    ++allocations;
    return p;
}
void CDXMemoryManager::Free(void *p) { if (p) { ++frees; free(p); } }
CDXMemoryManager MEM_MANAGER;

static bool Live(HMMIO file)
{
    for (unsigned i = 0; i < active_count; ++i) if (active[i] == file) return true;
    ++invalid_calls;
    return false;
}
extern "C" HMMIO WINAPI __real_mmioOpenA(LPSTR, LPMMIOINFO, DWORD);
extern "C" MMRESULT WINAPI __real_mmioClose(HMMIO, UINT);
extern "C" LONG WINAPI __real_mmioSeek(HMMIO, LONG, int);
extern "C" MMRESULT WINAPI __real_mmioGetInfo(HMMIO, LPMMIOINFO, UINT);
extern "C" HMMIO WINAPI __wrap_mmioOpenA(LPSTR name, LPMMIOINFO info, DWORD flags)
{
    HMMIO file = __real_mmioOpenA(name, info, flags);
    if (file)
    {
        if (active_count == sizeof active / sizeof *active) exit(3);
        active[active_count++] = file;
        ++opens;
    }
    return file;
}
extern "C" MMRESULT WINAPI __wrap_mmioClose(HMMIO file, UINT flags)
{
    // Invalid/double closes are failures, not crashes: this bounds the pre-fix negative control.
    if (!Live(file)) return MMSYSERR_ERROR;
    MMRESULT result = __real_mmioClose(file, flags);
    for (unsigned i = 0; i < active_count; ++i)
        if (active[i] == file) { active[i] = active[--active_count]; break; }
    ++closes;
    return result;
}
extern "C" LONG WINAPI __wrap_mmioSeek(HMMIO file, LONG offset, int origin)
{
    return Live(file) ? __real_mmioSeek(file, offset, origin) : -1;
}
extern "C" MMRESULT WINAPI __wrap_mmioGetInfo(HMMIO file, LPMMIOINFO info, UINT flags)
{
    return Live(file) ? __real_mmioGetInfo(file, info, flags) : MMSYSERR_ERROR;
}

static void Check(bool passed, const char *description)
{
    ++checks;
    if (!passed) { ++failures; fprintf(stderr, "FAIL %s\n", description); }
}
static void ClosedRead(CWaveSoundRead *reader)
{
    unsigned char data[4];
    UINT got = 99;
    Check(FAILED(reader->Read(sizeof data, data, &got)) && got == 0, "closed Read refuses and clears count");
    Check(FAILED(reader->Reset()), "closed Reset refuses");
}
static void FailedOpen(CWaveSoundRead *reader, const char *name)
{
    Check(FAILED(reader->Open(const_cast<char *>(name))), "invalid file is rejected");
    Check(!reader->m_hmmioIn && !reader->HasFormat(), "failed Open retains no handle or format");
    Check(active_count == 0, "failed Open releases prior and candidate handles");
}
static void ReadWave(CWaveSoundRead *reader, char *name, const char *expected_name, WORD tag, WORD extra)
{
    HRESULT hr = reader->Open(name);
    Check(SUCCEEDED(hr), "valid Open");
    if (FAILED(hr)) return;
    Check(reader->HasFormat() && reader->m_hmmioIn, "successful Open owns format and handle");
    Check(active_count == 1, "one owned handle after Open/reopen");
    if (!reader->m_pwfx) return;
    Check(reader->m_pwfx->wFormatTag == tag && reader->m_pwfx->cbSize == extra, "format kind and extra bytes");
    Check(reader->GetSampleRate() == 22050 && reader->GetChannels() == 1, "sample rate and channels");
    FILE *expected = fopen(expected_name, "rb");
    if (!expected) exit(3);
    unsigned char want[12000], got[12032];
    unsigned size = fread(want, 1, sizeof want, expected);
    if (ferror(expected) || !feof(expected) || fclose(expected)) exit(3);
    UINT amount = 0;
    Check(SUCCEEDED(reader->Read(3, got, &amount)) && amount == 3 && !memcmp(got, want, 3), "first samples");
    Check(SUCCEEDED(reader->Reset()), "Reset after partial read");
    Check(SUCCEEDED(reader->Read(sizeof got, got, &amount)) && amount == size && !memcmp(got, want, size),
          "all samples through real MMIO buffering, bounded at data end");
    amount = 99;
    Check(SUCCEEDED(reader->Read(1, got, &amount)) && amount == 0, "sample EOF");
}

int main(int argc, char **argv)
{
    if (argc != 4) return 2; // case, first sample reference, second sample reference
    PortPathsInit();
    CWaveSoundRead *reader = new(MEMTYPE_SOUND) CWaveSoundRead;
    Check(!reader->HasFormat(), "construction initializes the format pointer");
    if (!strncmp(argv[1], "unopened", 8))
        Check(reader->m_hmmioIn == NULL, "poisoned-storage construction initializes the handle");
    if (!strcmp(argv[1], "unopened")) {}
    else if (!strcmp(argv[1], "unopened-close-read"))
    {
        Check(SUCCEEDED(reader->Close()) && SUCCEEDED(reader->Close()), "unopened Close is idempotent");
        ClosedRead(reader);
    }
    else if (!strcmp(argv[1], "missing")) FailedOpen(reader, "missing.wav");
    else if (!strcmp(argv[1], "bad-format")) FailedOpen(reader, "bad.wav");
    else if (!strcmp(argv[1], "missing-data")) FailedOpen(reader, "no-data.wav");
    else if (!strcmp(argv[1], "truncated-extra")) FailedOpen(reader, "short-extra.wav");
    else if (!strcmp(argv[1], "destructor-open")) ReadWave(reader, "first.wav", argv[2], 1, 0);
    else if (!strcmp(argv[1], "repeat-close"))
    {
        ReadWave(reader, "first.wav", argv[2], 1, 0);
        Check(SUCCEEDED(reader->Close()) && SUCCEEDED(reader->Close()), "repeated Close succeeds");
        Check(!reader->m_hmmioIn && active_count == 0, "Close clears the owned handle");
        Check(reader->HasFormat(), "Close preserves the last format as before");
        ClosedRead(reader);
    }
    else if (!strcmp(argv[1], "reopen-valid"))
    {
        ReadWave(reader, "first.wav", argv[2], 1, 0);
        ReadWave(reader, "second.wav", argv[3], 1, 0);
        Check(opens == 2 && closes == 1, "reopen releases the earlier handle");
    }
    else if (!strcmp(argv[1], "reopen-missing") || !strcmp(argv[1], "reopen-no-data"))
    {
        ReadWave(reader, "first.wav", argv[2], 1, 0);
        FailedOpen(reader, !strcmp(argv[1], "reopen-missing") ? "missing.wav" : "no-data.wav");
    }
    else if (!strcmp(argv[1], "recover-after-failure"))
    {
        FailedOpen(reader, "no-data.wav");
        ReadWave(reader, "first.wav", argv[2], 1, 0);
    }
    else if (!strcmp(argv[1], "non-pcm")) ReadWave(reader, "extra.wav", argv[3], 2, 3);
    else return 2;
    delete reader;
    Check(active_count == 0 && opens == closes, "all successful opens closed exactly once");
    Check(invalid_calls == 0, "no invalid or repeated MMIO calls");
    Check(allocations == frees, "all reader and format allocations released");
    printf("{\"checks\":%u,\"failures\":%u,\"opens\":%u,\"closes\":%u,\"active\":%u,"
           "\"invalid_calls\":%u,\"allocations\":%u,\"frees\":%u}\n",
           checks, failures, opens, closes, active_count, invalid_calls, allocations, frees);
    // Release only handles opened by this test when evaluating a leaking pre-fix reader.
    while (active_count) __real_mmioClose(active[--active_count], 0);
    return failures ? 1 : 0;
}
