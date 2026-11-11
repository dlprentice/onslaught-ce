// SPDX-License-Identifier: GPL-3.0-only
// The entry guards and options writer are extracted from the patched production sources by the Python test.
// The remaining card bodies are in-memory boundaries: no game, data, save, display or network is opened.
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef wchar_t WCHAR;
typedef int BOOL;
static int boot_requested, lockstep_requested;
static int creates, writes, deletes, options, option_failure;

extern "C" int CE_BootRequested() { return boot_requested; }
extern "C" int CE_LockstepRequested() { return lockstep_requested; }
extern "C" int PortReplaceUserFile(const char *path, const void *, unsigned, const char *)
{
    if (std::strcmp(path, "defaultoptions.bea") != 0) return 1;
    ++options;
    return 0;
}
void PortCeOptionsWriteFailed() { ++option_failure; }
struct History { void AddMessage(const char *) { ++option_failure; } } SETUP_HISTORY;

class CPCMemoryCard {
public:
#include "career_declarations.inc"
};
class CFEPOptions {
public:
    static void WriteDefaultOptionsFile(char *, int);
};
#include "career_entries.inc"
#include "options_writer.inc"

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    boot_requested = std::atoi(argv[1]);
    lockstep_requested = std::atoi(argv[2]);
    const bool blocked =
#ifdef CE_NETWORK_OFF
        false;
#else
        boot_requested || lockstep_requested;
#endif
    CPCMemoryCard card;
    WCHAR name[] = L"authored";
    char content[] = "authored";
    int save = 77, save_new = 77;
    int c = card.CreateSave(0, name, &save, 1);
    int c_new = card.CreateSave(0, name, &save_new, 0);
    int c_null = blocked ? card.CreateSave(0, name, NULL, 1) : 1;
    int w = card.WriteSave(0, 0, name, content, sizeof content);
    int d = card.DeleteSave(0, 0, name);
    CFEPOptions::WriteDefaultOptionsFile(content, sizeof content);
    if (blocked)
        return c == 1 && c_new == 1 && c_null == 1 && w == 1 && d == 1 && save == -1 && save_new == -1 &&
               creates == 0 && writes == 0 && deletes == 0 &&
               options == 1 && option_failure == 0 ? 0 : 1;
    return c == 0 && c_new == 0 && w == 0 && d == 0 && save == 91 && save_new == 91 && creates == 2 && writes == 1 && deletes == 1 &&
           options == 1 && option_failure == 0 ? 0 : 1;
}
