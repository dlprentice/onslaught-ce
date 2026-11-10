// SPDX-License-Identifier: GPL-3.0-only
#include "ce_network_stl.h"
#include "ce_project.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

const char* PortGameDir();
const char* PortUserDir();

namespace {
const size_t DocumentMax = 8u << 20, PlacementMax = 4096, ScriptMax = 128;
const float CoordinateMax = 1048576.0f;
struct Invalid : std::runtime_error { explicit Invalid(const char* message) : std::runtime_error(message) {} };
void Need(bool condition, const char* message) { if (!condition) throw Invalid(message); }

struct Json {
    enum Kind { Null, Boolean, Number, String, Array, Object } kind = Null;
    std::string text;
    std::vector<Json> array;
    std::map<std::string, Json> object;
};
class Reader {
    const std::string& bytes_; size_t at_ = 0, nodes_ = 0;
    void Space() {
        while (at_ < bytes_.size() && (bytes_[at_] == ' ' || bytes_[at_] == '\t' || bytes_[at_] == '\r' || bytes_[at_] == '\n')) ++at_;
    }
    char Take() { Need(at_ < bytes_.size(), "The project JSON is truncated."); return bytes_[at_++]; }
    unsigned Hex() {
        unsigned value = 0;
        for (unsigned i = 0; i != 4; ++i) {
            unsigned char c = static_cast<unsigned char>(Take());
            Need((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'), "Invalid JSON Unicode escape.");
            value = value * 16 + (c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
        }
        return value;
    }
    static void Utf8(std::string& out, unsigned value) {
        if (value < 128) out.push_back(static_cast<char>(value));
        else if (value < 2048) { out.push_back(static_cast<char>(192 | (value >> 6))); out.push_back(static_cast<char>(128 | (value & 63))); }
        else if (value < 65536) { out.push_back(static_cast<char>(224 | (value >> 12))); out.push_back(static_cast<char>(128 | ((value >> 6) & 63))); out.push_back(static_cast<char>(128 | (value & 63))); }
        else { out.push_back(static_cast<char>(240 | (value >> 18))); out.push_back(static_cast<char>(128 | ((value >> 12) & 63))); out.push_back(static_cast<char>(128 | ((value >> 6) & 63))); out.push_back(static_cast<char>(128 | (value & 63))); }
    }
    std::string String() {
        Need(Take() == '"', "Expected a JSON string."); std::string out;
        for (;;) {
            unsigned char c = static_cast<unsigned char>(Take());
            if (c == '"') return out;
            Need(c >= 32, "A JSON string contains a control byte.");
            if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
            c = static_cast<unsigned char>(Take());
            if (c == '"' || c == '\\' || c == '/') out.push_back(static_cast<char>(c));
            else if (c == 'b') out.push_back('\b'); else if (c == 'f') out.push_back('\f');
            else if (c == 'n') out.push_back('\n'); else if (c == 'r') out.push_back('\r'); else if (c == 't') out.push_back('\t');
            else if (c == 'u') {
                unsigned value = Hex();
                if (value >= 0xd800 && value <= 0xdbff) {
                    Need(Take() == '\\' && Take() == 'u', "A Unicode surrogate is incomplete.");
                    unsigned low = Hex(); Need(low >= 0xdc00 && low <= 0xdfff, "A Unicode surrogate is invalid.");
                    value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
                } else Need(value < 0xdc00 || value > 0xdfff, "A Unicode surrogate is invalid.");
                Utf8(out, value);
            } else throw Invalid("Unsupported JSON escape.");
        }
    }
    Json Value(unsigned depth) {
        Need(depth <= 16 && ++nodes_ <= 65536, "The project JSON is too deeply nested or complex."); Space();
        Need(at_ < bytes_.size(), "The project JSON is truncated."); Json out; char c = bytes_[at_];
        if (c == '{') {
            ++at_; out.kind = Json::Object; Space(); if (at_ < bytes_.size() && bytes_[at_] == '}') { ++at_; return out; }
            for (;;) {
                Space(); std::string key = String(); Space(); Need(Take() == ':', "Expected a JSON field value.");
                Need(out.object.size() < 16 && out.object.count(key) == 0, "A project record has duplicate or excessive fields.");
                out.object.emplace(key, Value(depth + 1)); Space(); c = Take(); if (c == '}') return out;
                Need(c == ',', "Expected a JSON field separator.");
            }
        }
        if (c == '[') {
            ++at_; out.kind = Json::Array; Space(); if (at_ < bytes_.size() && bytes_[at_] == ']') { ++at_; return out; }
            for (;;) { out.array.push_back(Value(depth + 1)); Space(); c = Take(); if (c == ']') return out; Need(c == ',', "Expected a JSON list separator."); }
        }
        if (c == '"') { out.kind = Json::String; out.text = String(); return out; }
        for (const char* literal : {"null", "true", "false"}) {
            size_t n = std::strlen(literal);
            if (bytes_.compare(at_, n, literal) == 0) { at_ += n; out.kind = literal[0] == 'n' ? Json::Null : Json::Boolean; return out; }
        }
        size_t first = at_; if (c == '-') ++at_;
        Need(at_ < bytes_.size(), "Invalid JSON number.");
        if (bytes_[at_] == '0') ++at_;
        else { Need(bytes_[at_] >= '1' && bytes_[at_] <= '9', "Invalid JSON number."); while (at_ < bytes_.size() && bytes_[at_] >= '0' && bytes_[at_] <= '9') ++at_; }
        if (at_ < bytes_.size() && bytes_[at_] == '.') { ++at_; size_t digits = at_; while (at_ < bytes_.size() && bytes_[at_] >= '0' && bytes_[at_] <= '9') ++at_; Need(at_ != digits, "Invalid JSON fraction."); }
        if (at_ < bytes_.size() && (bytes_[at_] == 'e' || bytes_[at_] == 'E')) { ++at_; if (at_ < bytes_.size() && (bytes_[at_] == '+' || bytes_[at_] == '-')) ++at_; size_t digits = at_; while (at_ < bytes_.size() && bytes_[at_] >= '0' && bytes_[at_] <= '9') ++at_; Need(at_ != digits, "Invalid JSON exponent."); }
        out.kind = Json::Number; out.text = bytes_.substr(first, at_ - first); return out;
    }
public:
    explicit Reader(const std::string& bytes) : bytes_(bytes) {}
    Json Read() { Json result = Value(0); Space(); Need(at_ == bytes_.size(), "The project has trailing JSON data."); return result; }
};
void ValidUtf8(const std::string& value) {
    for (size_t i = 0; i < value.size();) {
        unsigned char c = static_cast<unsigned char>(value[i++]); if (c < 128) continue;
        unsigned n = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 9;
        Need(n != 9 && n <= value.size() - i, "The project is not valid UTF-8."); unsigned point = c & ((1u << (6 - n)) - 1);
        for (unsigned j = 0; j < n; ++j) { unsigned char tail = static_cast<unsigned char>(value[i++]); Need((tail & 192) == 128, "The project is not valid UTF-8."); point = (point << 6) | (tail & 63); }
        Need(point >= (n == 1 ? 128u : n == 2 ? 2048u : 65536u) && point <= 0x10ffff && !(point >= 0xd800 && point <= 0xdfff), "The project is not valid UTF-8.");
    }
}
const Json& Field(const Json& record, const char* key) {
    Need(record.kind == Json::Object, "Expected a project object."); auto found = record.object.find(key);
    Need(found != record.object.end(), "A required project field is missing."); return found->second;
}
void Fields(const Json& record, std::initializer_list<const char*> keys) {
    Need(record.kind == Json::Object && record.object.size() == keys.size(), "A project record has missing or unknown fields.");
    for (const char* key : keys) (void)Field(record, key);
}
const std::string& Text(const Json& value, size_t max, bool empty = false, bool multiline = false) {
    Need(value.kind == Json::String && value.text.size() <= max && (empty || !value.text.empty()), "A project text field is invalid or too large.");
    ValidUtf8(value.text);
    for (size_t i = 1; i < value.text.size(); ++i)
        Need(!(static_cast<unsigned char>(value.text[i-1]) == 0xc2 && static_cast<unsigned char>(value.text[i]) >= 0x80 && static_cast<unsigned char>(value.text[i]) <= 0x9f), "A project text field contains a Unicode control character.");
    bool visible = false;
    for (unsigned char c : value.text) { Need(c >= 32 || (multiline && (c == '\r' || c == '\n' || c == '\t')), "A project text field contains a control character."); Need(c != 127, "A project text field contains a control character."); visible |= c > 32; }
    Need(empty || visible, "A project name is blank."); return value.text;
}
const std::string& Id(const Json& value) {
    const std::string& id = Text(value, 96); auto start = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    Need(start(id[0]), "A project ID is invalid.");
    for (char c : id) Need(start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.', "A project ID is invalid.");
    return id;
}
double Number(const Json& value) {
    Need(value.kind == Json::Number, "Expected a project number."); std::istringstream input(value.text); input.imbue(std::locale::classic()); double result = 0;
    input >> std::noskipws >> result; Need(input && input.eof() && std::isfinite(result), "A project number is invalid or non-finite."); return result;
}
void Array(const Json& value, size_t max) { Need(value.kind == Json::Array && value.array.size() <= max, "A project list is invalid or too large."); }
struct Placement { std::string id, source; float x, y, z; bool seen = false; };
struct Project { int level = -1; std::vector<Placement> placements; };
Project Decode(const std::string& bytes) {
    ValidUtf8(bytes); Json root = Reader(bytes).Read();
    Fields(root, {"kind", "version", "coordinates", "source_language", "id", "name", "entry_script", "map", "placements", "scripts"});
    Need(Text(Field(root,"kind"), 64) == "onslaught.ce.mission-project" && Number(Field(root,"version")) == 2, "CE needs a linked version-2 mission project.");
    Need(Text(Field(root,"coordinates"), 64) == "aquila-xyz-z-down" && Text(Field(root,"source_language"), 64) == "bea-msl", "Unsupported project coordinates or source language.");
    Id(Field(root,"id")); Text(Field(root,"name"),256);
    const Json& map = Field(root,"map"); Fields(map, {"mode","level","bindings"});
    Need(Text(Field(map,"mode"),64) == "existing-object-placement", "CE supports existing-object placement mode only.");
    double level = Number(Field(map,"level")); Need(level >= 1 && level <= 9999 && std::floor(level) == level, "The installed map number is invalid."); Project out; out.level = static_cast<int>(level);
    const Json& bindings = Field(map,"bindings"); Array(bindings, PlacementMax); std::map<std::string,std::string> sources; std::set<std::string> named;
    for (const Json& row : bindings.array) {
        Fields(row,{"definition","source_object"}); std::string definition = Text(Field(row,"definition"),256), source = Text(Field(row,"source_object"),96);
        for (unsigned char c : source) Need(c >= 32 && c <= 126, "A named map-object link must use printable ASCII.");
        Need(sources.emplace(definition,source).second && named.insert(source).second, "A definition or named map object is bound more than once.");
    }
    const Json& scripts = Field(root,"scripts"); Array(scripts,ScriptMax); std::set<std::string> scriptIds; size_t total = 0;
    for (const Json& row : scripts.array) {
        Fields(row,{"id","name","source"}); Need(scriptIds.insert(Id(Field(row,"id"))).second,"Duplicate script ID."); Text(Field(row,"name"),256);
        total += Text(Field(row,"source"),1u<<20,true,true).size(); Need(total <= (4u<<20),"Combined source text is too large.");
    }
    auto reference = [&scriptIds](const Json& value) { if (value.kind != Json::Null) Need(scriptIds.count(Id(value)) != 0,"A source link names no script in the project."); };
    reference(Field(root,"entry_script")); const Json& placements = Field(root,"placements"); Array(placements,PlacementMax);
    Need(!placements.array.empty(),"The linked project has no placements."); std::set<std::string> ids, used;
    for (const Json& row : placements.array) {
        Fields(row,{"id","name","definition","position","script"}); Placement p; p.id = Id(Field(row,"id"));
        Need(ids.insert(p.id).second,"Duplicate placement ID."); Text(Field(row,"name"),256); const std::string& definition = Text(Field(row,"definition"),256);
        auto binding = sources.find(definition); Need(binding != sources.end(),"A placement definition has no named map-object binding.");
        Need(used.insert(definition).second,"Existing-object placement does not clone objects; use one placement per definition."); p.source = binding->second;
        const Json& position = Field(row,"position"); Array(position,3); Need(position.array.size() == 3,"A placement needs exactly three coordinates.");
        float* values[] = {&p.x,&p.y,&p.z};
        for (unsigned i = 0; i != 3; ++i) { double coordinate = Number(position.array[i]); Need(std::fabs(coordinate) <= CoordinateMax,"A placement exceeds the coordinate safety bound."); *values[i] = static_cast<float>(coordinate); }
        reference(Field(row,"script")); out.placements.push_back(std::move(p));
    }
    return out;
}
struct State { bool attempted = false, failed = false, began = false, complete = false; Project project; const char* message = "No CE project selected."; };
State& Current() { static State state; return state; }
bool Fail(const char* message) noexcept { State& state = Current(); state.failed = true; state.message = message; std::fprintf(stderr,"CE project: %s\n",message); return false; }
#if !defined(_WIN32)
std::string Directory(const char* path) {
    Need(path && path[0] == '/',"Project launch needs an explicit absolute custom user directory."); char result[PATH_MAX];
    Need(realpath(path,result) != nullptr,"The project custom user directory does not exist."); struct stat info;
    Need(stat(result,&info) == 0 && S_ISDIR(info.st_mode),"The project custom user path is not a directory."); return result;
}
bool Within(const std::string& path, const std::string& root) { return path == root || (path.size() > root.size() && path.compare(0,root.size(),root) == 0 && path[root.size()] == '/'); }
void ProfileRoots(const char* gameRoot, const char* userRoot, size_t gameCapacity, size_t userCapacity) {
    for (char** entry = environ; entry && *entry; ++entry)
        Need(std::strncmp(*entry,"BEA_NET_",8) != 0 && std::strncmp(*entry,"BEA_LOCKSTEP_",13) != 0,
             "Project launches cannot be combined with network startup settings.");
    Need(gameRoot && gameRoot[0] == '/' && userRoot && userRoot[0] == '/',
         "Project launch needs explicit absolute BEA_GAME_DIR and BEA_USER_DIR roots.");
    Need(gameCapacity != 0 && userCapacity != 0 && strnlen(gameRoot,gameCapacity) < gameCapacity && strnlen(userRoot,userCapacity) < userCapacity,
         "A project root spelling exceeds the startup path cache limit.");
    std::string user = Directory(userRoot), game = Directory(gameRoot);
    Need(game != "/" && user != "/" && !Within(user,game) && !Within(game,user),
         "Project launch needs a separate explicit custom user directory outside the game folder.");
    struct stat userInfo; Need(stat(user.c_str(),&userInfo) == 0 && userInfo.st_uid == geteuid() && (userInfo.st_mode & 0022) == 0,
         "The project user directory must be owned by this user and not writable by others.");
}
std::string ReadProject() {
    const char* requestedUser = std::getenv("BEA_USER_DIR");
    ProfileRoots(PortGameDir(),requestedUser,PATH_MAX,PATH_MAX);
    Need(Directory(requestedUser) == Directory(PortUserDir()),"The initialized user root differs from the selected project root.");
    const char* path = std::getenv("BEA_CE_PROJECT"); Need(path && path[0] == '/',"Select an absolute .ceproject file path.");
    int fd = open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK); Need(fd >= 0,"The selected project file cannot be opened safely.");
    struct Close { int fd; ~Close() { close(fd); } } closeFile{fd}; struct stat before,after;
    Need(fstat(fd,&before) == 0 && S_ISREG(before.st_mode) && before.st_size > 0 && static_cast<unsigned long long>(before.st_size) <= DocumentMax,"The selected project must be a bounded regular file.");
    std::string bytes(static_cast<size_t>(before.st_size),'\0'); size_t done = 0;
    while (done < bytes.size()) { ssize_t n = read(fd,&bytes[done],bytes.size()-done); Need(n > 0,"The selected project changed or could not be read."); done += static_cast<size_t>(n); }
    char extra; Need(read(fd,&extra,1) == 0 && fstat(fd,&after) == 0 && before.st_size == after.st_size && before.st_mtime == after.st_mtime && before.st_ctime == after.st_ctime,"The selected project changed while being read."); return bytes;
}
#endif
}
bool PortCeProjectRequested() noexcept { static const bool selected = std::getenv("BEA_CE_PROJECT") != nullptr; return selected; }
bool PortCeProjectProfilePreflight(const char* gameRoot, const char* userRoot, size_t gameCapacity, size_t userCapacity) noexcept {
    if (!PortCeProjectRequested()) return true;
    try {
#if defined(_WIN32)
        (void)gameRoot; (void)userRoot; (void)gameCapacity; (void)userCapacity;
        return Fail("The project launch prototype is currently available on Linux only.");
#else
        ProfileRoots(gameRoot,userRoot,gameCapacity,userCapacity);
        return true;
#endif
    } catch (const Invalid& error) {
        Fail("The project profile preflight refused startup before profile initialization.");
        std::fprintf(stderr,"CE project validation: %s\n",error.what());
    } catch (...) { Fail("Project profile preflight could not complete within the available resources."); }
    return false;
}
// Tool sessions remain active through their return-to-frontend boundary, including failed loads. The
// standalone project fixture may omit native tools; an absent optional symbol cannot enable a live tool.
extern "C" bool PortCeEditorActive(void) __attribute__((weak));
extern "C" bool PortCeMuseumVisitActive(void) __attribute__((weak));
extern "C" bool PortCeEditorBeginWorld(int level, bool base) __attribute__((weak));
extern "C" bool PortCeEditorObserveWorldObject(const char*,size_t,float,float,float) __attribute__((weak));
extern "C" bool PortCeEditorEndWorld(bool base) __attribute__((weak));
extern "C" int CE_BootRequested(void) __attribute__((weak));
extern "C" int CE_SessionActive(void) __attribute__((weak));
extern "C" int CE_LockstepRequested(void) __attribute__((weak));
bool PortCeNetActive() __attribute__((weak));
int PortModsActive() __attribute__((weak));
bool PortCeAuthoringSessionAllowed() noexcept {
    if (PortCeProjectRequested() || (PortCeNetActive && PortCeNetActive()) ||
        (CE_BootRequested && CE_BootRequested()) || (CE_SessionActive && CE_SessionActive()) ||
        (CE_LockstepRequested && CE_LockstepRequested()) || (PortModsActive && PortModsActive())) return false;
#if defined(_WIN32)
    char** environment = _environ;
#else
    char** environment = environ;
#endif
    for (char** entry = environment; entry && *entry; ++entry)
        if (!std::strncmp(*entry,"BEA_NET_",8) || !std::strncmp(*entry,"BEA_LOCKSTEP_",13)) return false;
    return true;
}
bool PortCeProjectCareerAllowed() noexcept {
    return !PortCeProjectRequested() && !(PortCeEditorActive && PortCeEditorActive()) &&
        !(PortCeMuseumVisitActive && PortCeMuseumVisitActive());
}
const char* PortCeProjectMessage() noexcept { return Current().message; }
bool PortCeProjectLoaded() noexcept { return Current().complete && !Current().failed; }
int PortCeProjectExitStatus() noexcept {
    State& state = Current();
    if (state.complete && !state.failed) return 0;
    if (!state.failed) Fail("The linked installed map did not load; the project was not applied.");
    return 1;
}
int PortCeProjectLaunchLevel() noexcept {
    if (!PortCeProjectRequested()) return -1;
    State& state = Current();
    if (state.attempted) return state.failed ? -1 : state.project.level;
    state.attempted = true;
    try {
#if defined(_WIN32)
        Fail("The project launch prototype is currently available on Linux only."); return -1;
#else
        state.project = Decode(ReadProject()); state.message = "Linked project selected; validating the installed map.";
        std::fprintf(stderr,"CE project: existing-object placement; career reads, writes and progression are disabled; authored source text is not executed.\n"); return state.project.level;
#endif
    } catch (const Invalid& error) { // Store fixed, owned messages only; never a document/path/asset string.
        // Invalid messages in this file are static string literals.
        (void)error; Fail("The project or isolated launch configuration is invalid. See the validation reason below.");
        std::fprintf(stderr,"CE project validation: %s\n",error.what());
    } catch (...) { Fail("The project could not be loaded within the available resources."); }
    return -1;
}
bool PortCeProjectBeginWorld(int level, bool base) noexcept {
    if (PortCeEditorActive && PortCeEditorActive())
        return PortCeEditorBeginWorld && PortCeEditorBeginWorld(level,base);
    if (!PortCeProjectRequested()) return true;
    State& state = Current();
    if (!state.attempted || state.failed) return Fail("The project launch was not validated.");
    if (base) return true;
    if (level != state.project.level) return Fail("The loaded map does not match the project's installed-map link.");
    if (state.began) return Fail("A project launch accepts one installed-map load only.");
    state.began = true; return true;
}
bool PortCeProjectApplyPlacement(const char* name, size_t capacity, bool base, float* x, float* y, float* z) noexcept {
    if (PortCeEditorActive && PortCeEditorActive()) {
        if (base) return true;
        return x && y && z && PortCeEditorObserveWorldObject && PortCeEditorObserveWorldObject(name,capacity,*x,*y,*z);
    }
    if (!PortCeProjectRequested() || base) return true;
    State& state = Current();
    if (!state.began || state.failed || state.complete || !name || !x || !y || !z) return Fail("Project placement was called outside its validated map load.");
    const char* end = static_cast<const char*>(std::memchr(name,0,capacity));
    if (!end) return Fail("An installed map object has an invalid bounded name.");
    size_t length = static_cast<size_t>(end-name);
    for (Placement& p : state.project.placements) if (p.source.size() == length && std::memcmp(p.source.data(),name,length) == 0) {
        if (p.seen) return Fail("A named map-object binding matches more than one installed object.");
        p.seen = true; *x = p.x; *y = p.y; *z = p.z; return true;
    }
    return true;
}
bool PortCeProjectEndWorld(bool base) noexcept {
    if (PortCeEditorActive && PortCeEditorActive())
        return PortCeEditorEndWorld && PortCeEditorEndWorld(base);
    if (!PortCeProjectRequested() || base) return true;
    State& state = Current();
    if (!state.began || state.failed || state.complete) return Fail("The project map load did not complete normally.");
    for (const Placement& p : state.project.placements) if (!p.seen) return Fail("A bound named object was not found in the installed map; the project cannot start.");
    state.complete = true; state.message = "Linked existing-object placements loaded. Terrain/assets are supplied by the installed map; source text is not executed.";
    std::fprintf(stderr,"CE project: applied %zu existing-object placement(s) in memory.\n",state.project.placements.size()); return true;
}

namespace {
void DocumentError(char* out, size_t capacity, const char* text) noexcept {
    if (out && capacity) std::snprintf(out, capacity, "%s", text);
}
Json StringValue(const std::string& text) { Json value; value.kind = Json::String; value.text = text; return value; }
Json NumberValue(double number) {
    Need(std::isfinite(number), "A project coordinate is not finite.");
    Json value; value.kind = Json::Number;
    std::ostringstream text; text.imbue(std::locale::classic());
    text << std::setprecision(std::numeric_limits<double>::max_digits10) << number;
    value.text = text.str(); return value;
}
Json ObjectValue() { Json value; value.kind = Json::Object; return value; }
Json ArrayValue() { Json value; value.kind = Json::Array; return value; }
void EncodeString(const std::string& text, std::string& out) {
    static const char hex[] = "0123456789abcdef";
    out += '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += char(c);
    }
    out += '"';
}
void EncodeValue(const Json& value, std::string& out) {
    switch (value.kind) {
    case Json::Null: out += "null"; break;
    case Json::String: EncodeString(value.text, out); break;
    case Json::Number: out += value.text; break;
    case Json::Array:
        out += '[';
        for (size_t i = 0; i != value.array.size(); ++i) { if (i) out += ','; EncodeValue(value.array[i], out); }
        out += ']'; break;
    case Json::Object: {
        out += '{'; bool first = true;
        for (const auto& row : value.object) {
            if (!first) out += ',';
            first = false;
            EncodeString(row.first, out);
            out += ':';
            EncodeValue(row.second, out);
        }
        out += '}'; break;
    }
    default: throw Invalid("A project contains an unsupported value.");
    }
    Need(out.size() < DocumentMax, "The project document exceeds the supported size.");
}
std::string Encode(const Json& root) { std::string bytes; EncodeValue(root, bytes); bytes += '\n'; return bytes; }
std::string PositionName(const char* name) {
    Need(name != nullptr, "An edited object is missing its source identity.");
    size_t length = 0; while (length <= 96 && name[length]) ++length;
    Need(length <= 96, "An edited source identity exceeds its bound.");
    std::string value(name, length);
    Text(StringValue(value), 96);
    for (unsigned char c : value) Need(c >= 32 && c <= 126, "An edited source identity must use printable ASCII.");
    return value;
}
Json Coordinates(const CeProjectPosition& value) {
    Json result = ArrayValue();
    for (float coordinate : {value.x, value.y, value.z}) {
        Need(std::isfinite(coordinate) && std::fabs(coordinate) <= CoordinateMax, "An edited position exceeds the coordinate bound.");
        result.array.push_back(NumberValue(coordinate));
    }
    return result;
}
}
struct CeProjectDocument { Json root; Project project; };
CeProjectDocument* PortCeProjectDocumentNew(int level, const char* name, char* error, size_t capacity) noexcept {
    DocumentError(error, capacity, "");
    try {
        Need(level >= 1 && level <= 9999, "Select a valid installed map number.");
        Need(name != nullptr, "The project needs a name.");
        size_t length = 0; while (length <= 256 && name[length]) ++length;
        Need(length <= 256, "The project name is too long."); Json title = StringValue(std::string(name, length)); Text(title,256);
        Json root = ObjectValue(); root.object["kind"] = StringValue("onslaught.ce.mission-project");
        root.object["version"] = NumberValue(2); root.object["coordinates"] = StringValue("aquila-xyz-z-down");
        root.object["source_language"] = StringValue("bea-msl"); root.object["id"] = StringValue("native_level_" + std::to_string(level));
        root.object["name"] = std::move(title); root.object["entry_script"] = Json(); root.object["scripts"] = ArrayValue();
        root.object["placements"] = ArrayValue(); Json map = ObjectValue();
        map.object["mode"] = StringValue("existing-object-placement"); map.object["level"] = NumberValue(level);
        map.object["bindings"] = ArrayValue(); root.object["map"] = std::move(map);
        CeProjectDocument* document = new CeProjectDocument(); document->root = std::move(root); document->project.level = level;
        return document;
    } catch (const Invalid& failure) { DocumentError(error,capacity,failure.what()); }
      catch (...) { DocumentError(error,capacity,"The project could not be created within available resources."); }
    return nullptr;
}
CeProjectDocument* PortCeProjectDocumentRead(const char* bytes, size_t size, char* error, size_t capacity) noexcept {
    DocumentError(error,capacity,"");
    try {
        Need(bytes && size && size <= DocumentMax, "Select a bounded, nonempty project document.");
        std::string input(bytes,size); Project decoded = Decode(input); Json root = Reader(input).Read();
        CeProjectDocument* document = new CeProjectDocument(); document->root = std::move(root); document->project = std::move(decoded);
        return document;
    } catch (const Invalid& failure) { DocumentError(error,capacity,failure.what()); }
      catch (...) { DocumentError(error,capacity,"The project could not be opened within available resources."); }
    return nullptr;
}
void PortCeProjectDocumentDelete(CeProjectDocument* document) noexcept { delete document; }
int PortCeProjectDocumentLevel(const CeProjectDocument* document) noexcept { return document ? document->project.level : -1; }
size_t PortCeProjectDocumentCount(const CeProjectDocument* document) noexcept { return document ? document->project.placements.size() : 0; }
bool PortCeProjectDocumentPosition(const CeProjectDocument* document, size_t index, CeProjectPosition* position) noexcept {
    if (!document || !position || index >= document->project.placements.size()) return false;
    const Placement& value = document->project.placements[index];
    *position = CeProjectPosition{value.source.c_str(),value.x,value.y,value.z}; return true;
}
bool PortCeProjectDocumentUpdate(CeProjectDocument* document, const CeProjectPosition* positions, size_t count,
                                  char* error, size_t capacity) noexcept {
    DocumentError(error,capacity,"");
    try {
        Need(document && positions && count > 0 && count <= PlacementMax, "Select a bounded set of object placements.");
        Json root = document->root;
        Json& bindings = root.object.at("map").object.at("bindings"); Json& placements = root.object.at("placements");
        std::map<std::string,std::string> definitions; std::set<std::string> usedDefinitions, usedIds, edited;
        std::map<std::string,size_t> placed;
        for (const Json& row : bindings.array) {
            definitions.emplace(Field(row,"source_object").text,Field(row,"definition").text);
            usedDefinitions.insert(Field(row,"definition").text);
        }
        for (size_t i = 0; i != placements.array.size(); ++i) {
            const Json& row = placements.array[i]; placed.emplace(Field(row,"definition").text,i); usedIds.insert(Field(row,"id").text);
        }
        unsigned serial = 1;
        for (size_t i = 0; i != count; ++i) {
            const std::string source = PositionName(positions[i].source);
            Need(edited.insert(source).second,"An edited object source occurs more than once.");
            Json coordinates = Coordinates(positions[i]); auto binding = definitions.find(source);
            if (binding == definitions.end()) {
                std::string definition;
                do { definition = "native.object." + std::to_string(serial++); } while (usedDefinitions.count(definition));
                Json row = ObjectValue(); row.object["definition"] = StringValue(definition); row.object["source_object"] = StringValue(source);
                bindings.array.push_back(std::move(row)); binding = definitions.emplace(source,definition).first; usedDefinitions.insert(definition);
            }
            const auto existing = placed.find(binding->second);
            if (existing != placed.end()) placements.array[existing->second].object.at("position") = std::move(coordinates);
            else {
                std::string id; do { id = "native_object_" + std::to_string(serial++); } while (usedIds.count(id));
                Json row = ObjectValue(); row.object["id"] = StringValue(id); row.object["name"] = StringValue(source);
                row.object["definition"] = StringValue(binding->second); row.object["position"] = std::move(coordinates); row.object["script"] = Json();
                placed.emplace(binding->second,placements.array.size()); placements.array.push_back(std::move(row)); usedIds.insert(id);
            }
        }
        const std::string encoded = Encode(root); Project decoded = Decode(encoded);
        document->root = std::move(root); document->project = std::move(decoded); return true;
    } catch (const Invalid& failure) { DocumentError(error,capacity,failure.what()); }
      catch (...) { DocumentError(error,capacity,"The edited project could not be retained within available resources."); }
    return false;
}
size_t PortCeProjectDocumentWrite(const CeProjectDocument* document, char* bytes, size_t capacity,
                                  char* error, size_t errorCapacity) noexcept {
    DocumentError(error,errorCapacity,"");
    try {
        Need(document != nullptr,"No project is open."); const std::string encoded = Encode(document->root); (void)Decode(encoded);
        const size_t required = encoded.size() + 1;
        if (!bytes) return required;
        Need(capacity >= required,"The output buffer is too small for the complete project.");
        std::memcpy(bytes,encoded.c_str(),required); return required;
    } catch (const Invalid& failure) { DocumentError(error,errorCapacity,failure.what()); }
      catch (...) { DocumentError(error,errorCapacity,"The project could not be serialized within available resources."); }
    return 0;
}
