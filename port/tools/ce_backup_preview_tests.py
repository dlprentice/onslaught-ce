#!/usr/bin/env python3
"""Read-only backup preview: real manifests and authored data in disposable directories."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[2];parent=root/'local-data/port/ce-backup-preview-tests';parent.mkdir(parents=True,exist_ok=True)
job=Path(tempfile.mkdtemp(prefix='run-',dir=parent));exe=job/'preview';subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-O2',str(root/'port/platform/linux/ce_backup_preview.cpp'),str(root/'port/tools/tests/ce_backup_preview_driver.cpp'),'-o',str(exe)],check=True)
user,game,package,storage=[job/x for x in ('user','game','package','sets')]
for p in (user,game,package,storage):p.mkdir()
for p in (user,game):(p/'savegames').mkdir()
(user/'savegames/Backup Copy.bes').write_bytes(b'installed untouched');(game/'savegames/BACKUP COPY 2.BES').write_bytes(b'original untouched');(user/'defaultoptions.bea').write_bytes(b'conflicting local options');(user/'ce.ini').write_bytes(b'original configuration');
source=job/'authored.bes';subprocess.run([str(exe),'fixture',str(source)],check=True);raw=source.read_bytes();alpha='Andr\u00e9.bes';hashit=lambda b:hashlib.sha256(b).hexdigest();report=[]
def members(base):return {p.relative_to(base).as_posix():(hashit(p.read_bytes()),p.stat().st_ino)for p in base.rglob('*')if p.is_file()and not p.is_symlink()}
protected=[members(q) for q in (user,game,package)]
def make(name,entries=None):
 p=storage/name;p.mkdir();entries=entries if entries is not None else [(alpha,raw)];files=[]
 for leaf,content in entries:
  (p/leaf).write_bytes(content);files.append(dict(Name=leaf,Original='/an/irrelevant/recorded/path/'+leaf,Size=len(content),Sha256=hashit(content)))
 manifest=dict(Folder='/recorded/folder',Created='2026-04-05T12:30:00Z',Game='/recorded/game',Files=files,Reason='Manual',Target=1);(p/'onslaught-backup.json').write_text(json.dumps(manifest,ensure_ascii=True));return p,manifest

def verify(name,p,allow=0,slot='Backup Copy 3.bes'):
 rc=subprocess.run([str(exe),str(p),str(user),str(game),str(package),alpha if allow else 'deny',slot,str(allow)],capture_output=True).returncode;report.append((name,rc==0));assert rc==0,(name,rc)

good,m=make('good');before=members(good);verify('single career with natural name and two occupied slots',good,1);verify('reopen',good,1);assert before==members(good)
full,m=make('multiple',[(alpha,raw),('Second.bes',raw),('defaultoptions.bea',b'backup-only options')]);verify('whole multi-file set verified including foreign options',full,3);(full/'Second.bes').write_bytes(b'x'*len(raw));verify('unselected member changed',full)
for case in ('malformed','hash','size','duplicate','traversal','bidi','missing','missing-manifest','payload-link','manifest-link','hardlink','ordinary-data','unstated-file-type'):
 p,m=make(case);man=p/'onslaught-backup.json'
 if case=='malformed':man.write_text('{broken')
 elif case=='missing-manifest':man.unlink()
 elif case=='missing':(p/alpha).unlink()
 elif case=='payload-link':(p/alpha).unlink();(p/alpha).symlink_to(source)
 elif case=='manifest-link':man.unlink();man.symlink_to(good/'onslaught-backup.json')
 elif case=='hardlink':(p/alpha).unlink();os.link(source,p/alpha)
 else:
  if case=='hash':m['Files'][0]['Sha256']='a'*64
  if case=='size':m['Files'][0]['Size']+=1
  if case=='duplicate':m['Files'].append(dict(m['Files'][0],Name=alpha.upper().replace('\u00c9','\u00e9')))
  if case=='traversal':m['Files'][0]['Name']='../authored.bes'
  if case=='bidi':m['Files'][0]['Name']='Hidden\u202e.bes'
  if case=='ordinary-data':(p/alpha).write_bytes(b'x'*len(raw));m['Files'][0]['Sha256']=hashit((p/alpha).read_bytes())
  if case=='unstated-file-type':m['Files'][0]['Name']='outside.ini'
  man.write_text(json.dumps(m))
 verify(case,p)
link=job/'linked';link.symlink_to(good,target_is_directory=True);verify('linked folder',link)
for name,protectedRoot in [('user',user),('game',game),('package',package)]:verify('refuse protected '+name,protectedRoot)
catalog=job/'catalog';catalog.mkdir();newest='onslaught-backup-20261113-131212';older='onslaught-backup-20261113-131211';shutil.copytree(good,catalog/newest);shutil.copytree(good,catalog/older);shutil.copytree(good,catalog/'unrelated');(catalog/'onslaught-backup-linked').symlink_to(good,target_is_directory=True);(catalog/'onslaught-backup-pending').mkdir();(catalog/'onslaught-backup-incomplete').mkdir();(catalog/'onslaught-backup-incomplete/onslaught-backup.json').symlink_to(good/'onslaught-backup.json')
for title,folder,n,first in [('newest first; links and incomplete sets excluded',catalog,2,newest),('missing configured folder',job/'missing',-1,''),('protected configured folder',user,-1,'')]:
 rc=subprocess.run([str(exe),str(folder),str(user),str(game),str(package),'list',str(n),first],capture_output=True).returncode;report.append((title,rc==0));assert rc==0,(title,rc)
anc_real=job/'anc_real';anc_real.mkdir();anc_link=job/'anc_link';anc_link.symlink_to(anc_real,target_is_directory=True)
for sub in ('user','game','package'):(anc_real/sub).mkdir()
(anc_real/'user/savegames').mkdir();(anc_real/'game/savegames').mkdir()
(anc_real/'user/savegames/Backup Copy.bes').write_bytes(b'installed untouched');(anc_real/'game/savegames/BACKUP COPY 2.BES').write_bytes(b'original untouched')
lu,lg,lp=anc_link/'user',anc_link/'game',anc_link/'package'
rc=subprocess.run([str(exe),str(good),str(lu),str(lg),str(lp),alpha,'Backup Copy 3.bes','1'],capture_output=True).returncode;report.append(('symlinked ancestors on read',rc==0));assert rc==0,('symlinked ancestors on read',rc)
rc=subprocess.run([str(exe),str(catalog),str(lu),str(lg),str(lp),'list','2',newest],capture_output=True).returncode;report.append(('symlinked ancestors on list',rc==0));assert rc==0,('symlinked ancestors on list',rc)
ctrl_cpp=job/'preview_ctrl.cpp'
ctrl_cpp.write_text(r'''#include <SDL3/SDL.h>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include "ce_backup_preview.h"
#include "ce_career_restore.h"
#define CE_CAREER_RESTORE_UI_INTERNAL
#include "ce_career_restore_ui.h"
static SDL_DialogFileCallback gCb;
static void *gData;
static std::string gPkg;
extern "C" const char *SDL_GetBasePath(void) { return gPkg.c_str(); }
extern "C" SDL_Window *SDL_GetKeyboardFocus(void) { return nullptr; }
extern "C" void SDL_ShowOpenFolderDialog(SDL_DialogFileCallback cb, void *data, SDL_Window *, const char *, bool) { gCb = cb; gData = data; }
bool CeCareerRestoreStamp(const CeCareerRestoreRequest &, std::string &out) { out = "test-stamp"; return true; }
static bool waitPhase(CePreviewPhase want) {
  CePreviewDisplay d{};
  for (int i = 0; i < 1000; ++i) { CePreviewDisplayRead(0, &d); if (d.phase == want) return true; std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
  return false;
}
int main(int argc, char **argv) {
  if (argc != 7) return 2;
  gPkg = std::string(argv[3]) + "///";
  CePreviewOpen((std::string(argv[1]) + "///").c_str(), (std::string(argv[2]) + "///").c_str(), (std::string(argv[4]) + "///").c_str());
  if (!waitPhase(CE_PREVIEW_IDLE)) return 3;
  CePreviewDisplay d{};
  CePreviewDisplayRead(0, &d);
  if (d.choices != 2) return 4;
  CePreviewSelectSet(0);
  if (!waitPhase(CE_PREVIEW_VERIFIED)) return 5;
  CeRestoreCard card;
  CePreviewRestoreCard(card);
  if (!card.verified || card.set != std::string(argv[4]) + "/" + argv[6] || card.root != argv[4] || card.user != argv[1] || card.game != argv[2] || card.package != argv[3]) return 6;
  CePreviewChoose();
  std::string picked = std::string(argv[5]) + "//";
  const char *files[] = {picked.c_str(), nullptr};
  gCb(gData, files, 0);
  if (!waitPhase(CE_PREVIEW_VERIFIED)) return 8;
  CePreviewRestoreCard(card);
  CePreviewLeave();
  return card.verified && card.set == argv[5] ? 0 : 10;
}
''')
ctrl_exe=job/'preview-ctrl'
subprocess.run(['c++','-std=c++17','-pthread','-Wall','-Wextra','-Werror','-O2','-I'+str(root/'port/platform/common'),'-I'+str((root/'port/tools/deps.py').parent),'-isystem',str(Path(os.environ['BEA_PORT_DATA'])/'prefix-sniper/include'),str(ctrl_cpp),str(root/'port/platform/common/ce_backup_preview.cpp'),str(root/'port/platform/linux/ce_backup_preview.cpp'),str(root/'port/game/ce_career_view.cpp'),'-o',str(ctrl_exe)],check=True)
bslash_set=job/'set_with_backslash\\';shutil.copytree(good,bslash_set)
rc=subprocess.run([str(ctrl_exe),str(user),str(game),str(package),str(catalog),str(bslash_set),newest],capture_output=True).returncode;report.append(('controller trailing slash trim and posix backslash preserve',rc==0));assert rc==0,('controller trailing slash trim and posix backslash preserve',rc)
assert protected==[members(q) for q in (user,game,package)];(job/'result.json').write_text(json.dumps({'checks':report,'protected_files_unchanged':True,'source_sha256':hashit(source.read_bytes())},indent=2)+'\n');print(f'{len(report)} preview cases passed; protected careers and settings unchanged. {job.name}')
