#!/usr/bin/env python3
"""Exercise the delivery identity, squad and cockpit gates without a running game."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

GAME = Path(__file__).resolve().parents[1] / 'game'


def definition(file, signature):
    src = (GAME / file).read_text()
    start = src.index(signature)
    body = src.index('{', start)
    depth = 1
    for pos in range(body + 1, len(src)):
        depth += (src[pos] == '{') - (src[pos] == '}')
        if depth == 0:
            return src[start:pos + 1]
    raise ValueError(signature)


HARNESS = r'''
#include <algorithm>
#include <cstring>
#include <vector>
#include <cstdlib>
#include <cstdio>
struct CSquad {int ai=11;int GetAIState(){return ai;}} sq, otherSquad;
struct CThing { int number=17, kind=1; int GetThingType(){return kind;} int GetThingNumber(){return number;} };
struct UnitData { const char *mName="test-vehicle"; } data, otherData{"other-class"};
struct CUnit: CThing {
    int team=4, ai=0, writes=0; int GetAIState(){return ai;} void SetAIState(int v){++writes;ai=v;} bool dying=false; float life=10; UnitData *type=&data; CSquad *squad=&sq;
    bool IsDying(){return dying;} float GetLife(){return life;} int GetAllegiance(){return team;}
    UnitData *GetUnitData(){return type;} CSquad *GetSquad(){return squad;}
} actor, replacement;
constexpr int CE_FACTORY_READY=3, THING_TYPE_UNIT=1, CE_PLAYUNIT_CLASS_TANK=5;
constexpr int AI_OFF=0; bool kept=true;bool FootKeepsAssignment(CUnit *){return kept;}
const char *kVehicleType="test-vehicle";
bool MatchType(const char *a,const char *b){return a&&b&&std::strcmp(a,b)==0;}
struct {bool active; int stage,product,allegiance; CUnit *vehicle;} job;
struct { std::vector<CThing*> all; std::vector<CThing*> &GetThingNB(){return all;} } WORLD;
template<class T> struct ListIterator {
    std::vector<T*> *items; size_t index=0;
    ListIterator(std::vector<T*> *in):items(in){}
    T *First(){index=0;return Next();} T *Next(){return index<items->size()?(*items)[index++]:nullptr;}
};
struct Rec {bool alive=true,normal=true;std::vector<CUnit*> members;} rec;
struct CeCmdSquad {bool commandable;}; bool ordinaryOrderAllowed=true, recordPresent=true;
int CeCmdDrawnSquad(const CSquad *s){return s==&sq?7:-1;}
Rec *ByOrdinal(int i){return recordPresent&&i==7?&rec:nullptr;}
int Units(Rec &r,CUnit **out,int max){int n=std::min<int>(r.members.size(),max);std::copy_n(r.members.begin(),n,out);return n;}
int PlayerAllegiance(){return actor.team;}
void Describe(int,Rec &,int,CeCmdSquad &i){i.commandable=ordinaryOrderAllowed;}
struct CPlayer{} pilot; struct CBattleEngine{} engine; struct CCONTROLLER{} ctl;
bool gConfigured=false,gWanted=true,gate=true,seat=false,map=false,ready=true;int calls=0,notices=0,kind=CE_PLAYUNIT_CLASS_TANK;
void ReadConfiguration(){gConfigured=true;}bool Allowed(bool o){return o&&gate;}bool CePlayUnitOwnsPilot(){return seat;}
bool PortCeCmdMapOpen(){return map;}bool Ready(CPlayer**p,CBattleEngine**e,CCONTROLLER**c){*p=&pilot;*e=&engine;*c=&ctl;return ready;}
int Facts(CUnit *,CBattleEngine *){return kind;}int CePlayUnitClassFor(int k){return k;}
bool Take(CPlayer*p,CBattleEngine*e,CCONTROLLER*c,CUnit*u,int k){++calls;seat=p==&pilot&&e==&engine&&c==&ctl&&u==&actor&&k==CE_PLAYUNIT_CLASS_TANK;return seat;}
void CePlayUnitNotice(const char*,const char*){++notices;}const char* NotReadySentence(){return "Pilot unavailable";}
'''

CASES = r'''
#define REQUIRE(e) do{if(!(e)){std::fprintf(stderr,"case %s at %d\n",label,__LINE__);return 1;}}while(0)
int main(int argc,char**argv){const char*label=argv[1]; const std::string mode(label);job={true,CE_FACTORY_READY,17,4,&actor};WORLD.all={&actor};rec.members={&actor};
 if(mode.rfind("handback-",0)==0){
     if(mode=="handback-reassigned"){kept=false;actor.squad=&otherSquad;}else if(mode=="handback-foreign-ai"){actor.ai=13;}
     else if(mode=="handback-gone"){actor.life=0;}else if(mode=="handback-no-squad"){actor.squad=nullptr;}
     else if(mode=="handback-later-order"){sq.ai=19;}else if(mode!="handback-current") return 2;
     const int before=actor.ai; ResumeDeliveryAssignment(&actor);
     if(mode=="handback-current"||mode=="handback-later-order")REQUIRE(actor.writes==1&&actor.ai==sq.ai);
     else REQUIRE(actor.writes==0&&actor.ai==before);
     return 0;
 }
 if(mode=="valid"){REQUIRE(CeFactoryLiveVehicle()==&actor);REQUIRE(CeCmdDeliveredSquad()==7);REQUIRE(CePlayUnitTakeDelivery()&&seat&&calls==1);return 0;}
 if(mode=="lost"){WORLD.all.clear();}else if(mode=="dead"){actor.life=0;}else if(mode=="dying"){actor.dying=true;}
 else if(mode=="recycled-number"){WORLD.all={&replacement};}else if(mode=="recycled-address"){actor.number=31;}
 else if(mode=="class-changed"){actor.type=&otherData;}else if(mode=="captured"){actor.team=8;}
 else if(mode=="off"){job.active=false;}else if(mode=="not-complete"){job.stage=1;}
 else if(mode=="unknown-squad"){actor.squad=&otherSquad;}else if(mode=="mixed-squad"){rec.members.push_back(&replacement);}
 else if(mode=="substitute-squad"){rec.members={&replacement};}else if(mode=="scenario-order-refused"){ordinaryOrderAllowed=false;}
 else if(mode=="missing-record"){recordPresent=false;}else if(mode=="squad-retired"){rec.alive=false;}
 else if(mode=="foreign-camera"){seat=true;}else if(mode=="map-open"){map=true;}else if(mode=="pilot-unready"){ready=false;}
 else if(mode=="no-unit-option"){gWanted=false;}else if(mode=="context-refused"){gate=false;}else if(mode=="class-refused"){kind=-1;}else return 2;
 const bool gateOnly=mode=="foreign-camera"||mode=="map-open"||mode=="pilot-unready"||mode=="no-unit-option"||mode=="context-refused"||mode=="class-refused";
 if(!gateOnly) REQUIRE(CeCmdDeliveredSquad()==-1);
 REQUIRE(!CePlayUnitTakeDelivery()&&calls==0);
 if(mode=="pilot-unready"||mode=="class-refused")REQUIRE(notices==1);
 return 0;
}
'''


class Delivery(unittest.TestCase):
    def test_selection_and_cockpit(self):
        gates = [('ce_cmdproduction.cpp','bool CeFactoryIsVehicle('),
                 ('ce_cmdproduction.cpp','CUnit *CeFactoryLiveVehicle('),
                 ('ce_commander.cpp','int CeCmdDeliveredSquad('),
                 ('ce_playunit.cpp','void ResumeDeliveryAssignment('),
                 ('ce_playunit.cpp','bool CePlayUnitCanTakeDelivery('),
                 ('ce_playunit.cpp','bool CePlayUnitTakeDelivery(')]
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            source = folder / 'gates.cpp'
            src = '#include <string>\n' + HARNESS + '\n'.join(definition(*gate) for gate in gates) + CASES
            source.write_text(src)
            exe = folder / 'gates'
            subprocess.run([os.environ.get('CXX','c++'), '-std=c++17', '-O0', str(source), '-o', str(exe)],check=True)
            for situation in ('valid lost dead dying recycled-number recycled-address class-changed captured off '
                              'not-complete unknown-squad mixed-squad substitute-squad scenario-order-refused '
                              'missing-record squad-retired foreign-camera map-open pilot-unready no-unit-option '
                              'context-refused class-refused handback-current handback-later-order handback-reassigned '
                              'handback-foreign-ai handback-gone handback-no-squad').split():
                with self.subTest(situation=situation):
                    subprocess.run([str(exe),situation],check=True)

if __name__=='__main__':
    unittest.main()
