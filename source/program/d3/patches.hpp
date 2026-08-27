#pragma once

#include <vector>

// Utility hook toggles (override with -D...).
#ifndef D3HACK_ENABLE_UTILITY_DEBUG_HOOKS
#define D3HACK_ENABLE_UTILITY_DEBUG_HOOKS 1
#endif

#ifndef D3HACK_ENABLE_UTILITY_SCRATCH
#define D3HACK_ENABLE_UTILITY_SCRATCH 0
#endif

struct GameParams;

namespace d3 {

    void PatchBuildlocker();
    void PatchVarResLabel();
    void PatchGraphicsPersistentHeapEarly();
    void PatchReleaseFPSLabel();
    void PatchDDMLabels();
    void PatchResolutionTargets();
    void PatchDynamicSeasonal();
    void UpdateDynamicSeasonalForSpawn(const GameParams *tParams);
    void PatchDynamicEvents();
    void PatchSeasonalItemGates();  // d3hack-custom
    void PatchInfiniteMp(bool enabled);
    void PatchBase();
    void ExtendTieredRiftTable();
    void ExtendParagonXpTable();
    void RaiseParagonStatLimits();
    void PatchItemTypeMaxSockets();  // d3hack-custom
    void PatchSocketAffixItemGroups();  // d3hack-custom
    void PatchRamaladniTargetTypes();  // d3hack-custom
    void NameSnoList();         // d3hack-custom
    void InspectSetBonuses();   // d3hack-custom
    void InspectParagonBonuses();  // d3hack-custom
    void PatchParagonStatValues();  // d3hack-custom
    void ShiftSetBonusTiers();  // d3hack-custom
    void DumpMonsterAffixes();  // d3hack-custom
    void DisableMonsterAffixes();  // d3hack-custom
    void DumpRiftTables();  // d3hack-custom
    void DumpTieredRiftLevels();  // d3hack-custom
    void PatchItemSocketCategoryFlags();  // d3hack-custom
    extern std::vector<unsigned int> g_arEquippableGbids;  // d3hack-custom
    void DumpPredicateRegistry();  // d3hack-custom
    void ProbeShowItemsOnGround();  // d3hack-custom

}  // namespace d3
