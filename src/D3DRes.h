// D3DRes.h: the sample framework's resource ids (only what matched files need; the values are the ones d3dapp.cpp's
// retail code passes, 00529146..0052c3b6, and match the Direct3D SDK's d3dres.h).

#ifndef D3DRES_H
#define D3DRES_H

#define IDI_MAIN_ICON          101 // Application icon (Create 0052914a)
#define IDR_MENU               141 // Application menu (Create 005291e5)
#define IDD_SELECTDEVICE       144 // "Change Device" dialog box (Reset3DEnvironment 0052bbc6)
#define IDC_DEVICE_COMBO          1000 // Device combobox for "SelectDevice" dlg
#define IDC_ADAPTER_COMBO         1002 // Adapter combobox for "SelectDevice" dlg
#define IDC_FULLSCREENMODES_COMBO 1003 // Mode combobox for "SelectDevice" dlg
#define IDC_MULTISAMPLE_COMBO     1005 // MultiSample combobox for "SelectDevice" dlg
#define IDC_WINDOW                1016 // Radio button for windowed-mode
#define IDC_FULLSCREEN            1018 // Radio button for fullscreen-mode
#define IDM_CHANGEDEVICE     40002 // Command to invoke "Change Device" dlg (MsgProc 0052abfd)
#define IDM_EXIT             40006 // Command to exit the application (MsgProc 0052ac04)

#endif
