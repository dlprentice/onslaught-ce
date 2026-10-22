// keytrap.h: the key trap the console and front end install (Stuart's ltshell.h; shared with Platform.h).

#ifndef KEYTRAP_H
#define KEYTRAP_H

enum KeyEventType
{
	KEY_EVENT_DOWN,
	KEY_EVENT_UP,
	KEY_EVENT_CHAR,
	KEY_EVENT_MOUSE,			// retail PC: a mouse button, the key is its number (the remap capture, 004562a4)
	KEY_EVENT_UNKNOWN4			// retail PC: the remap capture ignores it (0045619a)
};

typedef void (*pKeyTrapper)(WORD key,KeyEventType event);	// retail passes the scan code as a word (00512ea6)

#endif
