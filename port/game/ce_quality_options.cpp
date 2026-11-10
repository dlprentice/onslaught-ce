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
    enum QualityKind { FullModels, TerrainDistance, QualityCount };
    // What the frame policy reports for a requested terrain choice (terrain_quality.h).
    enum TerrainStatus { TerrainPending = 0, TerrainActive = 1, TerrainResourceFallback = 2 };
    enum QualityResult { QualitySaved, QualitySessionOnly, QualityRefused };
    // Keep a session-only notice truthful after leaving and reopening Video Options.
    QualityResult results[QualityCount] = { QualitySaved, QualitySaved };

    class QualityItem : public CMenuMenuItem
    {
        QualityKind kind;
    public:
        explicit QualityItem(QualityKind value) : CMenuMenuItem((EPCText)0, 3, true), kind(value)
        {
            Refresh();
        }
        WCHAR *GetText() override
        {
            if (kind == FullModels)
                return const_cast<WCHAR *>(results[kind] == QualityRefused ? L"Full models (not applied): " : L"Full models: ");
            return const_cast<WCHAR *>(results[kind] == QualityRefused ? L"Terrain detail distance (not applied): " : L"Terrain detail distance: ");
        }
        int GetNumChoices() override { return 2; }
        WCHAR *GetChoiceText(int value) override
        {
            const bool unsaved = results[kind] == QualitySessionOnly;
            if (kind == FullModels)
                return const_cast<WCHAR *>(value == 1 ?
                    (unsaved ? L"On (not saved)" : L"On") : (unsaved ? L"Off (not saved)" : L"Off"));
            // The frame draws what it reports, not always what was asked. Only a completed resource check that
            // refused Extended says so; a request not yet sampled, or a level that has not started, is simply the choice.
            if (value == 1 && CE_TerrainDetailChoice() == 1 && CE_TerrainDetailStatus() == TerrainResourceFallback)
                return const_cast<WCHAR *>(unsaved ? L"Extended (using Normal, not saved)" : L"Extended (using Normal)");
            return const_cast<WCHAR *>(value == 1 ?
                (unsaved ? L"Extended (not saved)" : L"Extended") : (unsaved ? L"Normal (not saved)" : L"Normal"));
        }
        int GetValue() override
        {
            return kind == FullModels ? CE_FullModelsChoice() : CE_TerrainDetailChoice();
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
            if (!PortCeQualityOptionsAvailable() || (value != 0 && value != 1))
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
            const int queued = kind == FullModels ? CE_FullModelsRequest(value) : CE_TerrainDetailRequest(value);
            if (!queued)
            {
                results[kind] = QualityRefused;
                mSelection = mValue = GetValue();
                return;
            }
            // A valid request applies next complete render frame even if persistence fails.
            // The settings writer preserves the old file on refusal; never undo the working session choice.
            const bool saved = CE_SettingSet(kind == FullModels ? "full_models" : "terrain_detail_distance", value) != 0;
            results[kind] = saved ? QualitySaved : QualitySessionOnly;
            if (!saved) PortCeSaveFailedNotice();
        }
    };
}

CMenuItem *PortCeFullModelsItem() { return new((EMemoryType)0x80) QualityItem(FullModels); }
CMenuItem *PortCeTerrainDetailItem() { return new((EMemoryType)0x80) QualityItem(TerrainDistance); }
