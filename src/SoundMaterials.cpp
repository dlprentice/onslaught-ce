// SoundMaterials.cpp: the sound of two things' materials meeting (PlayImpactSound, 004e2e60; CRound::Hit calls it).
// Not in Stuart's release; reconstructed from its own object between SoundManager.obj and SpawnerThng.obj, whose
// maths constants open it (004e2d70, 004e2d90). The file name is ours.

#include "common.h"
#include "mathsconst.h"	// FUNCTION: 0x004e2d70 init:ZERO_FVECTOR
						// FUNCTION: 0x004e2d90 init:ID_FMATRIX
#include "thing.h"
#include "SoundManager.h"

//******************************************************************************************
void	PlayImpactSound(CThing *a, CThing *b)	// FUNCTION: 0x004e2e60
{
	if (a && b)
	{
		SINT	ma = a->GetSoundMaterial();
		SINT	mb = b->GetSoundMaterial();
		SINT	lo = ma, hi = mb;
		if (ma > mb)
		{
			lo = mb;
			hi = ma;
		}
		if (lo >= 1 && lo < 9)
		{
			CEffect	*effect;
			if (hi == 0x66)
				effect = SOUND.GetEffectByName("impact\\Wood");
			else
			{
				char	name[12];
				sprintf(name, "hit %d", rand() % 6 + 1);
				effect = SOUND.GetEffectByName(name);
			}
			if (effect)
				SOUND.PlayEffect(effect, a, 0.7f, ST_SETINITIALPOSITION, false, 0, 0, -1.f, false, 1.f, ST_GAME_SOUND, 0);
		}
	}
}
