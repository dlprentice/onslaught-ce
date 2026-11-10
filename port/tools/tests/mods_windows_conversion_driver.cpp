// The runner inserts the candidate loader's actual conversion guard here.
// Its Windows conversion and error calls are fakes; no filesystem is opened.
#include <cstddef>
#include <cstring>
#include <cwchar>

typedef unsigned long DWORD;
enum {
    CP_UTF8 = 1,
    MB_ERR_INVALID_CHARS = 2,
    ERROR_INSUFFICIENT_BUFFER = 3,
    ERROR_NO_UNICODE_TRANSLATION = 4
};

static int mode;
static int conversion_calls;
static int error_reads;
static int open_attempts;
static DWORD last_error;

static const char *PortUserDir() { return "example user folder"; }
static int MultiByteToWideChar(unsigned codepage, unsigned flags, const char *text,
                               int text_length, wchar_t *out, int capacity) {
    ++conversion_calls;
    if (codepage != CP_UTF8 || flags != MB_ERR_INVALID_CHARS ||
        text != PortUserDir() || text_length != -1 || !out || capacity < 2) {
        last_error = 99;
        return 0;
    }
    if (mode == 0) { out[0] = L'x'; out[1] = L'\0'; return 2; }
    last_error = mode == 1 ? ERROR_INSUFFICIENT_BUFFER :
                 mode == 2 ? ERROR_NO_UNICODE_TRANSLATION : 99;
    return 0;
}
static DWORD GetLastError() { ++error_reads; return last_error; }

static const char *TestOpenUserFolder() {
    wchar_t userW[260];
/* INSERT_CONVERSION_GUARD */
    ++open_attempts;
    return NULL;
}

int main() {
    mode = 0;
    if (TestOpenUserFolder() != NULL || open_attempts != 1 || error_reads) return 1;
    mode = 1;
    const char *reason = TestOpenUserFolder();
    if (!reason || std::strcmp(reason, "the user folder path is too long for mod loading") != 0 ||
        open_attempts != 1 || error_reads != 1) return 2;
    mode = 2;
    reason = TestOpenUserFolder();
    if (!reason || std::strcmp(reason, "the user folder path is not valid UTF-8") != 0 ||
        open_attempts != 1 || error_reads != 2) return 3;
    mode = 3;
    reason = TestOpenUserFolder();
    if (!reason || std::strcmp(reason, "Windows could not convert the user folder path") != 0 ||
        open_attempts != 1 || error_reads != 3) return 4;
    return conversion_calls == 4 ? 0 : 5;
}
