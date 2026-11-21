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
assert protected==[members(q) for q in (user,game,package)];(job/'result.json').write_text(json.dumps({'checks':report,'protected_files_unchanged':True,'source_sha256':hashit(source.read_bytes())},indent=2)+'\n');print(f'{len(report)} preview cases passed; protected careers and settings unchanged. {job.name}')
