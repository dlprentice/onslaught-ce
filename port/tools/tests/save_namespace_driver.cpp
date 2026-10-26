// SPDX-License-Identifier: GPL-3.0-only
// Actual production Windows ordinal identity, FindAdd/scan/lookup/reserve, with an explicit ASCII+É/é OS model.
#define main UnusedFindScanHarnessMain
#include "save_find_driver.cpp"
#undef main
#ifdef SAVE_CARD_CREATE_SOURCE
#include "save_query_fixture.h"
#endif
static WIN32_FIND_DATAA Entry(const char *name)
{
	WIN32_FIND_DATAA entry = {}; strcpy(entry.cFileName,name); return entry;
}
int main(int argc, char **argv)
{
	if (argc != 2 && argc != 3) return 2;
	std::string test = argv[1];
	if(test.compare(0,8,"unknown-")==0)
	{
#ifdef SAVE_CARD_CREATE_SOURCE
		if(argc!=3)return 2;
		CoupledCreate(test,argv[2]);
#else
		return 2;
#endif
	}
	else if (test == "overlay" || test == "overlay-disagreement" || test == "same-root" || test == "distinct" || test == "ascii-overlay" || test == "resource-overlay")
	{
		if(test=="overlay-disagreement") nativeFault="identity-disagreement";
		Find f = {}; f.saveNames = test != "resource-overlay";
		WIN32_FIND_DATAA first = Entry(test == "ascii-overlay" ? "alpha.bes" : "\xc3\xa9.bes");
		WIN32_FIND_DATAA second = Entry(test == "distinct" ? "\xc3\x83\xc2\xa9.bes" : test == "ascii-overlay" ? "ALPHA.bes" : "\xc3\x89.bes");
		FindAdd(&f,&first); if (test != "same-root") f.sourceFirst = f.count;
		FindAdd(&f,&second);
		int expected = test == "same-root" || test == "overlay-disagreement" ? 0 : test == "distinct" || test == "resource-overlay" ? 2 : 1;
		Check(f.count == expected && f.error == (test == "same-root" ? 52u : test=="overlay-disagreement" ? 50u : 0u),"overlay shadow/collision/legacy identity wrong");
		if (f.count) Check(!strcmp(f.entries[0].cFileName,first.cFileName),"overlay did not preserve user spelling");
		free(f.entries);
	}
	else if (test == "lookup" || test == "case-disagreement" || test == "lookup-error" || test == "identity-disagreement" || test == "identity-denied")
	{
		Add({0xe9}); WCHAR requested[]={0xc9,0}; int index=5;
		if (test == "case-disagreement") nativeExists=false;
		if (test == "lookup-error") fault="native-next";
		if (test == "identity-disagreement" || test == "identity-denied") nativeFault=test;
		unsigned result=PortSaveLookupUtf16(requested,&index);
		Check(test == "lookup" ? result==0 && index==0 : result==(test=="lookup-error" || test=="identity-denied"?5u:50u) && index==-1,
			"lookup admitted missing/collation-disagreeing/partial namespace");
		Check(!creates,"lookup wrote a file");
	}
	else if (test == "reserve" || test == "already-exists" || test == "reserve-invalid")
	{
		if(test=="already-exists") nativeFault=test;
		unsigned result=PortSaveReserveUtf8(test=="reserve-invalid"?"savegames\\..\\x.bes":"savegames\\\xc3\xa9.bes");
		Check(result==(test=="already-exists"?80u:test=="reserve-invalid"?123u:0u),"reservation result");
		Check(creates==(test=="reserve-invalid"?0u:1u) && reservations==(test=="reserve"?1u:0u),"invalid/existing reservation changed file");
	}
	else if (test == "case-sensitive" || test == "directory-missing" || test == "directory-denied" || test == "query-denied" || test == "old-windows" ||
		test=="query-known" || test=="query120" || test=="case-flags2" || test=="query-zero" || test=="directory-open-zero" ||
		test=="directory-close-zero" || test=="directory-close-denied" || test=="query-denied-close-zero")
	{
		nativeFault=test; WCHAR path[]={'C',':','\\','s','a','v','e','g','a','m','e','s','\\','*',0};
		if(test=="query120")caseQueryError=120;
#if CASE_QUERY_OUTPUT
		SaveDirectoryCase policy=SAVE_CASE_KNOWN_INSENSITIVE;
		unsigned result=SaveDirectoryCasePolicy(path,&policy);
		Check(policy==(test=="query-known"?SAVE_CASE_KNOWN_INSENSITIVE:SAVE_CASE_QUERY_UNKNOWN),"failed/unsupported policy claimed known-insensitive");
#else
		unsigned result=SaveDirectoryCasePolicy(path);
#endif
		Check(result==(test=="case-sensitive"||test=="case-flags2"?50u:test=="query-zero"||test=="directory-close-zero"||test=="directory-open-zero"?31u:test=="directory-denied"||test=="query-denied"||test=="directory-close-denied"||test=="query-denied-close-zero"?5u:0u),"directory policy admitted unsupported/denied state or rejected ordinary old OS");
		Check(!creates && directoryOpens==1 && closesNative==(test=="directory-missing"||test=="directory-denied"||test=="directory-open-zero"?0u:1u),"directory policy ownership/write");
	}
	else if(test=="fileid-same" || test=="fileid-upper64" || test=="fileid-volume-upper" || test=="fileid-unsupported" || test=="fileid-bytes")
	{
		unsigned trials=test=="fileid-bytes"?16:1;
		for(unsigned i=0;i<trials;i++)
		{
			identityReads=identityOpens=closesNative=0;
			nativeFault=test=="fileid-bytes"?"identity-byte-"+std::to_string(i):
				test=="fileid-same"?"":test=="fileid-upper64"?"identity-upper64":
				test=="fileid-volume-upper"?"identity-volume-upper":"identity-unsupported";
			unsigned result=SaveNativeSameFile("user\\savegames\\a.bes","user\\savegames\\A.bes");
			printf("%s byte=%u result=%u queries=%u closed=%u\n",test.c_str(),i,result,identityReads,closesNative);
			Check(result==(test=="fileid-same"?0u:50u),"file/volume upper bits aliased or unavailable128-bit query was admitted");
			Check(identityReads==(test=="fileid-unsupported"?1u:2u) && identityOpens==closesNative && !creates,
				"identity query lost handles or wrote a file");
		}
	}
	else if(test=="fileid-both-queries-zero")
	{
		nativeFault="identity-query-zero-both";
		unsigned result=SaveNativeSameFile("user\\savegames\\a.bes","user\\savegames\\A.bes");
		printf("%s result=%u queries=%u opened=%u closed=%u\n",test.c_str(),result,identityReads,identityHandles,identityCloses);
		Check(result==31 && identityReads==1 && identityHandles==identityCloses && !creates,"two failed identity queries were accepted as equal zero IDs");
	}
	else if(test=="fileid-failure-zero" || test=="fileid-query-error-preserved")
	{
		for(const char *phase:{"convert","open","query","close"})for(unsigned position=1;position<=2;position++)
		{
			if(test=="fileid-query-error-preserved" && std::string(phase)!="query")continue;
			identityReads=identityOpens=identityHandles=identityCloses=closesNative=decodeCalls=0;
			std::string condition=test=="fileid-query-error-preserved"?"identity-query5-close0-"+std::to_string(position):
				"identity-"+std::string(phase)+"-zero-"+std::to_string(position);
			fault=std::string(phase)=="convert"?condition:"";nativeFault=std::string(phase)=="convert"?"":condition;
			unsigned result=SaveNativeSameFile("user\\savegames\\a.bes","user\\savegames\\A.bes");
			printf("%s phase=%s position=%u result=%u opened=%u closed=%u\n",test.c_str(),phase,position,result,identityHandles,identityCloses);
			Check(result==(test=="fileid-query-error-preserved"?5u:31u),"native FALSE with zero last-error admitted identity/lost earlier error");
			Check(identityHandles==identityCloses && !creates,"failed identity path leaked/closed twice or wrote");
		}
	}
	else return 2;
	return failures?1:0;
}
