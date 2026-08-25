FUNC_PTR(sGetItemTypeString,                CRefString (*)(GBID gbidItemType));
FUNC_PTR(AttribParamToString,               CRefString (*)(Attrib eAttrib, int32 nParam));
FUNC_PTR(FastAttribToString,                CRefString (*)(const FastAttribKey tKey));
FUNC_PTR(AttribStringToParam,                    int32 (*)(Attrib eAttrib, LPCSTR szParam));
FUNC_PTR(GBIDToStringAll,                   CRefString (*)(const GBID *gbid));
FUNC_PTR(GBIDToString,                      CRefString (*)(GBID gbid, GameBalanceType eGBType, const BOOL fUsedForXMLFields));
FUNC_PTR(StringListFetchLPCSTR,                 LPCSTR (*)(const SNO snoStringList, LPCSTR szLabel));

FUNC_PTR(ClearAssignedHero,                       void (*)(ActorCommonData *tACDItem));
FUNC_PTR(ACDInventoryItemAllowedInSlot,      GameError (*)(const ActorCommonData *tACDItem, const InventoryLocation *tInvLoc, const BOOL fSkipRequirements, const BOOL fSwapping));
FUNC_PTR(ACDAddToInventory,                       void (*)(ActorCommonData *tACDItem, const InventoryLocation *tLocation, const BOOL bMarkItemAsDirty, InventorySlotType eCameFrom));
FUNC_PTR(SACDInventoryPickupOrSpillOntoGround,    BOOL (*)(ActorCommonData *tACDPlayer, ActorCommonData *tACDItem, const BOOL bTryStashFirst, ACDID *pidACDStackDest, const BOOL bAllowAutoEquip));
FUNC_PTR(ItemHasLabel,                            BOOL (*)(const GBID gbidItem, const ItemLabel eItemLabel));
FUNC_PTR(SetBindingLevel,                         void (*)(ActorCommonData *tACD, s32 eBindingType));
FUNC_PTR(SetRecipient,                            void (*)(ActorCommonData *tACDItem, s32 nPlayerIndex));
FUNC_PTR(PopulateGenerator,                       void (*)(ActorCommonData *tACDItem, D3::Items::Generator *tGenerator, BOOL bSetRestoredBit));
FUNC_PTR(CItemGenerate,                          ACDID (*)(ACDNetworkName ann, D3::Items::Generator *tGenerator, ItemAffixFastAttribMap *arAffixSpecificAttributes));
FUNC_PTR(ItemsGenerator_ctor,                     void (*)(D3::Items::Generator *));
FUNC_PTR(ItemsGenerator_dtor,                     void (*)(D3::Items::Generator *));
FUNC_PTR(FlippyFindLandingLocation,               BOOL (*)(const ActorCommonData *tACDLaunching, const SNO snoActorFlippy, WorldPlace *tPlaceLanding, const Player *ptAssignedPlayer, const uintptr_t *pvBias, ACDID idACDKiller));
FUNC_PTR(FlippyDropCreateOnActor,                 void (*)(const ACDID idACDDropper, const ACDID idACDItem, const WorldPlace *tPlace));
FUNC_PTR(SItemGenerate,                          ACDID (*)(const OnlineService::ItemId *tItemId, const D3::Items::Generator *tGenerator, const WorldPlace *tPlace, float flScale, uint32 tName));
FUNC_PTR(AddItemToInventory,                      void (*)(Player *tPlayerFrom, D3::Items::Generator *tGenerator, BOOL bEquip));
FUNC_PTR(LootQuickCreateItem,                    ACDID (*)(const GBHandle *hGB, const BOOL fAutoIdentify));

FUNC_PTR(GetCosmeticItemType,         CosmeticItemType (*)(GBID gbidItem));
FUNC_PTR(ItemIsCosmeticItem,                      BOOL (*)(const GBID gbidItem));
FUNC_PTR(SetCraftLevel,                           void (*)(const CrafterType eType, Player *tPlayer, const int32 nLevel));
FUNC_PTR(Crafting_GetTransmogSlot,   InventorySlotType (*)(GBID gbidItem));
FUNC_PTR(ExperienceSetLevel,                      void (*)(const ACDID idACD, int32 nLevels, const BOOL fUpdateClient));
FUNC_PTR(ExperienceDropLootForAll,                void (*)(ActorCommonData *tACDLootee, ACDID idACDPrimaryLooter, uint32 dwGameEventNumber, BOOL fAutoIdentify, float flCameraBiasDist));
// d3hack-custom: 0x4C8C20 -- `(SGameRuleBits >> nBit) & 1`, a pure read through the game
// singleton. AddExperience tests bit 0x10 at 0x79FE74 and returns without granting anything if
// it is set, so anything hooking the front of that routine has to ask the same question before
// it commits state it cannot take back.
FUNC_PTR(GameRuleFlagTest,                       BOOL (*)(int32 nBit));
FUNC_PTR(SCosmeticItems_LearnCosmeticItem,        BOOL (*)(const ACDID idACDPlayer, const GBID gbidItem, CosmeticItemType eType, bool bPurchased));
FUNC_PTR(SCosmeticItems_LearnPet,                 BOOL (*)(const ACDID idACDPlayer, const GBID gbidItem, bool bPurchased));
FUNC_PTR(SItemPlayerExtractLegendaryPower,        BOOL (*)(Player *tPlayer, GBID gbidItem, GameError *eGameError));
FUNC_PTR(SItemCrafting_TryUnlockTransmog,         BOOL (*)(GBID gbidTransmog, Player *tPlayer, BOOL bMustBeInInventory, BOOL bSendFullUpdate));
FUNC_PTR(SItemCrafting_TryUnlockSecondaryTmogs,   BOOL (*)(GBID gbidTransmog, Player *tPlayer));
FUNC_PTR(SCrafterIncrementLevel,                  BOOL (*)(Player *tPlayer, CrafterType eType, uint32 nLevels));
FUNC_PTR(SItemCrafting_LearnRecipe,               void (*)(const ACDID idACDPlayer, const GBID gbidRecipe));
FUNC_PTR(sCrafterOnLevelUp,                       void (*)(const Player *tPlayer, CrafterType eType, const BOOL fChangedRank, const BOOL fSendToClient));
FUNC_PTR(LootRollForAncientLegendary,             void (*)(LootSpecifier *tSpecifier, const ACDID idACDLooter));
FUNC_PTR(LootItemIsLegendary,                     BOOL (*)(GBID gbidItem));  // d3hack-custom
FUNC_PTR(GlobalSNOGet,                             SNO (*)(const SNO eGlobalSNO));
FUNC_PTR(CameraSetZoomValue,                      void (*)(int nPlayer, float flZoom, float flBlendSeconds));  // d3hack-custom
FUNC_PTR(CameraApplyObserverByName,               void (*)(int nView, const char *szObserver, int nFlags));  // d3hack-custom
FUNC_PTR(SNOIsLoaded,                             BOOL (*)(int nGroup, int nSno));  // d3hack-custom: returns a BOOL, not a pointer
FUNC_PTR(GBEnumerate,                             void (*)(GameBalanceType eType, GBHandleList *listResults));
FUNC_PTR(GBAssetGet,                              void *(*)(void *ptMgr, SNO sno));
FUNC_PTR(GBRecordGet,                             void *(*)(const void *ptKey, void **ptHandle, s32 *pfFlag));
FUNC_PTR(GBRecordRelease,                         void (*)(void **ptHandle));
FUNC_PTR(GBGetHandlePool,                GBHandlePool* (*)());
FUNC_PTR(SNOToString,                       CRefString (*)(const SNOGroup snoGroup, const SNO snoHandle, const BOOL fUsedForXMLFields));
