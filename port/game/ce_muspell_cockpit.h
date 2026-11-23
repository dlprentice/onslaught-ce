// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_muspell_cockpit.h: pure, host-testable helpers for authentic Muspell first-person cockpit rendering
// (meshtex\cockpit_M.tga + procedural warm crimson-amber gunlight). Zero game-header dependencies so host ASan/UBSan
// tests compile and mutate this directly.
#ifndef PORT_CE_MUSPELL_COCKPIT_H
#define PORT_CE_MUSPELL_COCKPIT_H

static constexpr int CE_MUSPELL_ALLEGIANCE = 1;
static constexpr int CE_MUSPELL_COCKPIT_MAX_SWAPS = 32;
static constexpr int CE_MUSPELL_GUNLIGHT_SIZE = 64;

inline char CeMuspellCockpitNormChar(char c)
{
	if (c >= 'A' && c <= 'Z')
		return (char)(c - 'A' + 'a');
	if (c == '/')
		return '\\';
	return c;
}

inline bool CeMuspellCockpitStrEqPathCase(const char *a, const char *b)
{
	if (!a || !b)
		return false;
	while (*a && *b)
	{
		if (CeMuspellCockpitNormChar(*a) != CeMuspellCockpitNormChar(*b))
			return false;
		++a;
		++b;
	}
	return *a == '\0' && *b == '\0';
}

inline bool CeMuspellCockpitIsBaseTextureName(const char *name)
{
	return CeMuspellCockpitStrEqPathCase(name, "meshtex\\cockpit.tga");
}

inline bool CeMuspellCockpitIsMuspellTextureName(const char *name)
{
	return CeMuspellCockpitStrEqPathCase(name, "meshtex\\cockpit_M.tga");
}

inline bool CeMuspellCockpitIsGunlightTextureName(const char *name)
{
	return CeMuspellCockpitStrEqPathCase(name, "meshtex\\A8_bluegunlight_LIT.tga");
}

// Deterministic procedural warm crimson-amber emissive gradient (0xAARRGGBB) for Muspell gun-pod lights.
// Pure math (zero retail asset derivation): deep Muspell crimson on the outer edge ramping smoothly along U to molten
// orange-amber / hot gold at the bright core, with a brighter horizontal center band along V and strong alpha for mLit.
inline unsigned int CeMuspellCockpitGunlightPixel(int x, int y, int width, int height)
{
	if (width <= 0 || height <= 0)
		return 0u;
	if (x < 0) x = 0;
	if (x >= width) x = width - 1;
	if (y < 0) y = 0;
	if (y >= height) y = height - 1;

	const float u = (width > 1) ? ((float)x / (float)(width - 1)) : 0.0f;
	const float v = (height > 1) ? ((float)y / (float)(height - 1)) : 0.5f;
	const float centerBand = (v >= 0.18f && v <= 0.82f) ? 1.0f : 0.78f;

	const float rF = (232.0f + 23.0f * u) * (0.86f + 0.14f * centerBand);
	const float gF = (36.0f + 132.0f * u * u + 44.0f * u) * centerBand;
	const float bF = (10.0f + 78.0f * u * u * u) * centerBand;
	const float aF = (210.0f + 45.0f * u) * (0.85f + 0.15f * centerBand);

	auto clampByte = [](float val) -> unsigned int {
		if (val <= 0.0f) return 0u;
		if (val >= 255.0f) return 255u;
		return (unsigned int)(val + 0.5f);
	};

	const unsigned int a = clampByte(aF);
	const unsigned int r = clampByte(rF);
	const unsigned int g = clampByte(gF);
	const unsigned int b = clampByte(bF);
	return (a << 24) | (r << 16) | (g << 8) | b;
}

// Pure, host-testable warm crimson-charcoal lighting & emissive balance for Muspell 1P cockpits.
// Prevents cool night-sky ambient/directional lights (e.g. Level 851) from washing retail cockpit_M.tga
// into dark navy-indigo, while preserving its metallic bevel contrast and restoring every light/material
// strictly in LIFO order before any Forseti cockpit or world mesh renders.
struct CeMuspellCockpitLightBoost
{
	unsigned int ambient;
	float keyR, keyG, keyB;
	float rimR, rimG, rimB;
	float matDiffR, matDiffG, matDiffB;
	float matEmisR, matEmisG, matEmisB;
};

inline CeMuspellCockpitLightBoost CeMuspellCockpitComputeLightBoost(unsigned int origAmbient,
                                                                    float origKeyR, float origKeyG, float origKeyB)
{
	CeMuspellCockpitLightBoost out = {};
	const unsigned int origR = (origAmbient >> 16) & 0xffu;
	const unsigned int origG = (origAmbient >> 8) & 0xffu;
	const unsigned int origB = origAmbient & 0xffu;

	// Lift ambient into a rich warm crimson-obsidian floor and clamp cool blue down aggressively.
	unsigned int aR = origR + 48u;
	if (aR < 68u) aR = 68u;
	if (aR > 96u) aR = 96u;
	unsigned int aG = (origG / 4u) + 14u;
	if (aG > 28u) aG = 28u;
	unsigned int aB = origB / 7u + 6u;
	if (aB > 16u) aB = 16u;
	out.ambient = 0xff000000u | (aR << 16) | (aG << 8) | aB;

	auto clampF = [](float v, float lo, float hi) -> float {
		if (v < lo) return lo;
		if (v > hi) return hi;
		return v;
	};
	const float lum = clampF(0.35f * origKeyR + 0.45f * origKeyG + 0.20f * origKeyB, 0.25f, 1.0f);
	// Warm deep crimson primary key light + dedicated front-angled crimson rim fill light.
	out.keyR = clampF(0.76f + 0.22f * lum, 0.76f, 0.98f);
	out.keyG = clampF(0.22f + 0.12f * lum, 0.22f, 0.34f);
	out.keyB = clampF(0.11f + 0.06f * lum, 0.11f, 0.18f);

	out.rimR = 0.62f;
	out.rimG = 0.16f;
	out.rimB = 0.08f;

	out.matDiffR = 1.05f;
	out.matDiffG = 0.62f;
	out.matDiffB = 0.50f;
	out.matEmisR = 0.09f;
	out.matEmisG = 0.014f;
	out.matEmisB = 0.008f;
	return out;
}

struct CeMuspellCockpitFenceInput
{
	bool ceLayer;
	bool recording;
	bool playableDemo;
	bool autoConfigTest;
	bool recordDemo;
	bool playbackDemo;
	bool stressTest;
	bool attractMode;
	bool pilotEnv;
	int  allegiance;
};

inline bool CeMuspellCockpitShouldSwap(const CeMuspellCockpitFenceInput &in)
{
	if (!in.ceLayer || in.recording)
		return false;
	if (in.playableDemo || in.autoConfigTest || in.recordDemo ||
	    in.playbackDemo || in.stressTest || in.attractMode || in.pilotEnv)
		return false;
	return in.allegiance == CE_MUSPELL_ALLEGIANCE;
}

template <typename TexPtr, typename VBufPtr>
struct CeMuspellCockpitSwapSlot
{
	TexPtr  *meshTexSlot;
	TexPtr   origMeshTex;
	VBufPtr  vbuf;
	TexPtr   origVBufTex;
};

// Fixed-capacity scoped swap/restore stack. Restores strictly in LIFO order so that multiple CMeshTexture entries
// sharing one CVBufTexture* (such as cockpit2b.msh) always restore the true pre-swap CVBufTexture::mTexture pointer.
template <typename TexPtr, typename VBufPtr, int Capacity = CE_MUSPELL_COCKPIT_MAX_SWAPS>
struct CeMuspellCockpitRestoreStack
{
	int count = 0;
	CeMuspellCockpitSwapSlot<TexPtr, VBufPtr> slots[Capacity] = {};

	bool RecordAndSwap(TexPtr *meshTexSlot, VBufPtr vbuf, TexPtr *vbufTexSlot, TexPtr replacement)
	{
		if (!meshTexSlot || !replacement || count < 0 || count >= Capacity)
			return false;
		CeMuspellCockpitSwapSlot<TexPtr, VBufPtr> &s = slots[count++];
		s.meshTexSlot = meshTexSlot;
		s.origMeshTex = *meshTexSlot;
		s.vbuf = vbuf;
		s.origVBufTex = vbufTexSlot ? *vbufTexSlot : nullptr;
		*meshTexSlot = replacement;
		if (vbufTexSlot)
			*vbufTexSlot = replacement;
		return true;
	}

	template <typename GetVBufTexSlotFn>
	int RestoreAll(GetVBufTexSlotFn getVBufTexSlot)
	{
		int restored = 0;
		for (int i = count - 1; i >= 0; --i)
		{
			const CeMuspellCockpitSwapSlot<TexPtr, VBufPtr> &s = slots[i];
			if (s.meshTexSlot)
				*s.meshTexSlot = s.origMeshTex;
			if (s.vbuf)
			{
				TexPtr *vslot = getVBufTexSlot(s.vbuf);
				if (vslot)
					*vslot = s.origVBufTex;
			}
			restored++;
		}
		count = 0;
		return restored;
	}
};

#endif
