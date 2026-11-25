// SPDX-License-Identifier: GPL-3.0-only
// Real patched interpreter/replacement bodies, with authored allocator, game, object and reader boundaries.
#include <algorithm>
#include <cstdio>
#include <new>
#include <string>
#include <vector>

typedef int BOOL;
static const int TRUE = 1, FALSE = 0, OP_RETURN = 23, MT_SCRIPT = 0, INIT_SCRIPT = 1, START_OF_FRAME = 0;
static const int SCRIPT_EVENT_INIT = 0, GAME_STATE_LEVEL_LOST = 9, MAX_INSTRUCTIONS_PER_RUN = 10000;
static const float NEXT_FRAME = -1.f;
static int invalid_accesses = 0, inits_posted = 0, log_errors = 0;
static bool supplied = true;
void *operator new(std::size_t n, int) { return ::operator new(n); }
void operator delete(void *p, int) noexcept { ::operator delete(p); }
struct Log { void AddMessage(const char *, ...) { ++log_errors; } void AddString(const char *) { ++log_errors; } };
static Log LOG;
struct Game { int GetCurrentLevel() const { return 888; } int GetGameState() const { return 0; } };
static Game GAME;
bool PortModLevelSupplied(int level) { return supplied && level == 888; }

struct Observer { virtual ~Observer() {} virtual void TargetGone() = 0; };
struct CMonitor {
    std::vector<Observer *> readers;
    void NotifyGone() { auto snapshot = readers; readers.clear(); for (Observer *reader : snapshot) reader->TargetGone(); }
    virtual ~CMonitor() { NotifyGone(); }
    void Add(Observer *p) { readers.push_back(p); }
    void Remove(Observer *p) { readers.erase(std::remove(readers.begin(), readers.end(), p), readers.end()); }
};
template<class T> struct CActiveReader : Observer {
    T *value = nullptr;
    ~CActiveReader() { SetReader(nullptr); }
    void SetReader(T *p) { if (value) value->Remove(this); value = p; if (value) value->Add(this); }
    T *ToRead() const { return value; }
    void TargetGone() override { value = nullptr; }
};
struct CDataType {
    explicit CDataType(const char *v = "") : text(v) {}
    virtual ~CDataType() {}
    virtual char *GetString() { return const_cast<char *>(text.c_str()); }
    std::string text;
};
struct CIntDataType : CDataType { explicit CIntDataType(int) {} };
struct CDataStack {
    std::vector<CDataType *> values;
    int mNumValues = 0;
    ~CDataStack() { Clear(); }
    CDataType *Pop() { CDataType *v = values.back(); values.pop_back(); --mNumValues; return v; }
    void Push(CDataType *v) { values.push_back(v); ++mNumValues; }
    void RemoveTop() { delete values.back(); values.pop_back(); --mNumValues; }
    void Clear() { while (mNumValues) RemoveTop(); }
    int Size() const { return mNumValues; }
};
struct CComplexThing;
struct IScript;
struct CEventFunction;
struct SEventParam { void *mEvent = nullptr; };
template<class T> struct SPtrSet {
    std::vector<T *> values;
    bool *destroyed = nullptr;
    size_t position = 0;
    T *First() { if (destroyed && *destroyed) ++invalid_accesses; position = 0; return values.empty() ? nullptr : values[0]; }
    T *Next() { return ++position < values.size() ? values[position] : nullptr; }
};
template<class T> struct ListIterator {
    explicit ListIterator(SPtrSet<T> *set) : set(set) {}
    T *First() { return set->First(); }
    T *Next() { return set->Next(); }
    SPtrSet<T> *set;
};
#define FOR_ALL_ITEMS_IN(A,B) for ((B) = (A).First(); (B) != nullptr; (B) = (A).Next())
struct CEventFunction { SPtrSet<SEventParam> mParams; };
struct ScriptEvents { void *RegisterEventListener(SEventParam *, CEventFunction *) { return nullptr; } };
static ScriptEvents SCRIPT_EVENT_NB;
struct CVM;
struct CSymbolTable {};
struct CAsmInstruction {
    virtual ~CAsmInstruction() {}
    virtual int GetOpcode() const = 0;
    virtual void Execute(CVM *, CDataStack *, CSymbolTable *) = 0;
};
struct CMissionScriptObjectCode {
    IScript *mScript = nullptr;
    bool destroyed = false, mInitRun = true;
    int mUnknown5c = 0, mUnknown60 = 0, mEventPCs[13] = {};
    std::vector<CAsmInstruction *> mInstructions;
    CSymbolTable *mSymbols = nullptr;
    SPtrSet<CEventFunction> mEventFunctions;
    CMissionScriptObjectCode() { mEventFunctions.destroyed = &destroyed; }
};
struct CVM {
    CMissionScriptObjectCode *mCode = nullptr;
    CDataStack mStack;
    int mRunning = FALSE, mPC = 0, mFlags = 0, mStackBase = 0, mStop = FALSE, mCallDepth = 0;
    void CallEvent(CMissionScriptObjectCode *, int, CDataType **, int);
    void CallFunction(CMissionScriptObjectCode *, int, CDataType **, int);
    void Run();
    void Clean() { mStack.Clear(); }
};
static CVM MISSION_SCRIPT_VM;
struct CComplexThing : CMonitor {
    IScript *mMissionScript = nullptr;
    IScript *GetMissionScript() { return mMissionScript; }
    void SetScript(char *name);
};
struct WatchedThing {
    CComplexThing *value = nullptr;
    bool *alive = nullptr;
    operator CComplexThing *() const { return value; }
    CComplexThing *operator->() const { return value; }
    bool operator!() const { return !value; }
    WatchedThing &operator=(const WatchedThing &other) {
        if (alive && !*alive) ++invalid_accesses;
        value = other.value;
        return *this;
    }
};
struct IScript : CMonitor {
    IScript(CComplexThing *owner, CMissionScriptObjectCode *code) : mCode(code) {
        mThing.value = owner; mThing.alive = &alive;
        mDefaultThing.value = owner; mDefaultThing.alive = &alive;
        code->mScript = this;
    }
    ~IScript() override { alive = false; mCode->destroyed = true; }
    // Keep test storage after logical deletion, to observe the defect without accessing freed storage.
    static void operator delete(void *) noexcept {}
    static void *operator new(std::size_t n, int) { return ::operator new(n); }
    static void operator delete(void *p, int) noexcept { ::operator delete(p); }
    void SetScript(CDataType **args, int num_args, CDataType **result);
    void Init();
    bool alive = true;
    CMissionScriptObjectCode *mCode;
    WatchedThing mThing, mDefaultThing;
    CVM **mVM = nullptr;
};
struct World {
    CMissionScriptObjectCode successor;
    std::string requested;
    CMissionScriptObjectCode *GetCopyOfScript(char *name) { requested = name; return &successor; }
};
static World WORLD;
struct EventManager { void AddEvent(int, CComplexThing *, float, int) { ++inits_posted; } };
static EventManager EVENT_MANAGER;
struct SScriptFunction { const char *mName; void (IScript::*mFunction)(CDataType **, int, CDataType **); };
static SScriptFunction gScriptFunctions[] = {{"SetScript", &IScript::SetScript}};
static CDataType *gScriptFunctionArgs[7];
struct CInstructionOP_CALL : CAsmInstruction {
    int mAttr = 1 << 8;
    int GetOpcode() const override { return 24; }
    void Execute(CVM *, CDataStack *, CSymbolTable *) override;
};
struct PushName : CAsmInstruction {
    explicit PushName(const char *n) : name(n) {}
    int GetOpcode() const override { return 5; }
    void Execute(CVM *, CDataStack *stack, CSymbolTable *) override { stack->Push(new CDataType(name)); }
    const char *name;
};
struct Return : CAsmInstruction {
    int GetOpcode() const override { return OP_RETURN; }
    void Execute(CVM *, CDataStack *, CSymbolTable *) override {}
};
#include "guard_self_replace_methods.inc"

static bool check(bool b, const char *what) { if (!b) std::fprintf(stderr, "FAIL: %s\n", what); return b; }
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    CComplexThing owner;
    CMissionScriptObjectCode code;
    IScript *script = new (MT_SCRIPT) IScript(&owner, &code);
    owner.mMissionScript = script;
    PushName push(mode == "blank" ? "" : "next-script");
    CInstructionOP_CALL call;
    Return ret;
    code.mInstructions = {&push, &call, &ret};
    code.mEventPCs[SCRIPT_EVENT_INIT] = 0;
    if (mode == "init-code" || mode == "function-init") { code.mInitRun = false; code.mUnknown5c = 1; }
    if (mode == "stock") supplied = false;

    if (mode == "owner-cancel" || mode == "script-cancel" || mode == "wrong-vm" || mode == "duplicate") {
        MISSION_SCRIPT_VM.mCode = &code;
        MISSION_SCRIPT_VM.mRunning = TRUE;
        if (!check(PortCeDeferSelfReplacement(script, "next-script"), "mod self-replacement must be deferred")) return 1;
        if (mode == "owner-cancel") owner.NotifyGone();
        if (mode == "script-cancel") { delete script; owner.mMissionScript = nullptr; }
        if (mode == "wrong-vm") {
            CVM other;
            if (!check(!PortCeCompleteSelfReplacement(&other) && script->alive && !code.destroyed,
                       "another VM cannot consume the request")) return 1;
        }
        if (mode == "duplicate") {
            if (!check(PortCeDeferSelfReplacement(script, "wrong-script") && log_errors == 1,
                       "a second request is handled without deleting the active script")) return 1;
        }
        if (!check(PortCeCompleteSelfReplacement(&MISSION_SCRIPT_VM) && !MISSION_SCRIPT_VM.mRunning &&
                   !MISSION_SCRIPT_VM.mCode && MISSION_SCRIPT_VM.mStack.mNumValues == 0,
                   "completion retires only the original VM")) return 1;
        if (mode == "owner-cancel")
            return check(script->alive && owner.mMissionScript == script && inits_posted == 0,
                         "a retired owner receives no replacement") ? 0 : 1;
        if (mode == "script-cancel")
            return check(code.destroyed && owner.mMissionScript == nullptr && inits_posted == 0,
                         "a deleted script receives no replacement") ? 0 : 1;
        return check(code.destroyed && owner.mMissionScript != script && WORLD.requested == "next-script" &&
                     inits_posted == 1 && invalid_accesses == 0,
                     "the original request survives the duplicate or other-VM probe") ? 0 : 1;
    }

    if (mode == "other-target") {
        CComplexThing other;
        CMissionScriptObjectCode otherCode;
        IScript *otherScript = new (MT_SCRIPT) IScript(&other, &otherCode);
        other.mMissionScript = otherScript;
        script->mThing.value = &other;
        MISSION_SCRIPT_VM.CallEvent(&code, SCRIPT_EVENT_INIT, nullptr, 0);
        return check(!code.destroyed && otherCode.destroyed && other.mMissionScript != otherScript &&
                     owner.mMissionScript == script && MISSION_SCRIPT_VM.mCode == &code &&
                     WORLD.requested == "next-script" && inits_posted == 1 && invalid_accesses == 0,
                     "another target uses the original synchronous replacement") ? 0 : 1;
    }

    if (mode == "native-init") script->Init();
    else if (mode == "function-init") MISSION_SCRIPT_VM.CallFunction(&code, 0, nullptr, 0);
    else if (mode == "event" || mode == "blank" || mode == "init-code" || mode == "stock")
        MISSION_SCRIPT_VM.CallEvent(&code, SCRIPT_EVENT_INIT, nullptr, 0);
    else return 2;

    if (mode == "stock")
        return check(code.destroyed && invalid_accesses == 1 && MISSION_SCRIPT_VM.mCode == &code,
                     "stock path preserves original inline replacement") ? 0 : 1;
    const bool blank = mode == "blank";
    return check(code.destroyed && invalid_accesses == 0 && MISSION_SCRIPT_VM.mCode == nullptr &&
                 !MISSION_SCRIPT_VM.mRunning && MISSION_SCRIPT_VM.mStack.mNumValues == 0 &&
                 (blank ? owner.mMissionScript == nullptr && inits_posted == 0 :
                          owner.mMissionScript != nullptr && owner.mMissionScript != script &&
                          WORLD.requested == "next-script" && inits_posted == 1) &&
                 ((mode != "init-code" && mode != "function-init") || !code.mInitRun),
                 "self-replacement must finish only after the native call and retire its VM") ? 0 : 1;
}
