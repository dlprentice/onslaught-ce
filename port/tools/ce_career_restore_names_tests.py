#!/usr/bin/env python3
"""Exercise the native filename-semantic adapter on authored inputs.

The native predicate/table is extracted only into ignored local test output.
No original strings or source excerpts are printed by this test.
"""
from pathlib import Path
import hashlib
import json
import re
import resource
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
OUT = ROOT / 'local-data/port/ce-career-restore-names-tests'
OUT.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix='run-', dir=OUT))

def run(args, name):
    result = subprocess.run(args, cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (work / (name + '.private.log')).write_bytes(result.stdout)
    if result.returncode:
        raise RuntimeError(name + ' failed; private output retained at ' + str(work))
    return result

def function(source, name):
    at = source.index(name)
    start = source.rfind('\n', 0, at) + 1
    brace = source.index('{', at)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

def declaration(source, name):
    match = re.search(r'^\s*(?:static\s+)?(?:const\s+)?char\s+' + name + r'\s*\[', source, re.M)
    assert match, name
    return source[match.start():source.index(';', match.end()) + 1]

native = ROOT / 'src/FEPSaveGame.cpp'
(work / 'FEPSaveGame.cpp').write_bytes(native.read_bytes())
run(['patch', '--batch', '--forward', '-p1', '-i', str(ROOT / 'port/patches/0051-ce-cheat-switches-FEPSaveGame.patch')], 'patch')
patched = (work / 'FEPSaveGame.cpp').read_text()
domain = int(re.search(r'\bcheatname\s*\[\s*(\d+)\s*\]', patched)[1])
capacity = int(re.search(r'\bmSaveGameName\s*\[\s*(\d+)\s*\]', (ROOT / 'src/FEPSaveGame.h').read_text())[1])
adapter = (ROOT / 'port/game/ce_career_restore_names.cpp').read_text()
assert int(re.search(r'kNativeNameChecks\s*=\s*(\d+)', adapter)[1]) == domain
literal_callers = []
for file in (ROOT / 'src').glob('*.cpp'):
    literal_callers += [int(a) for a in re.findall(r'IsCheatActive\s*\(\s*(\d+)\s*\)', file.read_text(errors='replace'))]
assert all(0 <= value < domain for value in literal_callers)
header = '''#pragma once
#include <cstdint>
#include <cstring>
#include <string>
using WCHAR = uint16_t;
using BOOL = int;
constexpr BOOL TRUE = 1, FALSE = 0;
class CFEPSaveGame { public: WCHAR mSaveGameName[CAPACITY] = {}; BOOL IsCheatActive(int); };
struct Frontend { CFEPSaveGame mFEPSaveGame; };
struct CliParams { bool mAutoConfigTest = false; };
extern Frontend FRONTEND;
extern CliParams CLIPARAMS;
extern bool gPortCheatNameOnly;
extern char gAllCheatsEnabled;
extern bool PortCeProjectCareerAllowed() noexcept;
extern bool CE_CheatOn(int);
char* FromWCHAR(const WCHAR*);
'''.replace('CAPACITY', str(capacity))
(work / 'common.h').write_text(header)
(work / 'frontend.h').write_text('#include \"common.h\"\n')
(work / 'CLIParams.h').write_text('#include \"common.h\"\n')
predicate = function(patched, 'CFEPSaveGame::IsCheatActive(')
actual = '#include "common.h"\n#include <cstring>\n' + declaration(patched, 'cheatname') + '\n' + declaration(patched, 'cheatkey') + '''
std::string observedNeedle;
static const char* observe(const char* haystack, const char* needle) {
    observedNeedle = needle;
    return std::strstr(haystack, needle);
}
#define strstr observe
''' + predicate + '\n'
(work / 'native-predicate.private.cpp').write_text(actual)
# Preserve the actual product translation unit; only its include resolves to the tiny fixture.
(work / 'adapter.private.cpp').write_text(adapter.replace('"../platform/common/save_names.h"', '"save_names.h"'))
driver = r'''#include "common.h"
#include "save_names.h"
#include <cstdio>
#include <cstdlib>
#include <string>
Frontend FRONTEND;
CliParams CLIPARAMS;
bool gPortCheatNameOnly = false;
char gAllCheatsEnabled = 0;
bool allowed = true;
bool forcedSetting = true;
extern std::string observedNeedle;
bool PortCeProjectCareerAllowed() noexcept { return allowed; }
bool CE_CheatOn(int) { return forcedSetting; }
char* FromWCHAR(const WCHAR* w) { static char b[CAPACITY]; unsigned i=0; for (; i<CAPACITY-1 && w[i]; ++i) b[i]=static_cast<char>(w[i]); b[i]=0; return b; }
bool PortCeRestoreNamesCompatible(const char*, const char*);
static unsigned checks = 0;
static void require(bool value) { if (!value) { std::fprintf(stderr,"semantic check %u failed\n",checks+1); std::exit(1); } ++checks; }
static void unchanged(const WCHAR* before, bool namesOnly) {
    require(std::memcmp(before, FRONTEND.mFEPSaveGame.mSaveGameName, sizeof(FRONTEND.mFEPSaveGame.mSaveGameName)) == 0);
    require(gPortCheatNameOnly == namesOnly);
}
static void trial(const char* source, const char* target, bool expected) {
    WCHAR before[CAPACITY]; std::memcpy(before, FRONTEND.mFEPSaveGame.mSaveGameName, sizeof(before));
    bool prior = gPortCheatNameOnly;
    require(PortCeRestoreNamesCompatible(source,target) == expected); unchanged(before,prior);
}
int main() {
    for (unsigned i=0;i<CAPACITY;++i) FRONTEND.mFEPSaveGame.mSaveGameName[i]=static_cast<WCHAR>(0x300+i);
    trial("Amber.bes","Amber restored.bes",true);
    gPortCheatNameOnly=true;trial("Amber.bes","Amber restored 2.bes",true);gPortCheatNameOnly=false;
    trial(nullptr,"Amber restored.bes",false);trial("Amber.bes",nullptr,false);
    trial("Amber","Amber restored.bes",false);trial("Amber.bes","../Amber.bes",false);
    allowed=false;trial("Amber.bes","Amber restored.bes",false);allowed=true;
    gAllCheatsEnabled=1;trial("Amber.bes","Amber restored.bes",false);require(gAllCheatsEnabled==1);gAllCheatsEnabled=0;
    CLIPARAMS.mAutoConfigTest=true;trial("Amber.bes","Amber restored.bes",false);require(CLIPARAMS.mAutoConfigTest);CLIPARAMS.mAutoConfigTest=false;
    // Obtain each real predicate key locally through its actual substring call, not a guessed token list.
    for (unsigned i=0;i<DOMAIN;++i) {
        WCHAR saved[CAPACITY];std::memcpy(saved,FRONTEND.mFEPSaveGame.mSaveGameName,sizeof(saved));
        require(PortSaveNameUtf16("Amber.bes",FRONTEND.mFEPSaveGame.mSaveGameName,CAPACITY)==0);
        gPortCheatNameOnly=true;observedNeedle.clear();FRONTEND.mFEPSaveGame.IsCheatActive(i);
        std::string key=observedNeedle;gPortCheatNameOnly=false;
        std::memcpy(FRONTEND.mFEPSaveGame.mSaveGameName,saved,sizeof(saved));
        require(!key.empty());std::string source=key+".bes", same=key+" restored.bes";
        WCHAR sourceUnits[CAPACITY]={}, targetUnits[CAPACITY]={};
        unsigned sourceError=PortSaveNameUtf16(source.c_str(),sourceUnits,CAPACITY);
        unsigned targetError=PortSaveNameUtf16(same.c_str(),targetUnits,CAPACITY);
        if (sourceError) {
            // Internal keys that cannot be a save leaf must still refuse without state changes.
            trial(source.c_str(),"Amber restored.bes",false);
        } else {
            trial(source.c_str(),same.c_str(),targetError==0);
            trial(source.c_str(),"Amber restored.bes",false);
        }
    }
    std::printf("{\"checks\":%u,\"native_domain\":true,\"state_preserved\":true}\n",checks);
}
'''.replace('CAPACITY', str(capacity)).replace('DOMAIN', str(domain))
(work / 'driver.cpp').write_text(driver)
command = ['c++','-std=c++17','-O0','-Wall','-Wextra','-I'+str(work),'-I'+str(ROOT / 'port/platform/common'),str(work/'driver.cpp'),str(work/'native-predicate.private.cpp'),str(work/'adapter.private.cpp'),str(ROOT/'port/platform/common/save_names.cpp'),'-o',str(work/'test')]
run(command,'compile')
result=run([str(work/'test')],'test')
report=json.loads(result.stdout)
report.update({'native_predicate_sha256':hashlib.sha256(predicate.encode()).hexdigest(),'adapter_sha256':hashlib.sha256((ROOT/'port/game/ce_career_restore_names.cpp').read_bytes()).hexdigest(),'fixture_limits':['Frontend/CLIPARAMS are authored stand-ins; actual native page compile and runtime remain separate.','ASCII FromWCHAR stand-in; full filename decoding is actual production PortSaveNameUtf16.'],'original_data_emitted':False})
(work/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'ok':True,'checks':report['checks'],'output':str(work)}))
