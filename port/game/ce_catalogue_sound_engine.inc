// SPDX-License-Identifier: GPL-3.0-only
// Native adapter for Catalogue-owned sound previews. Included by the preview
// translation unit and by a synthetic host driver that supplies the same API.
namespace {
static_assert(sizeof(((CSample*)0)->mName) - 1 <= CE_CATALOGUE_SOUND_NAME_MAX,
    "Catalogue snapshots must retain every native sample name in full");
class CeCatalogueSoundOwner : public IAudibleThing
{
public:
    FVector GetSoundPos() { return FVector(0.f, 0.f, 0.f); }
    FMatrix GetSoundOrientation()
    {
        return FMatrix(FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 1.f));
    }
    FVector GetSoundVelocity() { return FVector(0.f, 0.f, 0.f); }
    void SampleFinishedPlaying(CSoundEvent*) {}
};
CeCatalogueSoundOwner sCatalogueSoundOwner;
char sCatalogueSoundName[256] = {0};
// Set while the owner plays a sample the catalogue made itself (a spoken line): Stop frees it.
void (*sCatalogueSampleRelease)() = 0;
const unsigned kCatalogueSoundWalkLimit = 65536;

bool CeCatalogueSoundNameValid(const CSample* sample)
{
    return sample && static_cast<const CPCSample*>(sample)->mDSBuffer && sample->mName[0] &&
        memchr(sample->mName, 0, sizeof(sample->mName)) != 0;
}
}

void PortCeCatalogueSoundVisit(CeCatalogueSoundVisitor visit, void* context)
{
    if (!visit) return;
    unsigned remaining = kCatalogueSoundWalkLimit;
    for (CSample* sample = SOUND.GetFirstSample(); sample && remaining; sample = sample->mNextSample, --remaining)
        if (CeCatalogueSoundNameValid(sample)) visit(sample->mName, context);
}

void PortCeCatalogueSoundStop()
{
    if (sCatalogueSoundName[0])
    {
        SOUND.KillSamplesForThing(&sCatalogueSoundOwner);
        sCatalogueSoundName[0] = 0;
    }
    if (sCatalogueSampleRelease) sCatalogueSampleRelease();
}

bool PortCeCatalogueSoundActive()
{
    if (!sCatalogueSoundName[0]) return false;
    if (SOUND.IsSamplePlaying(sCatalogueSoundName, &sCatalogueSoundOwner)) return true;
    sCatalogueSoundName[0] = 0;
    return false;
}

bool PortCeCatalogueSoundStart(const char* name)
{
    PortCeCatalogueSoundStop();
    if (!name || !*name) return false;
    unsigned length = 0;
    while (length < sizeof(sCatalogueSoundName) && name[length]) ++length;
    if (length == sizeof(sCatalogueSoundName)) return false;
    unsigned remaining = kCatalogueSoundWalkLimit;
    for (CSample* sample = SOUND.GetFirstSample(); sample && remaining; sample = sample->mNextSample, --remaining)
    {
        if (!CeCatalogueSoundNameValid(sample) || stricmp(sample->mName, name)) continue;
        memcpy(sCatalogueSoundName, sample->mName, strlen(sample->mName) + 1);
        SOUND.PlaySample(sample, &sCatalogueSoundOwner, 1.f, ST_FOLLOWDONTDIE, true, 0.f, 0.f, -1.0f, false, 1.f, false, true, ST_GAME_SOUND);
        return PortCeCatalogueSoundActive();
    }
    return false;
}
