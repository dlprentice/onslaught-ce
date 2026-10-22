// tweak.cpp: console-tweakable values (not in Stuart's release; rebuilt from the retail object 00528690..00528b57,
// which has no maths initializers). Every tweak links itself into one list by name; cardid.txt can set any of
// them for the player's graphics card (LoadCardId).

#include "common.h"
#include "tweak.h"
#include "ltshell.h"
#include "DebugLog.h"

CTweak	*CTweak::mFirst;							// 0089c018

//******************************************************************************************
CTweak::CTweak(char *name)	// FUNCTION: 0x00528690
{
	mName = name;
	mNext = mFirst;
	mFirst = this;
}

//******************************************************************************************
CTweak::~CTweak()	// FUNCTION: 0x005286b0
{
	CTweak	**link = &mFirst;
	for (CTweak *t = mFirst; t; t = t->mNext)
	{
		if (t == this)
		{
			*link = mNext;
			return;
		}
		link = &t->mNext;
	}
}

//******************************************************************************************
// Reads cardid.txt: "Vendor:" and "Device:" lines (hex ids) select the blocks that apply to this adapter, a
// "Driver:" line limits them to a range of driver versions, and each "Tweak: name value" in a matching block sets
// that tweak. "//" starts a comment.
// a "Key: hex" line
static bool	ReadHexLine(char *line, char *key, DWORD *id)
{
	if (_strnicmp(line, key, strlen(key)))
		return false;
	return sscanf(line + strlen(key), "%4x", id) == 1;
}

void	LoadCardId(char *filename)	// FUNCTION: 0x005286e0
{
	D3DAdapterInfo	*adapter = &LT.m_Adapters[LT.m_dwAdapter];
	FILE			*f = fopen(filename, "ra");

	if (f)
	{
		bool	vendor = false;
		bool	device = false;
		bool	apply = true;
		bool	in_device = false;

		while (!feof(f))
		{
			char	line[200] = "";
			DWORD	id;

			fgets(line, 199, f);
			if (line[0] == '/')
			{
				if (line[1] == '/')
					continue;
			}
			else if ((line[0] == 0) || (line[0] == '\r') || (line[0] == '\n'))
				continue;

			if (vendor && ReadHexLine(line, "Device:", &id))
			{
				if (!in_device || !device)
				{
					device = (id == adapter->d3dAdapterIdentifier.DeviceId);
					if (device)
						SETUP_HISTORY.AddMessage("Device in CardID: %s", line);
				}
				in_device = true;
				apply = true;
				continue;
			}
			in_device = false;

			if (!_strnicmp(line, "Version:", strlen("Version:")))
			{
				SETUP_HISTORY.AddMessage("CardID version: %s", line + strlen("Version:"));
				continue;
			}

			if (ReadHexLine(line, "Vendor:", &id))
			{
				apply = true;
				vendor = (id == adapter->d3dAdapterIdentifier.VendorId);
				continue;
			}

			LARGE_INTEGER	lo, hi;
			char			name[200];
			float			value;
			if (vendor && device && (sscanf(line, "Driver:%x %x - %x %x", &lo.HighPart, &lo.LowPart, &hi.HighPart, &hi.LowPart) == 4))
			{
				LARGE_INTEGER	&ver = adapter->d3dAdapterIdentifier.DriverVersion;
				if (((lo.HighPart < ver.HighPart) || ((lo.HighPart == ver.HighPart) && (lo.LowPart <= ver.LowPart))) &&
					((hi.HighPart > ver.HighPart) || ((hi.HighPart == ver.HighPart) && (hi.LowPart >= ver.LowPart))))
					apply = true;
				else
					apply = false;
			}
			else if (vendor && device && apply && !_strnicmp(line, "Tweak:", strlen("Tweak:")) && (sscanf(line + strlen("Tweak:"), "%s %f", name, &value) == 2))
			{
				CTweak	*t;
				for (t = CTweak::mFirst; t; t = t->mNext)
					if (!_stricmp(name, t->mName))
						break;
				if (t)
				{
					SETUP_HISTORY.AddMessage("Setting tweak %s to %f", name, value);
					t->Set(value);
				}
			}
		}
		fclose(f);
	}
	else
		SETUP_HISTORY.AddMessage("Couldn't open CardID file.\n");
}

//******************************************************************************************
CTweakSLONG::CTweakSLONG(char *name, int value) : CTweak(name)	// FUNCTION: 0x00528aa0
{
	mValue = value;
}

void	CTweakSLONG::Set(float value)	// FUNCTION: 0x00528ad0
{
	mValue = int(value);
}

bool	CTweakSLONG::IsSet()	// FUNCTION: 0x00528af0
{
	return mValue != -1;
}

void	CTweakSLONG::SetDefault(int value)	// FUNCTION: 0x00528b00
{
	if (mValue == -1)
		Set(float(value));
}

//******************************************************************************************
CTweakFLOAT::CTweakFLOAT(char *name, float value) : CTweak(name)	// FUNCTION: 0x00528b20
{
	mValue = value;
}

void	CTweakFLOAT::Set(float value)	// FUNCTION: 0x00528b50
{
	mValue = value;
}
