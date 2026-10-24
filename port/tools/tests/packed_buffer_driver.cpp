// SPDX-License-Identifier: GPL-3.0-only
// Compile the real generated reader, not a second implementation of its loops.
#include PACKED_READER_SOURCE
#undef printf
#undef fopen
#include <stdarg.h>

static DWORD last_error = ERROR_HANDLE_EOF;
static unsigned fatal_errors;
static unsigned long capacities[32];
static unsigned capacity_count;

// File/error/allocation boundaries only. Decompression uses the linked zlib.
struct Input { unsigned char *data; DWORD size, pos, calls, max_request; };
extern "C" BOOL WINAPI ReadFile(HANDLE handle, LPVOID output, DWORD size, LPDWORD got, LPOVERLAPPED)
{
    Input *input = (Input *)handle;
    ++input->calls;
    if (size > input->max_request) input->max_request = size;
    DWORD amount = input->size - input->pos;
    if (amount > size) amount = size;
    if (amount) memcpy(output, input->data + input->pos, amount);
    input->pos += amount;
    *got = amount;
    return TRUE; // Like a successful synchronous file read, EOF is a short read.
}
extern "C" DWORD WINAPI SetFilePointer(HANDLE handle, LONG offset, PLONG, DWORD)
{
    ((Input *)handle)->pos = offset;
    return offset;
}
extern "C" DWORD WINAPI GetLastError() { return last_error; }
extern "C" void WINAPI SetLastError(DWORD error) { last_error = error; }
void DebugTrace(char *, ...) {}
void CConsole::FatalFileError(char *, SINT) { ++fatal_errors; }
CConsole CONSOLE;
CMemoryHeap::CMemoryHeap() {}
CMemoryHeap::~CMemoryHeap() {}
CDXMemoryManager::CDXMemoryManager() {}
CDXMemoryManager::~CDXMemoryManager() {}
void *CDXMemoryManager::Alloc(UINT size, EMemoryType, char *, UINT) { return malloc(size); }
void CDXMemoryManager::Free(void *p) { free(p); }
CDXMemoryManager MEM_MANAGER;

extern "C" int __real_uncompress(unsigned char *, unsigned long *, const unsigned char *, unsigned long);
extern "C" int __wrap_uncompress(unsigned char *dest, unsigned long *size,
                                  const unsigned char *source, unsigned long source_size)
{
    if (capacity_count == sizeof capacities / sizeof *capacities) exit(3);
    capacities[capacity_count++] = *size;
    return __real_uncompress(dest, size, source, source_size);
}

class Reader : public CDXMemBuffer
{
    char crc_dummy[2];
public:
    using CDXMemBuffer::ReadBlock;
    using CDXMemBuffer::SeekBlock;
    void Prepare(Input *input, char *data, unsigned capacity)
    {
        mFile = input;
        mData = mPtr = data;
        mSize = capacity;
        mDataSize = 0;
        mReading = TRUE;
        mLastBlock = mEOF = FALSE;
        mCRCData = NULL;
        mCRCDataUpTo = crc_dummy;
        strcpy(mFileName, "fixture.aya");
    }
    void Detach() { mData = mPtr = NULL; }
    unsigned Count() const { return mDataSize; }
    bool Last() const { return mLastBlock; }
};

static unsigned char *Load(const char *name, unsigned *size)
{
    FILE *file = fopen(name, "rb");
    if (!file || fseek(file, 0, SEEK_END)) exit(3);
    long length = ftell(file);
    if (length < 0 || length > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) exit(3);
    unsigned char *data = (unsigned char *)malloc(length + 1);
    if (!data || fread(data, 1, length, file) != (unsigned)length || fclose(file)) exit(3);
    *size = length;
    return data;
}

int main(int argc, char **argv)
{
    if (argc != 7) return 2;
    unsigned input_size, expected_size;
    unsigned char *input_data = Load(argv[2], &input_size);
    unsigned char *expected = Load(argv[3], &expected_size);
    unsigned capacity = strtoul(argv[4], NULL, 0);
    unsigned next_capacity = strtoul(argv[5], NULL, 0);
    unsigned seek = strtoul(argv[6], NULL, 0);
    if (capacity > 8 * 1024 * 1024 || expected_size > capacity) return 2;
    const unsigned guard = 64;
    unsigned char *output = (unsigned char *)malloc(capacity + 2 * guard);
    if (!output) return 3;
    memset(output, 0xa5, capacity + 2 * guard);
    unsigned char *destination = output + guard;
    Input input = {input_data, input_size, 0, 0, 0};
    Reader::SetNextReadBufferSize(next_capacity);
    DWORD count = 0, seek_result = 0;
    BOOL ok;
    bool last = false;
    if (!strcmp(argv[1], "load"))
    {
        Reader reader;
        reader.Prepare(&input, (char *)destination, capacity);
        reader.LoadNextBlock();
        count = reader.Count();
        last = reader.Last();
        ok = !fatal_errors;
        reader.Detach();
    }
    else if (!strcmp(argv[1], "seek"))
    {
        seek_result = Reader::SeekBlock("fixture.aya", &input, seek);
        ok = TRUE;
    }
    else if (!strcmp(argv[1], "packed") || !strcmp(argv[1], "plain"))
        ok = Reader::ReadBlock(!strcmp(argv[1], "packed") ? "fixture.aya" : "fixture.bin",
                               &input, (char *)destination, capacity, &count);
    else return 2;
    bool guards_ok = true;
    for (unsigned i = 0; i < guard; ++i)
        if (output[i] != 0xa5 || destination[capacity + i] != 0xa5) guards_ok = false;
    printf("{\"ok\":%s,\"read\":%lu,\"position\":%lu,\"read_calls\":%lu,"
           "\"max_request\":%lu,\"error\":%lu,\"fatal\":%u,\"last\":%s,\"seek\":%lu,"
           "\"canaries\":%s,\"payload\":%s,\"capacities\":[",
           ok ? "true" : "false", count, input.pos, input.calls, input.max_request, last_error,
           fatal_errors, last ? "true" : "false", seek_result, guards_ok ? "true" : "false",
           !memcmp(destination, expected, expected_size) ? "true" : "false");
    for (unsigned i = 0; i < capacity_count; ++i) printf("%s%lu", i ? "," : "", capacities[i]);
    printf("]}\n");
    free(output);
    free(expected);
    free(input_data);
    return 0;
}
