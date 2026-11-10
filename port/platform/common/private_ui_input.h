#pragma once
#include <stddef.h>
#include <stdint.h>

// Harness-only Linux input. BEA_PORT_UI_INPUT_FD names a private readable pipe
// or owner-only regular file, never its payload. The descriptor is consumed,
// duplicated CLOEXEC, and removed from the environment before START can exec.
// Records: "CEUI", version=1, kind (1=text, 2=folder), LE16 chooser ordinal
// (text=0; first explicit chooser=1), LE32 due Present, LE32 payload length,
// then payload. Text is 1..512 printable ASCII bytes; folder is an absolute
// path of at most4096 bytes, or empty for Cancel. Only one record is retained.
// Text goes through ordinary WM_CHAR editing, at most16 characters/Present.
// Folder delivery requires an actual Choose action and matching ordinal; this
// backend does not establish acceptance of the operating system's chooser UI.
// Explicit Cancel of a pending chooser retires and scrubs this harness channel;
// its ordinary null callback is deferred to Pump, with no later OS fallback.
// Input recording/replay/dumps are incompatible. No payload is diagnosed.
typedef bool (*PortPrivateUiChar)(void* context, unsigned char character);
typedef void (*PortPrivateUiFolderCallback)(void* context, const char* const* files, int filter);
enum { PORT_PRIVATE_UI_DISABLED=0, PORT_PRIVATE_UI_WAITING=1,
       PORT_PRIVATE_UI_REFUSED=2, PORT_PRIVATE_UI_END=3 };
bool PortPrivateUiConfigured() noexcept;
int PortPrivateUiStatus() noexcept;
void PortPrivateUiPump(uint64_t present, PortPrivateUiChar deliver, void* context) noexcept;
bool PortPrivateUiChooseFolder(PortPrivateUiFolderCallback callback, void* context) noexcept;
void PortPrivateUiCancelFolder() noexcept;
