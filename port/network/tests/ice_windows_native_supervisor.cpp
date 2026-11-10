// SPDX-License-Identifier: GPL-3.0-only
// Standalone, synthetic-fixture supervisor. It never launches the game.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr DWORD kMaxFile = 32 * 1024 * 1024;
constexpr DWORD kCaseMs = 90000;
constexpr size_t kFailedOutputBytes = 8192;
struct Failure { unsigned code; };
void Need(bool yes, unsigned code) { if (!yes) throw Failure{code}; }
struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE value) : h(value) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : h(other.h) { other.h = INVALID_HANDLE_VALUE; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { if (good()) CloseHandle(h); h = other.h; other.h = INVALID_HANDLE_VALUE; }
        return *this;
    }
    ~Handle() { if (h != INVALID_HANDLE_VALUE && h != nullptr) CloseHandle(h); }
    bool good() const { return h != INVALID_HANDLE_VALUE && h != nullptr; }
};
std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }
std::string Hex(const BYTE* bytes, size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string result;
    for (size_t i = 0; i < size; ++i) { result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15]; }
    return result;
}
bool HexString(const std::string& s, size_t n) {
    return s.size() == n && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}
bool Name(const std::string& s) {
    return !s.empty() && s.size() <= 96 && s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-._") == std::string::npos &&
        s != "." && s != ".." && s.back() != '.';
}
bool Relative(const std::string& s) {
    if (s.empty() || s.size() > 240) return false;
    std::istringstream in(s); std::string part;
    while (std::getline(in, part, '/')) if (!Name(part)) return false;
    return s.back() != '/';
}
std::wstring Join(const std::wstring& root, const std::string& leaf) {
    Need(Relative(leaf), 10);
    std::wstring path = root + L"\\" + Wide(leaf);
    std::replace(path.begin(), path.end(), L'/', L'\\');
    return path;
}
std::vector<BYTE> User() {
    HANDLE raw = nullptr; Need(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw) != 0, 11);
    Handle token(raw); DWORD needed = 0;
    GetTokenInformation(token.h, TokenUser, nullptr, 0, &needed);
    Need(needed > 0 && needed < 65536, 12);
    std::vector<BYTE> data(needed);
    Need(GetTokenInformation(token.h, TokenUser, data.data(), needed, &needed) != 0, 13);
    PSID sid = reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid;
    Need(IsValidSid(sid) != 0, 14);
    std::vector<BYTE> result(GetLengthSid(sid));
    Need(CopySid(static_cast<DWORD>(result.size()), result.data(), sid) != 0, 15);
    return result;
}
bool StandardToken(HANDLE process) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &raw)) return false;
    Handle token(raw); TOKEN_ELEVATION elevation{}; DWORD size = 0;
    if (!GetTokenInformation(token.h, TokenElevation, &elevation, sizeof(elevation), &size) || elevation.TokenIsElevated) return false;
    GetTokenInformation(token.h, TokenIntegrityLevel, nullptr, 0, &size);
    if (size == 0 || size > 65536) return false;
    std::vector<BYTE> data(size);
    if (!GetTokenInformation(token.h, TokenIntegrityLevel, data.data(), size, &size)) return false;
    PSID sid = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(data.data())->Label.Sid;
    if (!IsValidSid(sid) || *GetSidSubAuthorityCount(sid) == 0) return false;
    const DWORD level = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
    return level == SECURITY_MANDATORY_MEDIUM_RID;
}
DWORD NativeWindowsBuild() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    Need(ntdll && !GetProcAddress(ntdll, "wine_get_version"), 85);
    using Version = LONG (WINAPI*)(OSVERSIONINFOW*);
    auto version = reinterpret_cast<Version>(GetProcAddress(ntdll, "RtlGetVersion"));
    OSVERSIONINFOW info{}; info.dwOSVersionInfoSize = sizeof(info);
    Need(version && version(&info) == 0 && info.dwMajorVersion == 10 && info.dwBuildNumber >= 22000, 86);
    return info.dwBuildNumber;
}
bool Private(HANDLE handle, PSID user, bool protected_acl) {
    PSECURITY_DESCRIPTOR descriptor = nullptr; PSID owner = nullptr; PACL acl = nullptr;
    if (GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &acl, nullptr, &descriptor) != ERROR_SUCCESS) return false;
    struct Free { PSECURITY_DESCRIPTOR p; ~Free() { LocalFree(p); } } free{descriptor};
    SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0;
    if (!descriptor || !IsValidSecurityDescriptor(descriptor) || !owner || !IsValidSid(owner) || !EqualSid(owner, user) ||
        !acl || !IsValidAcl(acl) || !GetSecurityDescriptorControl(descriptor, &control, &revision) ||
        (protected_acl && !(control & SE_DACL_PROTECTED))) return false;
    std::array<BYTE, SECURITY_MAX_SID_SIZE> system{}, admins{};
    DWORD system_size = static_cast<DWORD>(system.size()), admin_size = static_cast<DWORD>(admins.size());
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system.data(), &system_size) ||
        !CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admins.data(), &admin_size)) return false;
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void* raw = nullptr; if (!GetAce(acl, i, &raw)) return false;
        auto* header = static_cast<ACE_HEADER*>(raw);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || header->AceSize < sizeof(ACCESS_ALLOWED_ACE)) return false;
        PSID sid = &static_cast<ACCESS_ALLOWED_ACE*>(raw)->SidStart;
        const size_t room = header->AceSize - offsetof(ACCESS_ALLOWED_ACE, SidStart);
        if (room < 8 || GetSidLengthRequired(*GetSidSubAuthorityCount(sid)) > room || !IsValidSid(sid) ||
            (!EqualSid(sid, user) && !EqualSid(sid, system.data()) && !EqualSid(sid, admins.data()))) return false;
    }
    return true;
}
DWORD ShareMode(bool directory, bool child_publication) {
    return FILE_SHARE_READ | (directory && child_publication ? FILE_SHARE_WRITE : 0);
}
Handle Open(const std::wstring& path, bool directory, PSID user, bool protected_acl = false, bool child_publication = false) {
    Handle result(CreateFileW(path.c_str(), (directory ? FILE_TRAVERSE : FILE_READ_DATA) | FILE_READ_ATTRIBUTES | READ_CONTROL,
        ShareMode(directory, child_publication), nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr));
    Need(result.good(), 16); BY_HANDLE_FILE_INFORMATION info{};
    Need(GetFileType(result.h) == FILE_TYPE_DISK && GetFileInformationByHandle(result.h, &info), 17);
    Need(!(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) &&
        bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory, 18);
    Need(directory || (info.nNumberOfLinks == 1 && info.nFileSizeHigh == 0 && info.nFileSizeLow <= kMaxFile), 19);
    Need(Private(result.h, user, protected_acl), 20);
    return result;
}
std::string Read(HANDLE file, DWORD limit) {
    LARGE_INTEGER zero{}; Need(SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) != 0, 21);
    std::string value; std::array<char, 4096> buffer{};
    for (;;) {
        DWORD n = 0; Need(ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &n, nullptr) != 0, 22);
        if (!n) break;
        Need(value.size() + n <= limit, 23); value.append(buffer.data(), n);
    }
    return value;
}
struct Sha256 {
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
    Sha256() {
        Need(CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != 0, 25);
        if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) { CryptReleaseContext(provider, 0); throw Failure{26}; }
    }
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    ~Sha256() { CryptDestroyHash(hash); CryptReleaseContext(provider, 0); }
    void Add(const BYTE* data, DWORD size) { Need(CryptHashData(hash, data, size, 0) != 0, 29); }
    std::string Finish() {
        std::array<BYTE, 32> value{}; DWORD size = static_cast<DWORD>(value.size());
        Need(CryptGetHashParam(hash, HP_HASHVAL, value.data(), &size, 0) != 0 && size == value.size(), 30);
        return Hex(value.data(), value.size());
    }
};
std::string Digest(HANDLE file) {
    LARGE_INTEGER zero{}; Need(SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) != 0, 24);
    Sha256 hash;
    std::array<BYTE, 65536> buffer{}; DWORD total = 0;
    for (;;) {
        DWORD n = 0; Need(ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &n, nullptr) != 0, 27);
        if (!n) break;
        Need(n <= kMaxFile - total, 28); total += n;
        hash.Add(buffer.data(), n);
    }
    return hash.Finish();
}
std::string DigestBytes(const std::string& value) {
    Sha256 hash;
    if (!value.empty()) hash.Add(reinterpret_cast<const BYTE*>(value.data()), static_cast<DWORD>(value.size()));
    return hash.Finish();
}
void Directory(const std::wstring& path) { Need(CreateDirectoryW(path.c_str(), nullptr) != 0, 31); }
void WriteNew(const std::wstring& path, const std::string& text) {
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    Need(file.good(), 32); DWORD wrote = 0;
    Need(WriteFile(file.h, text.data(), static_cast<DWORD>(text.size()), &wrote, nullptr) && wrote == text.size() && FlushFileBuffers(file.h), 33);
}
struct File { std::string path, hash; DWORD size; Handle handle; };
struct Case { std::string name, path, kind; };
struct Plan { std::string run, source; std::vector<File> files; std::vector<Case> cases; std::vector<std::string> forbidden, progress; };
std::vector<std::string> Fields(const std::string& line) {
    std::vector<std::string> fields; std::istringstream in(line); std::string field;
    while (std::getline(in, field, '\t')) fields.push_back(field);
    return fields;
}
Plan Parse(const std::string& text) {
    Plan plan; std::istringstream in(text); std::string line;
    Need(std::getline(in, line) && line == "CE-ICE-NATIVE-GATE\t1", 34);
    while (std::getline(in, line)) {
        const auto f = Fields(line); Need(!f.empty(), 35);
        if (f[0] == "run") { Need(f.size() == 2 && plan.run.empty() && Name(f[1]), 36); plan.run = f[1]; }
        else if (f[0] == "source") { Need(f.size() == 2 && plan.source.empty() && HexString(f[1], 40), 37); plan.source = f[1]; }
        else if (f[0] == "file") {
            Need(f.size() == 4 && Relative(f[1]) && HexString(f[2], 64) && f[3].find_first_not_of("0123456789") == std::string::npos, 38);
            const unsigned long size = std::stoul(f[3]); Need(size > 0 && size <= kMaxFile && plan.files.size() < 64, 39);
            for (const auto& old : plan.files) Need(old.path != f[1], 40);
            plan.files.push_back(File{f[1], f[2], static_cast<DWORD>(size), Handle{}});
        } else if (f[0] == "case") {
            Need(f.size() == 4 && Name(f[1]) && Relative(f[2]) && f[3] == "root", 41);
            plan.cases.push_back(Case{f[1], f[2], f[3]});
        } else if (f[0] == "progress") {
      Need(f.size() == 3 && f[1] == "grant" && !f[2].empty() && f[2].size() <= 160 && plan.progress.size() < 4, 90);
      for (unsigned char c : f[2]) Need(c >= 32 && c <= 126, 90);
      for (const char* word : {"PASS", "FAIL", "UNVERIFIED", "UNEXERCISED"}) Need(f[2].find(word) == std::string::npos, 90);
      Need(std::find(plan.progress.begin(), plan.progress.end(), f[2]) == plan.progress.end(), 90);
      plan.progress.push_back(f[2]);
    } else if (f[0] == "forbid") {
            Need(f.size() == 2 && f[1].size() >= 8 && f[1].size() <= 128 && plan.forbidden.size() < 32, 42);
            for (char c : f[1]) Need(c >= 32 && c <= 126, 43);
            plan.forbidden.push_back(f[1]);
        } else throw Failure{44};
    }
    Need(plan.run.find("ice-" + plan.source.substr(0, 12) + "-") == 0 && plan.cases.size() == 4 && !plan.forbidden.empty(), 45);
    const std::array<std::string, 4> names{{"closed", "grant", "directory", "protocol"}};
    for (size_t i = 0; i < names.size(); ++i) {
        Need(plan.cases[i].name == names[i], 46);
        Need(std::any_of(plan.files.begin(), plan.files.end(), [&](const File& f) { return f.path == plan.cases[i].path; }), 47);
    }
    Need(plan.progress.size() == 4, 90);
  return plan;
}
std::wstring Quote(const std::wstring& path) { Need(path.find(L'"') == std::wstring::npos, 48); return L"\"" + path + L"\""; }
std::vector<wchar_t> Environment(const std::wstring& temp) {
    std::array<wchar_t, 32768> system{};
    const DWORD n = GetWindowsDirectoryW(system.data(), static_cast<UINT>(system.size())); Need(n > 0 && n < system.size(), 49);
    std::vector<wchar_t> out;
    for (const auto& entry : std::array<std::wstring, 4>{{L"SystemRoot=" + std::wstring(system.data()), L"TEMP=" + temp, L"TMP=" + temp, L"WINDIR=" + std::wstring(system.data())}}) {
        out.insert(out.end(), entry.begin(), entry.end()); out.push_back(0);
    }
    out.push_back(0); return out;
}
std::wstring AckCommand(const std::wstring& root, const std::string& run) {
    return L"cmd.exe /d /c \"if exist " + root + L"\\ack.txt (exit /b 75) else (if exist " + root +
        L"\\ack.tmp (exit /b 75) else ((echo " + Wide(run) + L")>" + root + L"\\ack.tmp && ren " + root + L"\\ack.tmp ack.txt))\"";
}
struct AckCompletion {
    bool publication = false;
    const char* stage = "launch";
    bool waited = false, exit_queried = false, exit_ok = false, accounting_queried = false, accounting_ok = false, first_accounting_ok = false;
    DWORD wait_result = 0, wait_error = 0, exit_code = 125, exit_error = 0, active_processes = 0, accounting_error = 0;
    DWORD first_active_processes = 0, accounting_queries = 0;
    ULONGLONG wait_ms = 0, completion_ms = 0;
};
std::string AckCompletionJson(const AckCompletion& value) {
    std::ostringstream out;
    out << "{\"variant\":\"" << (value.publication ? "new" : "old") << "\",\"stage\":\"" << value.stage << "\",\"wait\":";
    if (value.waited) out << "{\"result\":" << value.wait_result << ",\"error\":" << value.wait_error << ",\"elapsed_ms\":" << value.wait_ms << "}";
    else out << "null";
    out << ",\"exit\":";
    if (value.exit_queried) out << "{\"ok\":" << (value.exit_ok ? "true" : "false") << ",\"code\":"
        << (value.exit_ok ? std::to_string(value.exit_code) : "null") << ",\"error\":" << value.exit_error << "}";
    else out << "null";
    out << ",\"accounting\":";
    if (value.accounting_queried) out << "{\"ok\":" << (value.accounting_ok ? "true" : "false") << ",\"active_processes\":"
        << (value.accounting_ok ? std::to_string(value.active_processes) : "null") << ",\"error\":" << value.accounting_error
        << ",\"first_active_processes\":" << (value.first_accounting_ok ? std::to_string(value.first_active_processes) : "null")
        << ",\"queries\":" << value.accounting_queries << ",\"elapsed_ms\":" << value.completion_ms << "}";
    else out << "null";
    out << "}"; return out.str();
}
DWORD RunAckCommand(const std::wstring& root, const std::wstring& command_text, AckCompletion& completion) {
    std::array<wchar_t, 32768> windows{};
    DWORD n = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    BOOL wow64 = FALSE;
    Need(n > 0 && n < windows.size() && IsWow64Process(GetCurrentProcess(), &wow64), 101);
    const std::wstring cmd = std::wstring(windows.data()) + (wow64 ? L"\\Sysnative\\cmd.exe" : L"\\System32\\cmd.exe");
    Handle job(CreateJobObjectW(nullptr, nullptr)); Need(job.good(), 102);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_JOB_MEMORY;
    limits.BasicLimitInformation.ActiveProcessLimit = 1; limits.JobMemoryLimit = 128 * 1024 * 1024;
    Need(SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != 0, 102);
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle sink(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); Need(sink.good(), 102);
    SIZE_T bytes = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<BYTE> storage(bytes);
    auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    Need(InitializeProcThreadAttributeList(attributes, 1, 0, &bytes) != 0, 102);
    struct Attributes { PPROC_THREAD_ATTRIBUTE_LIST p; ~Attributes() { DeleteProcThreadAttributeList(p); } } cleanup_attributes{attributes};
    HANDLE inherited[] = {sink.h};
    Need(UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr), 102);
    STARTUPINFOEXW start{}; start.StartupInfo.cb = sizeof(start); start.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    start.StartupInfo.hStdInput = sink.h; start.StartupInfo.hStdOutput = sink.h; start.StartupInfo.hStdError = sink.h; start.lpAttributeList = attributes;
    auto environment = Environment(root + L"\\work"); std::wstring command = command_text; PROCESS_INFORMATION raw{};
    Need(CreateProcessW(cmd.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
        environment.data(), root.c_str(), &start.StartupInfo, &raw), 103);
    Handle process(raw.hProcess), thread(raw.hThread);
    struct Unassigned { HANDLE p; bool assigned = false; ~Unassigned() { if (!assigned) { TerminateProcess(p, 125); WaitForSingleObject(p, 5000); } } } cleanup_process{process.h};
    Need(StandardToken(process.h) && AssignProcessToJobObject(job.h, process.h), 104); cleanup_process.assigned = true;
    Need(ResumeThread(thread.h) != static_cast<DWORD>(-1), 104);
    completion.stage = "wait"; completion.waited = true;
    const ULONGLONG wait_started = GetTickCount64();
    const ULONGLONG completion_deadline = wait_started + 5000;
    completion.wait_result = WaitForSingleObject(process.h, 5000);
    completion.wait_error = completion.wait_result == WAIT_FAILED ? GetLastError() : 0;
    completion.wait_ms = GetTickCount64() - wait_started;
    if (completion.wait_result != WAIT_OBJECT_0) {
        TerminateJobObject(job.h, 125); WaitForSingleObject(process.h, 5000); Need(false, 105);
    }
    completion.stage = "exit-query"; completion.exit_queried = true;
    DWORD code = 125; completion.exit_ok = GetExitCodeProcess(process.h, &code) != 0;
    completion.exit_error = completion.exit_ok ? 0 : GetLastError(); completion.exit_code = code;
    Need(completion.exit_ok, 110);
    completion.stage = "accounting-query"; completion.accounting_queried = true;
    for (;;) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{}; ++completion.accounting_queries;
        completion.accounting_ok = QueryInformationJobObject(job.h, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr) != 0;
        completion.accounting_error = completion.accounting_ok ? 0 : GetLastError(); completion.active_processes = accounting.ActiveProcesses;
        const ULONGLONG now = GetTickCount64(); completion.completion_ms = now - wait_started;
        if (completion.accounting_ok && completion.accounting_queries == 1) {
            completion.first_accounting_ok = true; completion.first_active_processes = accounting.ActiveProcesses;
        }
        Need(completion.accounting_ok, 111);
        completion.stage = "active-processes";
        if (accounting.ActiveProcesses == 0 && now <= completion_deadline) break;
        Need(now < completion_deadline, 112);
        Sleep(static_cast<DWORD>(std::min<ULONGLONG>(10, completion_deadline - now)));
        completion.stage = "accounting-query";
    }
    completion.stage = "complete";
    return code;
}
void AckSharingControl(const std::wstring& root, const std::string& run, PSID user, std::vector<AckCompletion>& completions) {
    for (bool publication : {false, true}) {
        const std::wstring directory = root + (publication ? L"\\work\\ack-shared" : L"\\work\\ack-denied");
        Directory(directory); Handle pin = Open(directory, true, user, false, publication);
        {
            Handle target(CreateFileW(directory.c_str(), FILE_ADD_FILE | SYNCHRONIZE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            const DWORD error = GetLastError();
            Need(publication ? target.good() : (!target.good() && error == ERROR_SHARING_VIOLATION), publication ? 107 : 106);
        }
        completions.push_back(AckCompletion{}); completions.back().publication = publication;
        const DWORD code = RunAckCommand(root, AckCommand(directory, run), completions.back());
        const std::wstring temporary = directory + L"\\ack.tmp", final = directory + L"\\ack.txt";
        const std::wstring present = publication ? final : temporary, absent = publication ? temporary : final;
        Need(publication ? code == 0 : code != 0, publication ? 107 : 106);
        Need(GetFileAttributesW(absent.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND, publication ? 107 : 106);
        {
            Handle file = Open(present, false, user); std::string value = Read(file.h, 128);
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
            Need(value == run, 108);
        }
        const std::wstring moved = directory + L"-replacement";
        Need(!MoveFileExW(directory.c_str(), moved.c_str(), 0) && GetLastError() == ERROR_SHARING_VIOLATION, 109);
    }
}
struct Log {
    Handle read, write, sink;
    DWORD total = 0;
    bool eof = false;
    void Init(const std::wstring& path) {
        SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE}; HANDLE r = nullptr, w = nullptr;
        Need(CreatePipe(&r, &w, &inherit, 4096) != 0, 77); read = Handle(r); write = Handle(w);
        Need(SetHandleInformation(read.h, HANDLE_FLAG_INHERIT, 0) != 0, 78);
        sink = Handle(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        Need(sink.good(), 79);
    }
    bool Pump() {
        std::array<BYTE, 4096> buffer{}; DWORD budget = 65536;
        while (!eof && budget) {
            DWORD available = 0;
            if (!PeekNamedPipe(read.h, nullptr, 0, nullptr, &available, nullptr)) {
                Need(GetLastError() == ERROR_BROKEN_PIPE, 80); eof = true; break;
            }
            if (!available) break;
            DWORD count = 0, written = 0;
            Need(ReadFile(read.h, buffer.data(), std::min<DWORD>(available, static_cast<DWORD>(buffer.size())), &count, nullptr) != 0 && count > 0, 81);
            if (count > 1024 * 1024 - total) return false;
            Need(WriteFile(sink.h, buffer.data(), count, &written, nullptr) != 0 && written == count, 82);
            total += count; budget -= count;
        }
        return true;
    }
    void Finish() { Need(FlushFileBuffers(sink.h) != 0, 83); sink = Handle{}; }
};
// One case stream, read through the checked no-follow handle from Open. The bytes
// pumped from the pipe, the file's size and two reads of it must all agree.
std::string CaseStream(HANDLE file, DWORD pumped, std::string& sha256) {
    LARGE_INTEGER size{};
    Need(GetFileSizeEx(file, &size) != 0 && size.QuadPart == static_cast<LONGLONG>(pumped), 93);
    std::string text = Read(file, 1024 * 1024);
    Need(text.size() == pumped, 94);
    sha256 = Digest(file);
    Need(DigestBytes(text) == sha256, 95);
    return text;
}
std::string FailedStreamJson(const std::string& text, const std::string& sha256) {
    const size_t retained = std::min(text.size(), kFailedOutputBytes);
    const auto* tail = reinterpret_cast<const BYTE*>(text.data()) + (text.size() - retained);
    std::ostringstream out;
    out << "{\"bytes\":" << text.size() << ",\"sha256\":\"" << sha256 << "\",\"retained_bytes\":" << retained
        << ",\"hex\":\"" << Hex(tail, retained) << "\"}";
    return out.str();
}
// Private evidence for the host's failure collection, verified again there
// against the case's sizes and hashes in done.json. Written before done.json.
std::string FailedCaseJson(const std::string& run, const std::string& manifest, const std::string& name,
        const std::string& out, const std::string& out_hash, const std::string& err, const std::string& err_hash) {
    return "{\"schema\":1,\"run\":\"" + run + "\",\"manifest_sha256\":\"" + manifest + "\",\"name\":\"" + name +
        "\",\"stdout\":" + FailedStreamJson(out, out_hash) + ",\"stderr\":" + FailedStreamJson(err, err_hash) + "}\n";
}
bool Summary(const std::string& role, std::string text, const std::string& errors,
             const std::vector<std::string>& progress, std::uint64_t& checks, bool& has_checks) {
  checks = 0; has_checks = false;
  if (!errors.empty()) return false;
  if (role == "grant") {
    if (progress.size() != 4) return false;
    for (const auto& expected : progress) {
      const auto newline = text.find('\n');
      if (newline == std::string::npos) return false;
      auto line = text.substr(0, newline);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line != expected) return false;
      text.erase(0, newline + 1);
    }
  }
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) text.pop_back();
  if (role == "closed") return text == "PASS Windows production exchange gate remains closed";
  std::string prefix, suffix;
  if (role == "grant") { prefix = "PASS authored checks="; suffix = "; unexercised-reparse=0; native privacy requires separate review"; }
  else if (role == "directory") { prefix = "PASS Windows private-directory checks="; suffix = "; native acceptance remains separate"; }
  else if (role == "protocol") { prefix = "PASS "; suffix = " authored ICE exchange checks"; }
  else return false;
  if (text.size() <= prefix.size() + suffix.size() || text.compare(0, prefix.size(), prefix) != 0 ||
      text.compare(text.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
  const auto digits = text.substr(prefix.size(), text.size() - prefix.size() - suffix.size());
  if (digits.size() > 8) return false;
  for (char c : digits) {
    if (c < '0' || c > '9') return false;
    checks = checks * 10 + static_cast<unsigned>(c - '0');
    if (checks > 10000000) return false;
  }
  has_checks = checks > 0;
  return has_checks;
}
struct CaseCompletion {
    bool queried = false, query_ok = false, first_ok = false;
    DWORD query_error = 0, first_active = 0, active = 0, queries = 0;
    ULONGLONG elapsed_ms = 0;
};
std::string CaseCompletionJson(const CaseCompletion& value) {
    if (!value.queried) return "null";
    std::ostringstream out;
    out << "{\"query_ok\":" << (value.query_ok ? "true" : "false") << ",\"query_error\":" << value.query_error
        << ",\"first_active_processes\":" << (value.first_ok ? std::to_string(value.first_active) : "null")
        << ",\"active_processes\":" << (value.query_ok ? std::to_string(value.active) : "null")
        << ",\"queries\":" << value.queries << ",\"elapsed_ms\":" << value.elapsed_ms << ",\"deadline_ms\":" << kCaseMs << "}";
    return out.str();
}
bool CompleteCaseJob(HANDLE job, ULONGLONG deadline, ULONGLONG started, Log& out, Log& err, CaseCompletion& value) {
    for (;;) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{}; value.queried = true; ++value.queries;
        value.query_ok = QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr) != 0;
        value.query_error = value.query_ok ? 0 : GetLastError(); value.active = accounting.ActiveProcesses;
        const ULONGLONG now = GetTickCount64(); value.elapsed_ms = now - started;
        if (value.query_ok && value.queries == 1) { value.first_ok = true; value.first_active = value.active; }
        Need(value.query_ok, 60);
        if (value.active == 0 && now <= deadline) return true;
        if (now >= deadline || !out.Pump() || !err.Pump()) return false;
        Sleep(static_cast<DWORD>(std::min<ULONGLONG>(10, deadline - now)));
    }
}
struct Attempt { DWORD pid = 0; std::uint64_t creation_time = 0; CaseCompletion completion; };
std::string FailedCase(const Case& spec, const Attempt& attempt, unsigned failure) {
  std::ostringstream out;
  out << "{\"name\":\"" << spec.name << "\",\"pid\":" << attempt.pid << ",\"creation_time\":" << attempt.creation_time
      << ",\"identity_confirmed\":" << (attempt.pid && attempt.creation_time ? "true" : "false")
      << ",\"exit\":null,\"forced\":null,\"cleanup_confirmed\":false,\"infrastructure_failure\":" << failure
      << ",\"output_complete\":false,\"no_forbidden_output\":false,\"summary_valid\":false,\"checks\":null"
      << ",\"stdout_bytes\":null,\"stderr_bytes\":null,\"stdout_sha256\":null,\"stderr_sha256\":null,\"job_completion\":"
      << CaseCompletionJson(attempt.completion) << "}";
  return out.str();
}
std::string RunCase(const std::wstring& root, const Case& spec, PSID user, const Plan& plan, const std::string& manifest_hash, bool& passed, Attempt& attempt) {
    const auto& forbidden = plan.forbidden; const auto& progress = plan.progress;
    const std::wstring work = Join(root, "work/" + spec.name), fixture = Join(root, "fixtures/" + spec.name);
    Directory(work); const std::wstring temp = work + L"\\temp"; Directory(temp);
    const std::wstring out_path = Join(root, "output/" + spec.name + ".stdout"), err_path = Join(root, "output/" + spec.name + ".stderr");
    Log out, err; out.Init(out_path); err.Init(err_path);
    Handle job(CreateJobObjectW(nullptr, nullptr)); Need(job.good(), 51);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_JOB_MEMORY;
    limits.BasicLimitInformation.ActiveProcessLimit = 8; limits.JobMemoryLimit = 512 * 1024 * 1024;
    Need(SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != 0, 52);
    SIZE_T attribute_size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    std::vector<BYTE> attribute_bytes(attribute_size);
    auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attribute_bytes.data());
    Need(InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size) != 0, 53);
    struct AttributeCleanup { PPROC_THREAD_ATTRIBUTE_LIST p; ~AttributeCleanup() { DeleteProcThreadAttributeList(p); } } attribute_cleanup{attributes};
    HANDLE inherited[] = {out.write.h, err.write.h};
    Need(UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr) != 0, 54);
    STARTUPINFOEXW start{}; start.StartupInfo.cb = sizeof(start); start.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    start.StartupInfo.hStdOutput = out.write.h; start.StartupInfo.hStdError = err.write.h; start.StartupInfo.hStdInput = nullptr; start.lpAttributeList = attributes;
    std::wstring exe = Join(root, spec.path), command = Quote(exe);
    command += L" " + Quote(fixture);
    if (spec.name == "grant") command += L" --prepared-native-reparse " + Quote(root + L"\\prepared-reparse");
    if (spec.name == "directory") command += L" --prepared-native-reparse " + Quote(root + L"\\prepared-reparse-directory");
    auto environment = Environment(temp); PROCESS_INFORMATION raw{};
    Need(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
        environment.data(), work.c_str(), &start.StartupInfo, &raw) != 0, 55);
    Handle process(raw.hProcess), thread(raw.hThread);
    // Every post-creation failure owns this exact child; never leave a suspended orphan.
    struct ChildCleanup { HANDLE process; bool resumed = false; ~ChildCleanup() { if (!resumed) { TerminateProcess(process, 125); WaitForSingleObject(process, 5000); } } } child_cleanup{process.h};
  attempt.pid = raw.dwProcessId;
    Need(StandardToken(process.h), 56);
    FILETIME created{}, ended{}, kernel{}, cpu{};
    Need(GetProcessTimes(process.h, &created, &ended, &kernel, &cpu) != 0, 84);
    const std::uint64_t creation_time = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
  attempt.creation_time = creation_time;
    Need(AssignProcessToJobObject(job.h, process.h) != 0, 57);
    out.write = Handle{}; err.write = Handle{};
    Need(ResumeThread(thread.h) != static_cast<DWORD>(-1), 58); child_cleanup.resumed = true;
    const ULONGLONG started = GetTickCount64(), deadline = started + kCaseMs;
    bool forced = false;
    while (WaitForSingleObject(process.h, 0) == WAIT_TIMEOUT) {
        if (!out.Pump() || !err.Pump() || GetTickCount64() >= deadline) { forced = true; break; }
        Sleep(10);
    }
    DWORD exit_code = 125; if (!forced) Need(GetExitCodeProcess(process.h, &exit_code) != 0, 59);
    forced = forced || !CompleteCaseJob(job.h, deadline, started, out, err, attempt.completion);
    if (forced) { Need(TerminateJobObject(job.h, 125) != 0, 61); WaitForSingleObject(process.h, 5000); }
    const ULONGLONG drain_deadline = GetTickCount64() + 5000;
    while ((!out.eof || !err.eof) && GetTickCount64() < drain_deadline) {
        if (!out.Pump() || !err.Pump()) { forced = true; break; }
        Sleep(10);
    }
    forced = forced || !out.eof || !err.eof;
    out.Finish(); err.Finish();
    Handle stdout_file = Open(out_path, false, user), stderr_file = Open(err_path, false, user);
    std::string stdout_hash, stderr_hash;
    std::string stdout_text = CaseStream(stdout_file.h, out.total, stdout_hash), stderr_text = CaseStream(stderr_file.h, err.total, stderr_hash);
    bool clean = true;
    for (const auto& token : forbidden) clean = clean && stdout_text.find(token) == std::string::npos && stderr_text.find(token) == std::string::npos;
    std::uint64_t checks = 0; bool has_checks = false;
  const bool summary_valid = Summary(spec.name, stdout_text, stderr_text, progress, checks, has_checks);
    passed = !forced && exit_code == 0 && clean && summary_valid;
    if (!passed && clean) {
        // Evidence only: a collection failure never changes this case's own result.
        std::string evidence;
        try {
            evidence = FailedCaseJson(plan.run, manifest_hash, spec.name, stdout_text, stdout_hash, stderr_text, stderr_hash);
            WriteNew(Join(root, "output/failed-case.json"), evidence);
        } catch (const Failure&) {}
        SecureZeroMemory(evidence.data(), evidence.size());
    }
    std::ostringstream result;
  result << "{\"name\":\"" << spec.name << "\",\"pid\":" << raw.dwProcessId << ",\"creation_time\":" << creation_time
         << ",\"identity_confirmed\":true,\"exit\":" << exit_code << ",\"forced\":" << (forced ? "true" : "false")
         << ",\"cleanup_confirmed\":" << (!forced ? "true" : "false") << ",\"infrastructure_failure\":0,\"output_complete\":true"
         << ",\"no_forbidden_output\":" << (clean ? "true" : "false") << ",\"summary_valid\":" << (summary_valid ? "true" : "false")
         << ",\"checks\":" << (summary_valid && has_checks ? std::to_string(checks) : "null")
         << ",\"stdout_bytes\":" << stdout_text.size() << ",\"stderr_bytes\":" << stderr_text.size()
         << ",\"stdout_sha256\":\"" << stdout_hash << "\",\"stderr_sha256\":\"" << stderr_hash
         << "\",\"job_completion\":" << CaseCompletionJson(attempt.completion) << "}";
  SecureZeroMemory(stdout_text.data(), stdout_text.size()); SecureZeroMemory(stderr_text.data(), stderr_text.size());
    return result.str();
}
int Execute(const std::wstring& root, const std::string& manifest_hash) {
    unsigned failure = 0; std::string run, source; std::vector<std::string> results; bool output_owned = false;
    std::vector<AckCompletion> ack_completions;
    try {
        Need(HexString(manifest_hash, 64) && StandardToken(GetCurrentProcess()), 63);
        const DWORD windows_build = NativeWindowsBuild();
        auto user = User(); Handle root_handle = Open(root, true, user.data(), true, true);
        Need(root.size() < 150 && root.find(L"C:\\ce-native-ice-") == 0 && GetDriveTypeW(L"C:\\") == DRIVE_FIXED, 64);
        DWORD flags = 0; std::array<wchar_t, 32> filesystem{};
        Need(GetVolumeInformationByHandleW(root_handle.h, nullptr, 0, nullptr, nullptr, &flags, filesystem.data(), static_cast<DWORD>(filesystem.size())) &&
            (flags & FILE_PERSISTENT_ACLS) && std::wstring(filesystem.data()) == L"NTFS", 65);
        Handle manifest = Open(root + L"\\gate-plan.txt", false, user.data()); Need(Digest(manifest.h) == manifest_hash, 66);
        Plan plan = Parse(Read(manifest.h, 65536)); run = plan.run; source = plan.source;
        Need(root == L"C:\\ce-native-" + Wide(run), 67);
        std::array<wchar_t, 32768> own{}; DWORD own_n = GetModuleFileNameW(nullptr, own.data(), static_cast<DWORD>(own.size()));
        Need(own_n > 0 && own_n < own.size() && std::wstring(own.data()) == root + L"\\ice-native-supervisor.exe", 68);
        bool own_pinned = false; std::vector<Handle> directories;
        for (auto& file : plan.files) {
            size_t separator = 0;
            while ((separator = file.path.find('/', separator)) != std::string::npos) { directories.push_back(Open(Join(root, file.path.substr(0, separator)), true, user.data())); ++separator; }
            file.handle = Open(Join(root, file.path), false, user.data()); LARGE_INTEGER size{};
            Need(GetFileSizeEx(file.handle.h, &size) && size.QuadPart == file.size && Digest(file.handle.h) == file.hash, 69);
            own_pinned = own_pinned || file.path == "ice-native-supervisor.exe";
        }
        Need(own_pinned, 70);
        Directory(root + L"\\output"); output_owned = true; Directory(root + L"\\work"); Directory(root + L"\\fixtures");
        AckSharingControl(root, run, user.data(), ack_completions);
        const std::wstring ack = root + L"\\ack.txt";
        Need(GetFileAttributesW(ack.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND, 71);
        WriteNew(root + L"\\output\\ready.json", "{\"schema\":1,\"run\":\"" + run + "\",\"manifest_sha256\":\"" + manifest_hash +
            "\",\"standard_user\":true,\"windows_build\":" + std::to_string(windows_build) + "}\n");
        const ULONGLONG deadline = GetTickCount64() + 20000; bool acknowledged = false;
        while (GetTickCount64() < deadline) {
            if (GetFileAttributesW(ack.c_str()) != INVALID_FILE_ATTRIBUTES) {
                Handle ack_file = Open(ack, false, user.data()); std::string value = Read(ack_file.h, 128);
                while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
                Need(value == run, 72); acknowledged = true; break;
            }
            Sleep(50);
        }
        Need(acknowledged, 73);
        for (const auto& spec : plan.cases) {
      Attempt attempt; bool passed = false;
      results.push_back(FailedCase(spec, attempt, 91));
      try { results.back() = RunCase(root, spec, user.data(), plan, manifest_hash, passed, attempt); }
      catch (const Failure& f) { results.back() = FailedCase(spec, attempt, f.code); throw; }
      catch (...) { results.back() = FailedCase(spec, attempt, 92); throw; }
      Need(passed, 74);
        }
        for (auto& file : plan.files) Need(Digest(file.handle.h) == file.hash && Private(file.handle.h, user.data(), false), 75);
        Need(Digest(manifest.h) == manifest_hash && Private(root_handle.h, user.data(), true), 76);
    } catch (const Failure& e) { failure = e.code; }
      catch (...) { failure = 99; }
    if (output_owned) {
        try {
            std::ostringstream out;
            out << "{\"schema\":1,\"run\":\"" << run << "\",\"source_commit\":\"" << source << "\",\"manifest_sha256\":\"" << manifest_hash
                << "\",\"standard_user\":true,\"failure\":" << failure << ",\"inputs_unchanged\":" << (failure ? "false" : "true") << ",\"ack_control\":[";
            for (size_t i = 0; i < ack_completions.size(); ++i) { if (i) out << ','; out << AckCompletionJson(ack_completions[i]); }
            out << "],\"cases\":[";
            for (size_t i = 0; i < results.size(); ++i) { if (i) out << ','; out << results[i]; }
            out << "]}\n"; WriteNew(root + L"\\output\\done.json", out.str());
        } catch (...) { return 98; }
    }
    return failure ? 1 : 0;
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 2;
    int result = 2;
    if (argc == 4 && std::wstring(argv[1]) == L"--run") {
        const std::wstring hash(argv[3]);
        if (hash.find_first_not_of(L"0123456789abcdef") == std::wstring::npos) result = Execute(argv[2], std::string(hash.begin(), hash.end()));
    }
    LocalFree(argv); return result;
}
