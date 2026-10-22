/* avirecorder.c: STAND-IN (link layout; an end-stage concession, applied 2026-10-16). Retail's game dropped an AVI
   recorder whose imports survive in the image: the MSVFW32 import library's null thunk (the zero dword after KERNEL32's
   block in the IAT), the `comdlg32.dll`/`MSVFW32.dll` name strings and two Rich import-member counts. This is the
   smallest form that reproduces them: one unreferenced function calling one MSVFW32 and one comdlg32 import through the
   import pointers. /OPT:REF removes its code; nothing in the specimen calls it. Its body is ours, not retail's. */
#define IMP __declspec(dllimport)
IMP int __stdcall GetSaveFileNameA(void *ofn);
IMP long __stdcall ICSendMessage(void *hic, unsigned msg, unsigned long p1, unsigned long p2);
int EndStageAviRecorderStart(void *hic, void *ofn, void *opts)
{
	if (!GetSaveFileNameA(ofn)) return 0;
	return ICSendMessage(hic, 0x4000 + 105, (unsigned long)opts, 4);
}
