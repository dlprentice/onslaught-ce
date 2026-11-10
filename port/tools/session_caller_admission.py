#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Admit the named native load/automatic-script/pre-run abort sites, never a blanket call count."""
import hashlib
import re

# Strings/chars are single tokens; comments and whitespace never become evidence or braces.
LEX=re.compile(r'''\s+|//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[A-Za-z_]\w*|0[xX][0-9a-fA-F]+|\d+|::|<=|>=|==|!=|&&|\|\||[^\s]''',re.S)

def lex(text):
    return [(m.group(),m.start(),m.end()) for m in LEX.finditer(text)
            if not m.group().isspace() and not m.group().startswith(('//','/*'))]

def values(text):
    return [row[0] for row in lex(text)]

def one(haystack,needle,label):
    matches=[i for i in range(len(haystack)-len(needle)+1) if haystack[i:i+len(needle)]==needle]
    if len(matches)!=1:
        raise ValueError(label+' must have exactly one source-supported form')
    return matches[0]

LOAD='if (!CE_SessionAfterLoad()) { return (EQuitType)CE_SessionGameAbortLoad(); }'
# Main's console patch (0100) runs no script file while the CE layer is on: its gate sits between the abort guard and the call.
# The paused map authoring patch wraps the call itself: no script runs while the map editor owns the load.
SCRIPT=('int ce_script = CE_BootAutomaticScript(); if (ce_script < 0) return (EQuitType)CE_SessionGameAbortLoad(); '
        '#ifdef CE_VERSION if (!PortCeLayer()) #endif if (ce_script > 0) (ceEditorAuthoring ? (void)0 : CONSOLE.Exec("autoexec.con"));')
PRERUN='if (!CE_SessionPreparePlay()) { return (EQuitType)CE_SessionGameAbortLoad(); }'
WRAPPER='extern "C" int CE_SessionGameAbortLoad() { return CENetworkSession::AbortLoad(); }'

def admit(text):
    ts=lex(text);vs=[t[0] for t in ts]
    hashes={row[1] for row in ts if row[0]=='#'}
    directives=[]
    for match in re.finditer(r'(?m)^[\t ]*#\s*(if|ifdef|ifndef|elif|else|endif)\b[^\n]*',text):
        if text.index('#',match.start(),match.end()) in hashes:
            directives.append((match.start(),match.group(1)))
    signature=values('EQuitType CGame::RestartLoopRunLevel(SINT aLevel) {')
    start=one(vs,signature,'native RestartLoopRunLevel definition')+len(signature)-1
    depth=1;end=start+1
    while end<len(vs) and depth:
        depth+=(vs[end]=='{')-(vs[end]=='}');end+=1
    if depth:
        raise ValueError('native RestartLoopRunLevel body is incomplete')
    fragments={};records=[];owned_calls=set()
    for label,pattern in (('after-load',LOAD),('automatic-script',SCRIPT),('pre-run',PRERUN)):
        expected=values(pattern);at=one(vs,expected,label+' abort guard')
        if not start<at<at+len(expected)<end:
            raise ValueError(label+' is outside native RestartLoopRunLevel')
        nesting=sum((v=='{')-(v=='}') for v in vs[start:at])
        if nesting!=1:
            raise ValueError(label+' is not an unconditional native load-stage boundary')
        conditions=[]
        for position,directive in directives:
            if position>=ts[at][1]:break
            if directive in ('if','ifdef','ifndef'):conditions.append(position)
            elif directive=='endif':
                if not conditions:raise ValueError('unbalanced caller preprocessing')
                conditions.pop()
        if conditions:
            raise ValueError(label+' is hidden behind conditional preprocessing')
        call=at+expected.index('CE_SessionGameAbortLoad');owned_calls.add(call)
        source=text[ts[at][1]:ts[at+len(expected)-1][2]]
        fragments[label]=source
        records.append({'site':label,'line':text.count('\n',0,ts[at][1])+1,'token_index':at,
                        'source_sha256':hashlib.sha256(source.encode()).hexdigest(),
                        'unconditional_preprocessing':True,
                        'preserved_return':'(EQuitType)CE_SessionGameAbortLoad()'})
    wrapper=values(WRAPPER);at=one(vs,wrapper,'native abort bridge wrapper')
    owned_calls.add(at+wrapper.index('CE_SessionGameAbortLoad'))
    actual={i for i,v in enumerate(vs) if v=='CE_SessionGameAbortLoad'}
    if actual!=owned_calls:
        raise ValueError('unknown additional native abort reference')
    native=vs[start:end]
    stages=[one(native,values(pattern),label) for label,pattern in (
        ('world load','LoadLevel(aLevel)'),('post-load native processing','PostLoadProcess()'),
        ('session loaded',LOAD),('automatic script',SCRIPT),('session pre-run',PRERUN),
        ('initial sounds','unit->StartPlayingInitNoise()'),('level music','PlayMusicForCurrentLevel()'))]
    if stages!=sorted(stages):
        raise ValueError('native load, script, pre-run and sound stage ordering changed')
    return {'sites':records,'native_stage_order':stages,'native_function':'CGame::RestartLoopRunLevel',
            'caller_sha256':hashlib.sha256(text.encode()).hexdigest()},fragments

def controls(text):
    proof,fragments=admit(text);rows=[]
    mutations=[('unknown-extra',text+'\nint UnknownAbortCaller(){ return CE_SessionGameAbortLoad(); }\n','unknown additional'),
               ('after-load-return',text.replace(fragments['after-load'],fragments['after-load'].replace('(EQuitType)CE_SessionGameAbortLoad()','QT_NONE')),'after-load'),
               ('script-return',text.replace(fragments['automatic-script'],fragments['automatic-script'].replace('(EQuitType)CE_SessionGameAbortLoad()','QT_NONE')),'automatic-script'),
               ('pre-run-return',text.replace(fragments['pre-run'],fragments['pre-run'].replace('(EQuitType)CE_SessionGameAbortLoad()','QT_NONE')),'pre-run')]
    # Construct an unambiguous order-only change, without adding/removing any known site.
    load,script=fragments['after-load'],fragments['automatic-script']
    swapped=text.replace(load,'__SESSION_LOAD_SITE__',1).replace(script,load,1).replace('__SESSION_LOAD_SITE__',script,1)
    mutations.append(('script-early',swapped,'ordering'))
    mutations.append(('script-preprocessed-out',text.replace(script,'#if 0\n'+script+'\n#endif\n',1),'conditional preprocessing'))
    mutations.append(('unknown-address-reference',text+'\nauto AnotherAbort=&CE_SessionGameAbortLoad;\n','unknown additional'))
    for label,variant,reason in mutations:
        try:admit(variant)
        except ValueError as exc:
            if reason not in str(exc):raise
            rows.append({'control':label,'refused':True,'reason':str(exc),'source_sha256':hashlib.sha256(variant.encode()).hexdigest()})
        else:raise ValueError('caller admission control was not refused: '+label)
    # Token look-alikes in comments and string literals cannot create an additional callsite.
    admit(text+'\n// CE_SessionGameAbortLoad()\nconst char*callerLead="CE_SessionGameAbortLoad()";\n')
    proof['negative_controls']=rows;proof['non_code_lookalike_control']=True
    return proof,fragments
