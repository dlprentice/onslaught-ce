// SPDX-License-Identifier: GPL-3.0-only
#include <cstdint>
#include <cstring>
#include <cstdio>
#define main unused_existing_fixture_main
#include "ce_netcode_driver.cpp"
#undef main
#include "ce_netcode.h"
int main() {
 const char *alphabet="0123456789ABCDEFGHJKMNPQRSTVWXYZ";
 uint64_t seed=3950599089740882159ULL;
 unsigned long valid=0,roundtrip_fail=0,spaces_fail=0,case_fail=0,alias_fail=0,linebreak_fail=0,single=0,single_accept=0,swaps=0,swap_accept=0,invalid_accept=0,unknown=0;
 for (int i=0;i<12000 && valid<300;i++) {
  CE_JoinCode x{};auto *raw=reinterpret_cast<unsigned char*>(&x);
  for(size_t j=0;j<sizeof(x);j++){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;raw[j]=(unsigned char)seed;}
  char out[256]={};(void)CE_CodeEncode(&x,out,sizeof(out));
  if(!out[0] || !std::memchr(out,0,sizeof(out))) { continue; }
  CE_JoinCode initial{},invalid{};int good=CE_CodeDecode(out,&initial);int bad=CE_CodeDecode("!",&invalid);
  if(good==bad) { continue; }
  size_t n=std::strlen(out);if(n>100){unknown++;continue;}valid++;
  auto matches = [&](const char *text) {
   CE_JoinCode decoded{};if(CE_CodeDecode(text,&decoded)!=good){return false;}
   char canonical[256]={};(void)CE_CodeEncode(&decoded,canonical,sizeof(canonical));
   return decoded.key==x.key && decoded.ipv4==x.ipv4 && decoded.port==x.port && std::memchr(canonical,0,sizeof(canonical)) && std::strcmp(out,canonical)==0;
  };
  if(!matches(out)){roundtrip_fail++;}
  if(CE_CodeDecode("",&invalid)==good || CE_CodeDecode("!",&invalid)==good){invalid_accept++;}
  char compact[256]={},lower[256]={},spaced[256]={},alias[256]={},alias_i[256]={},tabbed[256]={},wrapped[256]={},interleaved[256]={};size_t k=0;
  for(size_t j=0;j<n;j++){
   if(out[j]!='-' && out[j]!=' ') { compact[k++]=out[j]; }
   lower[j]=(out[j]>='A' && out[j]<='Z')?(char)(out[j]-'A'+'a'):out[j];
  }
  for(size_t j=0;j<k;j++){spaced[2*j]=' ';spaced[2*j+1]=compact[j];}spaced[2*k]=' ';
  if(!matches(compact) || !matches(spaced)){spaces_fail++;}
  if(!matches(lower)){case_fail++;}
  std::memcpy(alias,out,n+1);bool have_alias=false;
  for(size_t j=0;j<n;j++)if(alias[j]=='0'){alias[j]='O';have_alias=true;}else if(alias[j]=='1'){alias[j]='L';have_alias=true;}
  std::memcpy(alias_i,alias,n+1);for(size_t j=0;j<n;j++)if(alias_i[j]=='L'){alias_i[j]='I';}
  if(have_alias && (!matches(alias) || !matches(alias_i))){alias_fail++;}
  tabbed[0]='\t';std::memcpy(tabbed+1,out,n);tabbed[n+1]='\t';if(!matches(tabbed)){spaces_fail++;}
  wrapped[0]='\r';wrapped[1]='\n';std::memcpy(wrapped+2,out,n);wrapped[n+2]='\r';wrapped[n+3]='\n';
  for(size_t j=0;j<k;j++){interleaved[2*j]=compact[j];interleaved[2*j+1]=(j%2)?'\r':'\n';}
  if(!matches(wrapped) || !matches(interleaved)){linebreak_fail++;}
  size_t pos[256],count=0;for(size_t j=0;j<n;j++){
   if(std::strchr(alphabet,out[j])){pos[count++]=j;}else if(out[j]!='-' && out[j]!=' '){unknown++;}
  }
  for(size_t j=0;j<count;j++)for(const char *c=alphabet;*c;c++){
   if(*c==out[pos[j]]){continue;}char changed[256];std::memcpy(changed,out,n+1);changed[pos[j]]=*c;CE_JoinCode temp{};single++;
   if(CE_CodeDecode(changed,&temp)==good){single_accept++;}
  }
  for(size_t j=0;j<count;j++)for(size_t q=j+1;q<count;q++){
   if(out[pos[j]]==out[pos[q]]){continue;}char changed[256];std::memcpy(changed,out,n+1);changed[pos[j]]=out[pos[q]];changed[pos[q]]=out[pos[j]];CE_JoinCode temp{};swaps++;
   if(CE_CodeDecode(changed,&temp)==good){swap_accept++;}
  }
 }
 std::printf("{\"valid\":%lu,\"roundtrip_fail\":%lu,\"spaces_fail\":%lu,\"case_fail\":%lu,\"alias_fail\":%lu,\"linebreak_fail\":%lu,\"single_checks\":%lu,\"single_accepted\":%lu,\"swap_checks\":%lu,\"swap_accepted\":%lu,\"invalid_accepted\":%lu,\"unknown_symbols\":%lu}\n",valid,roundtrip_fail,spaces_fail,case_fail,alias_fail,linebreak_fail,single,single_accept,swaps,swap_accept,invalid_accept,unknown);
 return valid==300 && !roundtrip_fail && !spaces_fail && !case_fail && !alias_fail && !linebreak_fail && single>0 && !single_accept && swaps>0 && !swap_accept && !invalid_accept && !unknown ? 0 : 1;
}