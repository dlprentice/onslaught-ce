// SPDX-License-Identifier: GPL-3.0-only
// Native Video Options rows. The renderer owns frame-boundary activation; this file owns menu and save feedback.
#include "common.h"
#include "MenuItem.h"
#include "ce_notice.h"
#include <stdlib.h>

extern bool PortCeMenus();
extern "C" int CE_FullModelsChoice();
extern "C" int CE_FullModelsRequest(int);
extern "C" int CE_TerrainDetailChoice();
extern "C" int CE_TerrainDetailRequest(int);
extern "C" int CE_TerrainDetailStatus();
extern "C" int CE_AnisoChoice();
extern "C" int CE_AnisoRequest(int);
extern "C" int CE_AnisoChoiceToLevel(int);
extern "C" unsigned CE_AnisoHardwareMax();
extern "C" int CE_SettingSet(const char *, int);

bool PortCeQualityOptionsAvailable()
{
#ifdef CE_VERSION
    // Even a forced CE layer must not add controls to a faithful reference run.
    const char *fences[] = { "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD" };
    for (unsigned i = 0; i < sizeof(fences) / sizeof(fences[0]); ++i)
    {
        const char *value = getenv(fences[i]);
        if (value && *value) return false;
    }
    return PortCeMenus();
#else
    return false;
#endif
}

namespace
{
    enum QualityKind { FullModels, TerrainDistance, AnisoFiltering, QualityCount };
    // What the frame policy reports for a requested terrain choice (terrain_quality.h).
    enum TerrainStatus { TerrainPending = 0, TerrainActive = 1, TerrainResourceFallback = 2 };
    enum QualityResult { QualitySaved, QualitySessionOnly, QualityRefused };
    // Keep a session-only notice truthful after leaving and reopening Video Options.
    QualityResult results[QualityCount] = { QualitySaved, QualitySaved, QualitySaved };
    // The rows alive now (the front end's Options and a level's Pause Menu may each hold a set): their list is drawn at CE's row pitch.
    const void *liveRows[12];
    void Track(const void *row, bool alive)
    {
        for (unsigned i = 0; i < sizeof(liveRows) / sizeof(liveRows[0]); ++i)
            if (liveRows[i] == (alive ? 0 : row))
            {
                liveRows[i] = alive ? row : 0;
                return;
            }
    }

    int AnisoChoiceCount()
    {
        const unsigned maxHw = CE_AnisoHardwareMax();
        if (maxHw >= 16u) return 4;
        if (maxHw >= 8u) return 3;
        if (maxHw >= 2u) return 2;
        return 1;
    }

    const WCHAR *AnisoChoiceLabel(int value, QualityResult state, bool isCurrent)
    {
        static const WCHAR *const kPlain[4] = { L"Off", L"4x", L"8x", L"16x" };
        static const WCHAR *const kUnsaved[4] = { L"Off (not saved)", L"4x (not saved)", L"8x (not saved)", L"16x (not saved)" };
        static const WCHAR *const kRefused[4] = { L"Off (not changed)", L"4x (not changed)", L"8x (not changed)", L"16x (not changed)" };
        if (value < 0 || value > 3) value = 0;
        if (value == 1 && CE_AnisoHardwareMax() < 4u)
        {
            if (state == QualityRefused && isCurrent) return L"2x (not changed)";
            if (state == QualitySessionOnly) return L"2x (not saved)";
            return L"2x";
        }
        if (state == QualityRefused && isCurrent) return kRefused[value];
        if (state == QualitySessionOnly) return kUnsaved[value];
        return kPlain[value];
    }

    class QualityItem : public CMenuMenuItem
    {
        QualityKind kind;
    public:
        explicit QualityItem(QualityKind value) : CMenuMenuItem((EPCText)0, 3, true), kind(value)
        {
            Refresh();
            Track(this, true);
        }
        ~QualityItem() override { Track(this, false); }
        // The label never changes, as the original's never do ("Label:value", no space): a state follows the value.
        WCHAR *GetText() override
        {
            if (kind == FullModels) return const_cast<WCHAR *>(L"Full models:");
            if (kind == TerrainDistance) return const_cast<WCHAR *>(L"Terrain detail distance:");
            return const_cast<WCHAR *>(L"Anisotropic filtering:");
        }
        int GetNumChoices() override
        {
            return kind == AnisoFiltering ? AnisoChoiceCount() : 2;
        }
        WCHAR *GetChoiceText(int value) override
        {
            if (kind == AnisoFiltering)
                return const_cast<WCHAR *>(AnisoChoiceLabel(value, results[kind], value == GetValue()));
            // A refused change leaves the row on the choice still in effect, and says that it did not change.
            if (results[kind] == QualityRefused && value == GetValue())
            {
                if (kind == FullModels) return const_cast<WCHAR *>(value == 1 ? L"Yes (not changed)" : L"No (not changed)");
                return const_cast<WCHAR *>(value == 1 ? L"Extended (not changed)" : L"Normal (not changed)");
            }
            const bool unsaved = results[kind] == QualitySessionOnly;
            if (kind == FullModels)
                return const_cast<WCHAR *>(value == 1 ?
                    (unsaved ? L"Yes (not saved)" : L"Yes") : (unsaved ? L"No (not saved)" : L"No"));
            // The frame draws what it reports, not always what was asked. Only a completed resource check that
            // refused Extended says so; a request not yet sampled, or a level that has not started, is simply the choice.
            if (value == 1 && CE_TerrainDetailChoice() == 1 && CE_TerrainDetailStatus() == TerrainResourceFallback)
                return const_cast<WCHAR *>(unsaved ? L"Extended (using Normal, not saved)" : L"Extended (using Normal)");
            return const_cast<WCHAR *>(value == 1 ?
                (unsaved ? L"Extended (not saved)" : L"Extended") : (unsaved ? L"Normal (not saved)" : L"Normal"));
        }
        int GetValue() override
        {
            if (kind == FullModels) return CE_FullModelsChoice();
            if (kind == TerrainDistance) return CE_TerrainDetailChoice();
            int choice = CE_AnisoChoice();
            const int limit = AnisoChoiceCount() - 1;
            if (choice < 0) choice = 0;
            if (choice > limit) choice = limit;
            return choice;
        }
        void Commit() override
        {
            const bool changed = HasPendingChange();
            CMenuMenuItem::Commit();
            // A later native Apply may retry a session-only preference while another row changes.
            if (!changed && results[kind] == QualitySessionOnly) SetValue(GetValue());
        }
        void SetValue(int value) override
        {
            const int choices = GetNumChoices();
            if (!PortCeQualityOptionsAvailable() || value < 0 || value >= choices)
            {
                results[kind] = QualityRefused;
                mSelection = mValue = GetValue();
                return;
            }
            // Apply visits every native item. Unchanged rows must not manufacture saved preferences.
            if (value == GetValue() && results[kind] != QualitySessionOnly)
            {
                results[kind] = QualitySaved;
                return;
            }
            const int requestValue = kind == AnisoFiltering ? CE_AnisoChoiceToLevel(value) : value;
            const int queued = kind == FullModels ? CE_FullModelsRequest(requestValue) :
                kind == TerrainDistance ? CE_TerrainDetailRequest(requestValue) : CE_AnisoRequest(requestValue);
            if (!queued)
            {
                results[kind] = QualityRefused;
                mSelection = mValue = GetValue();
                return;
            }
            // A valid request applies next complete render frame even if persistence fails.
            // The settings writer preserves the old file on refusal; never undo the working session choice.
            const char *key = kind == FullModels ? "full_models" :
                kind == TerrainDistance ? "terrain_detail_distance" : "anisotropic_filtering";
            const bool saved = CE_SettingSet(key, requestValue) != 0;
            results[kind] = saved ? QualitySaved : QualitySessionOnly;
            if (!saved) PortCeSaveFailedNotice();
        }
    };
}

bool PortCeQualityRow(const void *item)
{
    if (!item) return false;
    for (unsigned i = 0; i < sizeof(liveRows) / sizeof(liveRows[0]); ++i)
        if (liveRows[i] == item) return true;
    return false;
}

CMenuItem *PortCeFullModelsItem() { return new((EMemoryType)0x80) QualityItem(FullModels); }
CMenuItem *PortCeTerrainDetailItem() { return new((EMemoryType)0x80) QualityItem(TerrainDistance); }
CMenuItem *PortCeAnisoFilteringItem() { return new((EMemoryType)0x80) QualityItem(AnisoFiltering); }
