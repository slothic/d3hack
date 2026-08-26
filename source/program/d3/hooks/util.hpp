#pragma once  // d3hack-custom: this header defines inline objects; a second include
              // in one TU is a redefinition error, not a benign repeat.
#include "program/config.hpp"
#include "program/gui2/imgui_overlay.hpp"  // d3hack-custom: combat log
#include "program/d3/_util.hpp"
#include "program/d3/patches.hpp"
#include "program/d3/types/common.hpp"
#include "program/d3/types/attributes.hpp"
#include "program/fs_util.hpp"
#include "nn/fs.hpp"
#include "lib/hook/inline.hpp"
#include "lib/hook/replace.hpp"
#include "lib/hook/trampoline.hpp"
#include "lib/util/sys/modules.hpp"
#include "nn/os/os_thread_api.hpp"  // d3hack-custom: scan on a worker thread  // d3hack-custom: resolve pointers to module+offset
#include "nn/oe.hpp"

namespace d3 {

    HOOK_DEFINE_TRAMPOLINE(AttackSpeed) {
        // float PowerGetFormulaValueAtLevel(AttribGroupID idFastAttrib, const ActorCommonData *ptACD, SNO snoPower, uint8 *pbFormula, int32 nFormulaSize, int32 nLevel)
        static auto Callback(AttribGroupID idFastAttrib, ActorCommonData *ptACD, SNO snoPower, uint8 *pbFormula, int32 nFormulaSize, int32 nLevel) -> float {
            auto ret = Orig(idFastAttrib, ptACD, snoPower, pbFormula, nFormulaSize, nLevel);
            if (!global_config.rare_cheats.active || snoPower != 0x50860)
                return ret;
            const float mult = static_cast<float>(global_config.rare_cheats.attack_speed);
            if (mult <= 0.0f || mult == 1.0f)
                return ret;
            FastAttribKey tKey;
            tKey.nValue = ATTACKS_PER_SECOND_TOTAL;
            if (ptACD != nullptr) {
                ACD_AttributesSetFloat(ptACD, tKey, 3.0f);
            }
            PRINT_EXPR("ORIG ATKSPD: %f", ret);
            return ret * mult;
        }
    };

    HOOK_DEFINE_TRAMPOLINE(MoveSpeed) {
        // @ float CPlayerGetMoveSpeedForStickInput(ActorCommonData *tACDPlayer, Player *tPlayer, SNO snoPower, float flMinClientWalkSpeed)
        static auto Callback(ActorCommonData *tACDPlayer, Player *tPlayer, SNO snoPower, float flMinClientWalkSpeed) -> float {
            auto MoveSpeed = Orig(tACDPlayer, tPlayer, snoPower, flMinClientWalkSpeed);
            if (!global_config.rare_cheats.active)
                return MoveSpeed;
            const auto mult = static_cast<float>(global_config.rare_cheats.move_speed);
            if (mult <= 0.0f)
                return MoveSpeed;
            return MoveSpeed * mult;
        }
    };

    HOOK_DEFINE_TRAMPOLINE(EquipAny) {
        //   @ GameError ACDInventoryItemAllowedInSlot(const ActorCommonData *tACDItem, const InventoryLocation *tInvLoc, const BOOL fSkipRequirements, const BOOL fSwapping)
        static auto Callback(const ActorCommonData *tACDItem, const InventoryLocation *tInvLoc, const BOOL fSkipRequirements, const BOOL fSwapping) -> GameError {
            auto rGameError = Orig(tACDItem, tInvLoc, fSkipRequirements, fSwapping);
            if (!global_config.rare_cheats.active || !global_config.rare_cheats.equip_any_slot)
                return rGameError;
            return GAMEERROR_NONE;
        }
    };

    // d3hack-custom: primals granted so far in this world. Reset from sInitializeWorld,
    // which fires per Greater Rift, so the budget is per-rift rather than per-session.
    inline int g_nPrimalsThisWorld = 0;

    // d3hack-custom: per-world loot-roll tally. Lets a build prove it is handing normal
    // rifts back to the stock ancient/primal roll without having to farm until something
    // rare actually drops -- these tick on every roll, not on the rare event.
    inline int g_nAncientRollsThisWorld  = 0;
    inline int g_nAncientStockThisWorld  = 0;
    inline int g_nAncientRaisedThisWorld = 0;

    // d3hack-custom: the true, unclamped greater-rift tier.
    // The greater-rift tier as the UI shows it.
    //
    // sTieredLootRunGetLevel() is a plain global load (SGame -> [+0x58] -> [+0x738] ->
    // [+0x14]), which is what we want here: the loot path frequently runs with a null
    // looter ACD, so there is nothing to hang an attribute lookup off.
    //
    // It is stored ZERO-BASED -- a GR 200 reads back as 199 -- which is why the game's
    // own chance math is `rank - level - 1`, i.e. rank minus the displayed tier. Add the
    // one back here so every threshold in this file and in config.toml is written as the
    // number the player sees. Negative means "not in a tiered rift".
    inline auto TrueGRLevel() -> int {
        // Null-checked because this now runs on the experience path, i.e. on every kill: an
        // unresolved symbol here would be a null call several times a second rather than a
        // one-off at startup.
        if (TieredLootRunGetLevel == nullptr)
            return -1;
        const int nRaw = static_cast<int>(TieredLootRunGetLevel());
        return nRaw < 0 ? -1 : nRaw + 1;
    }

    // d3hack-custom: the tier of the rift just left, so the completion bonus still counts.
    //
    // The big rift-close award is handed out by Orek in TOWN, after the rift world is
    // already gone and TrueGRLevel() reads -1 -- which is precisely the grant worth
    // multiplying. Latch the tier on the way out and keep it until the world changes
    // again after something has actually used it.
    //
    // The latch is deliberately not cleared the moment you reach town: several grants may
    // make up one turn-in. It does mean that leaving a rift and going straight to a bounty
    // without visiting Orek carries the bonus into that one world before it clears.
    inline int  g_nLatchedGRLevel = -1;
    inline bool g_bLatchedGRUsed  = false;

    // Marks the latch used ONLY when the latch is what answered. Marking it on an in-rift
    // kill is what used to kill the completion bonus outright: every kill inside the rift set
    // the flag, and the very next world change -- walking out to Orek, the whole reason the
    // latch exists -- then expired it before his award ever arrived. So the big turn-in grant,
    // the one grant most worth multiplying, never got the bonus in any run where the player
    // had killed something.
    inline auto BonusGRLevel() -> int {
        const int nNow = TrueGRLevel();
        if (nNow > 0) {
            g_nLatchedGRLevel = nNow;
            g_bLatchedGRUsed  = false;
            return nNow;
        }
        if (g_nLatchedGRLevel > 0)
            g_bLatchedGRUsed = true;  // paid out from the latch; it may expire next world
        return g_nLatchedGRLevel;
    }

    // Called from sInitializeWorld: drop a latch that has already paid out.
    inline void ExpireGRLatchOnWorldChange() {
        if (g_bLatchedGRUsed) {
            g_nLatchedGRLevel = -1;
            g_bLatchedGRUsed  = false;
        }
    }

    // d3hack-custom: extra experience while inside a high greater rift.
    //
    // The static ExperienceMultiplier rewrites `mov x20, x1` at 0x79FE70 into a left shift,
    // so it is a fixed power of two decided at patch time and cannot depend on where you
    // are. This scales instead at 0x79FE84, AFTER that instruction has run, so the two
    // compose without either having to know about the other: the shift lands first, this
    // reads the shifted value out of x20 and writes the result back to the same register.
    //
    // Deliberately NOT hooked on the multiplier patch site itself -- an inline hook
    // re-executes the original instruction from its trampoline, so sharing an address with a
    // rewritten instruction would make the result depend on patch order. 0x79FE84 is plain
    // `mov w26, wzr` and is not a patch site for anything.
    //
    // BonusGRLevel() is negative outside a tiered rift (bar the completion latch above),
    // so the bonus stays confined to greater rifts without a separate check.
    //
    // Both this and the Pool of Reflection bonus below share the one callback: two inline
    // hooks cannot occupy the same address, and this one owns it.
    // d3hack-custom: Pool of Reflection -> permanent stacking XP bonus.
    //
    // Stock behaviour: a pool grants +25% XP against a limited XP budget (10% of a level),
    // stacks 10x, and is wiped on death. The Altar of Rites node that stops the death-wipe
    // makes it background noise -- always on, never meaningful. So make each pool touched
    // worth a permanent multiplier step instead.
    //
    // The count lives in sd:/config/d3hack-nx/pools_u<profile>.txt, NOT in the game save.
    // The save is checksummed and cloud-synced and a bad write there costs a character; this
    // file is the mod's own and fs_util::WriteAllAtomic already does tmp/bak/rename properly.
    //
    // ================================================================================
    // WHAT "THE GAME CAP" ACTUALLY IS
    // ================================================================================
    // All of this is read off the grant routine at 0x79FE40, which is the only way
    // experience enters a hero. It has three entries, none of them indirect:
    //     0x7A0988  BL, from the wrapper at 0x7A08F0, w3 = 1   -- a real grant
    //     0x7A0AA0  B,  a TAIL CALL from that same wrapper, w3 = 1 -- also a real grant
    //     0x79FFC8  BL, its own recursion for rested XP,     w3 = 0
    // The tail call is the reason this is hooked inside the function rather than at its call
    // sites: a scan for BL alone misses 0x7A0AA0 completely.
    //
    //     AddExperience(ActorCommonData *pACD, u64 uXP, u32 nFlags, u32 bAllowRested)
    //
    //     if (GameRuleFlag(0x10)) return           // 0x79FE74, experience disabled outright
    //     if (uXP == 0) return                     // 0x79FE7C
    //     x28 = uXP                                // still to hand out
    //   loop @0x79FE98:
    //     bPara = ExperienceIsPastNormalLevels()       // 0x47B8A0
    //     lvl   = attr(bPara ? ALT_LEVEL : LEVEL)
    //     cap   = bPara ? 20000 : attr(LEVEL_CAP)      // that 20000 is MOVZ w27 @0x79FE8C
    //     if (lvl >= cap) goto done                    // <-- x28 IS SIMPLY ABANDONED
    //     need  = bPara ? 0x4853B0() : 0x4852D0()      // XP still owed for this level
    //     if (x28 < need) { store x28 as progress; goto done }
    //     LevelUp(); x28 -= need; goto loop
    //
    // Three consequences, each checked against the disassembly rather than assumed:
    //
    //  1. THERE IS NO 64-BIT TOTAL TO OVERFLOW. Progress is stored as "XP still owed for the
    //     current level" (0x485420, a HI/LO attribute pair) and is by construction smaller
    //     than one level's cost. A lifetime total is never accumulated anywhere. Every
    //     comparison on the path is unsigned -- cmp/b.hs at 0x79FEE8, subs at 0x79FF20,
    //     ucvtf at 0x79FF94 -- so any u64 whatsoever is arithmetically safe, 2^64-1 included.
    //
    //  2. EXPERIENCE PAST THE LEVEL CAP IS DISCARDED BY THE GAME, silently, in vanilla.
    //     Nothing that can be put in x1 makes that worse.
    //
    //  3. The one real hazard is ITERATION COUNT. The loop calls LevelUp (0x7A0080) once per
    //     level gained, and LevelUp writes attributes, re-reads the XP table and fires the
    //     level-up presentation. Handing over enough to cross thousands of levels in one call
    //     means thousands of those in one frame. That is what the code below bounds. Not
    //     overflow -- overflow cannot happen.
    //
    // ================================================================================
    // THE BOUND, AND WHY IT IS EXACT
    // ================================================================================
    // Before the grant we can ask the game the same question its loop asks: `need`, the XP
    // still owed for the current level. Hand over at most need * N.
    //
    //     iteration 1 consumes exactly `need`
    //     iteration i>1 consumes cost(L+i-1), the FULL cost of a later level
    //     level costs are non-decreasing, and `need` is a remainder of cost(L), so
    //         cost(L+i-1) >= cost(L) >= need
    //     so N iterations consume at least N*need, which is everything handed over, and
    //     iteration N+1 cannot start.
    //
    // => at most N level-ups per grant, N = PoolOfReflectionLevelsPerGrant.
    //
    // The one bit of slack is the normal->paragon changeover, where the first paragon level
    // can cost less than the last normal one. The loop re-reads bPara every iteration so it
    // stays correct; it can just overshoot N by a small factor, once, on that one grant.
    //
    // ================================================================================
    // WHY THE CARRY DRAINS, WITH NO TIMER
    // ================================================================================
    // Whatever the bound does not deliver is CARRIED, not dropped, and rides along on the
    // next grant. The previous design bounded the grant by a fixed XP number, and that really
    // could stall: if every kill produced more than the ceiling the backlog grew for ever and
    // capping it silently ate experience.
    //
    // Bounding by LEVELS cannot do that, because the thing being drained is finite. There are
    // at most (cap - level) levels left to buy, so the carry is fully spent within
    // (cap - level)/N grants however large it is -- 800 kills for a 20000-level climb at
    // N = 25 -- and at the cap it stops growing, because past that point the game discards
    // experience anyway and banking more would be pretending to save something unspendable.
    //
    // A wall-clock timer was the other candidate and is strictly worse: it would have to call
    // AddExperience from a tick using a hero pointer cached on some earlier frame, and a
    // stale ActorCommonData* is a crash. Bounding by levels needs no cached pointer, no
    // second call site and no re-entrancy -- it only ever edits x1 of a grant already in
    // flight.
    namespace pools {
        // inline, not static: this header is pulled into more than one translation unit, and
        // file statics would give each of them a private copy of the count while the installed
        // hook used only one of them.
        //
        // Keyed by Switch user profile index, so the count is PER ACCOUNT: every character on
        // that profile shares it and a second profile keeps its own. (Profile index rather
        // than a Battle.net id -- ConsoleGamerProfileGetAccount exists at 0x574E0 but its
        // signature is unverified, and calling it blind is not worth the risk.)
        inline char s_szPath[64] = "sd:/config/d3hack-nx/pools.txt";
        inline int  s_nCount     = -1;     // -1 = not loaded
        inline u64  s_uCarry     = 0ull;   // scaled XP earned but not yet handed over
        // Largest uNeed seen, per level kind (0 = normal, 1 = paragon). uNeed is the XP
        // still owed for the current level, so its MAXIMUM is a full level cost, sampled
        // just after a level-up. The grant bound is built from that -- see the note there.
        inline u64  s_uMaxNeed[2] {};
        // d3hack-custom: XP CENSUS. "Kill 300 mobs and the bar barely moves, kill 25 more
        // and it moves the same amount" is either the multiplier failing to apply or the
        // grant being metered, and those want opposite fixes. Counting every call and every
        // early exit separates them instead of guessing a third time.
        inline u32  s_nXpCalls   = 0;   // reached the scaling path
        inline u32  s_nXpRested  = 0;   // w3 == 0: the rested-XP recursion, deliberately unscaled
        inline u32  s_nXpRuleOff = 0;   // game rule 0x10: experience off (challenge rift)
        inline u32  s_nXpZero    = 0;   // raw amount was 0
        inline u32  s_nXpNoACD   = 0;   // hero pointer failed the heap test -> vanilla amount
        inline u32  s_nXpCapped  = 0;   // uWant exceeded the bound: metered, remainder carried
        inline u64  s_uXpRaw     = 0ull;
        // How much XP the UNSCALED rested path carries. rested ~= calls on every census
        // line, so every kill fires a second grant that skips the multiplier entirely. If
        // that path is a big share of the total, the effective multiplier swings with
        // however much bonus pool is left -- which would read exactly as "it only applies
        // sometimes". Unmeasured until now.
        inline u64  s_uXpRestedRaw = 0ull;
        inline u64  s_uXpOut     = 0ull;
        inline u64  s_uXpLastBound = 0ull;
        inline u64  s_uXpLastNeed  = 0ull;
        inline u64  s_uXpTick    = 0ull;

        inline bool s_bDirty     = false;
        // Set when the state file exists but could not be read. Writing after that would
        // replace a file we could not understand with a fresh zero -- so it latches saving
        // off for the session instead. Losing the session's progress beats overwriting the
        // account's.
        inline bool s_bReadFailed = false;
        inline u64  s_uLastSaveTick = 0ull;
        inline int  s_nSuppressedTouches = 0;

        // Attribute keys are (param << 12) | id, param -1 for the unparameterised ones.
        // 0x79FE88 builds exactly these three: MOVN w23,#0xFD1 -> 0xFFFFF02F, then +0xA/B/C.
        inline constexpr u32 kKeyLevel    = 0xFFFFF039u;  // LEVEL
        inline constexpr u32 kKeyLevelCap = 0xFFFFF03Au;  // LEVEL_CAP
        inline constexpr u32 kKeyAltLevel = 0xFFFFF03Bu;  // ALT_LEVEL (paragon)
        // Experience still owed for the current level, stored as a HI/LO pair of 32-bit
        // attributes. 0x4852D0 and 0x4853B0 exist to read exactly these and reassemble them
        // (`bfi x0, hi, #32, #32`), but this reads them directly: everything here then goes
        // through ACD_AttributesGetInt and nothing else, which is one already-exercised call
        // instead of three more entry points reached from inside a hook.
        inline constexpr u32 kKeyXpNextHi    = 0xFFFFF029u;  // EXPERIENCE_NEXT_HI
        inline constexpr u32 kKeyXpNextLo    = 0xFFFFF02Au;  // EXPERIENCE_NEXT_LO
        inline constexpr u32 kKeyAltXpNextHi = 0xFFFFF02Bu;  // ALT_EXPERIENCE_NEXT_HI
        inline constexpr u32 kKeyAltXpNextLo = 0xFFFFF02Cu;  // ALT_EXPERIENCE_NEXT_LO
        // Banked rested experience, the HI/LO pair 0x485810 reads. This IS the Pool of
        // Reflection budget: a pool deposits into it, and the grant routine spends it at
        // REST_EXPERIENCE_BONUS_PERCENT (0x2F) per kill -- the +25% seen in the trace.
        inline constexpr u32 kKeyRestXpHi    = 0xFFFFF02Du;  // REST_EXPERIENCE_HI
        inline constexpr u32 kKeyRestXpLo    = 0xFFFFF02Eu;  // REST_EXPERIENCE_LO

        // Half the u64 range: room for any realistic carry while leaving the top bit clear,
        // so no signed misread of it downstream can come out negative.
        inline constexpr u64 kMaxCarry = 0x7FFFFFFFFFFFFFFFull;

        // The Switch system tick is 19.2 MHz on every unit, and Ryujinx matches it.
        inline constexpr u64 kTicksPerSecond   = 19200000ull;
        inline constexpr int kMaxTouchesPerWorld = 16;
        // How stale the on-disk carry is allowed to get. A world change persists it too, but
        // closing the game from inside a rift never reaches one, so bound the loss by time.
        inline constexpr u64 kSaveIntervalTicks = 60ull * kTicksPerSecond;

        inline void XpCensusTick() {
            if (!global_config.rare_cheats.xp_census)
                return;
            const u64 uNow = svcGetSystemTick();
            if (s_uXpTick != 0ull && (uNow - s_uXpTick) < (5ull * d3::pools::kTicksPerSecond))
                return;
            s_uXpTick = uNow;
            PRINT("[d3hack-xp] calls=%u rested=%u ruleoff=%u zero=%u noacd=%u capped=%u",
                  s_nXpCalls, s_nXpRested, s_nXpRuleOff, s_nXpZero, s_nXpNoACD, s_nXpCapped)
            PRINT("[d3hack-xp]   restedRaw=%llu (unscaled, granted as-is)",
                  static_cast<unsigned long long>(s_uXpRestedRaw))
            PRINT("[d3hack-xp]   raw=%llu out=%llu carry=%llu lastNeed=%llu lastBound=%llu",
                  static_cast<unsigned long long>(s_uXpRaw),
                  static_cast<unsigned long long>(s_uXpOut),
                  static_cast<unsigned long long>(s_uCarry),
                  static_cast<unsigned long long>(s_uXpLastNeed),
                  static_cast<unsigned long long>(s_uXpLastBound))
            s_nXpCalls = s_nXpRested = s_nXpRuleOff = s_nXpZero = s_nXpNoACD = s_nXpCapped = 0;
            s_uXpRaw = s_uXpOut = s_uXpRestedRaw = 0ull;
        }
        inline u64  s_uLastTouchTick    = 0ull;
        inline int  s_nTouchesThisWorld = 0;
        // Last rest-XP budget seen, per actor, for pool detection. See DetectPool().
        inline const void *s_pRestACD  = nullptr;
        inline u64         s_uLastRest = 0ull;
        // Last level seen at a grant, used only to check the assumption the bound rests on.
        // The actor is part of the key: followers earn experience through the same routine and
        // comparing their level against a hero's would invent level jumps that never happened.
        inline const void *s_pLastACD   = nullptr;
        inline s32         s_nLastLevel = -1;
        inline bool        s_bLastPara  = false;
        // First few grants are traced step by step. If the log stops part way through one of
        // these lines, the thing it was about to do is the thing that killed the process.
        inline int         s_nTrace     = 0;

        inline void BuildPath() {
            int nUser = 0;
            if (GetPrimaryProfileUserIndex != nullptr)
                nUser = GetPrimaryProfileUserIndex();
            if (nUser < 0 || nUser > 9)
                nUser = 0;
            const char *pre = "sd:/config/d3hack-nx/pools_u";
            int         i   = 0;
            for (; pre[i] != 0; ++i)
                s_szPath[i] = pre[i];
            s_szPath[i++] = static_cast<char>('0' + nUser);
            const char *suf = ".txt";
            for (int k = 0; suf[k] != 0; ++k)
                s_szPath[i++] = suf[k];
            s_szPath[i] = 0;
        }

        inline int AppendU64(char *pOut, u64 v) {
            char tmp[24];
            int  n = 0;
            if (v == 0ull)
                tmp[n++] = '0';
            while (v > 0ull && n < 20) {
                tmp[n++] = static_cast<char>('0' + static_cast<int>(v % 10ull));
                v /= 10ull;
            }
            for (int k = 0; k < n; ++k)
                pOut[k] = tmp[n - 1 - k];
            return n;
        }

        inline void Save() {
            if (s_nCount < 0 || s_bReadFailed)
                return;
            // 10 digits of count + separator + 19 digits of carry (kMaxCarry is 2^63-1) + a
            // trailing newline is 31 bytes. AppendU64 is bounded at 20 digits either way.
            char buf[48];
            int  len = AppendU64(buf, static_cast<u64>(s_nCount));
            buf[len++] = '\n';
            len += AppendU64(buf + len, s_uCarry);
            buf[len++] = '\n';
            std::string err;
            // Stamped before the attempt, not after it: a failing card would otherwise be
            // retried on every single kill instead of once a minute.
            s_uLastSaveTick = svcGetSystemTick();
            if (!fs_util::WriteAllAtomic(s_szPath, std::string_view(buf, static_cast<size_t>(len)),
                                         "pool state", err)) {
                // Leave the dirty flag set so the next world change tries again, and say so --
                // a write that fails quietly is how the carry would evaporate unnoticed.
                static int s_nFailed = 0;
                if (s_nFailed < 5) {
                    ++s_nFailed;
                    PRINT("[d3hack-custom] pools: could not write %s (%s) -- keeping the carry in "
                          "memory and retrying at the next world change",
                          s_szPath, err.c_str())
                }
                return;
            }
            s_bDirty = false;
        }

        inline void Load() {
            if (s_nCount >= 0)
                return;
            s_nCount = 0;
            s_uCarry = 0ull;
            BuildPath();
            nn::fs::FileHandle fh {};
            if (R_SUCCEEDED(nn::fs::OpenFile(&fh, s_szPath, nn::fs::OpenMode_Read))) {
                // The file EXISTS, so anything that stops us reading its CONTENT is a
                // failure to report, not an absence to quietly replace with zero: otherwise one
                // bad read overwrites a real count with 0 at the next pool touch, and
                // WriteAllAtomic removes the .bak as soon as that write succeeds.
                //
                // A zero-length file is the one exception. It holds nothing, so there is
                // nothing to protect and nothing to lose by writing over it.
                s64 size = 0;
                if (!R_SUCCEEDED(nn::fs::GetFileSize(&size, fh)) || size >= 64) {
                    s_bReadFailed = true;
                } else if (size > 0) {
                    char buf[64] {};
                    if (!R_SUCCEEDED(nn::fs::ReadFile(fh, 0, buf, static_cast<u64>(size)))) {
                        s_bReadFailed = true;
                    } else {
                        const int n = static_cast<int>(size);
                        int       i = 0;
                        // line 1: pools touched. Files written before the carry existed hold
                        // only this line, and still parse.
                        u64 v = 0ull;
                        while (i < n && buf[i] >= '0' && buf[i] <= '9') {
                            if (v < 100000000ull)
                                v = (v * 10ull) + static_cast<u64>(buf[i] - '0');
                            ++i;
                        }
                        s_nCount = static_cast<int>(v);
                        while (i < n && (buf[i] < '0' || buf[i] > '9'))
                            ++i;
                        // line 2: experience carried over from a previous grant.
                        u64 c = 0ull;
                        while (i < n && buf[i] >= '0' && buf[i] <= '9') {
                            const u64 d = static_cast<u64>(buf[i] - '0');
                            if (c > ((kMaxCarry - d) / 10ull)) {
                                c = kMaxCarry;
                                break;
                            }
                            c = (c * 10ull) + d;
                            ++i;
                        }
                        s_uCarry = c;
                    }
                }
                nn::fs::CloseFile(fh);
            }
            s_uLastSaveTick = svcGetSystemTick();
            if (s_bReadFailed) {
                PRINT("[d3hack-custom] pools: %s exists but could not be read -- saving is "
                      "disabled for this session so the file is not overwritten with zero. Move "
                      "it aside to start fresh.",
                      s_szPath)
                return;
            }
            PRINT("[d3hack-custom] pools: %d touched, %llu xp carried (%s)", s_nCount,
                  static_cast<unsigned long long>(s_uCarry), s_szPath)
        }

        // The cap is applied on the way OUT, not written into the stored count. Clamping the
        // stored value meant that lowering PoolOfReflectionMaxCount once destroyed every pool
        // above it permanently, and that a file loaded above the cap kept its full value for
        // the session and then lost the difference at the next touch. The count on disk is
        // simply the number of pools touched; the cap is a setting, and settings should be
        // reversible.
        inline int Count() {
            if (s_nCount <= 0)
                return 0;
            const int nCap = global_config.rare_cheats.pool_xp_cap;
            return (nCap > 0 && s_nCount > nCap) ? nCap : s_nCount;
        }

        inline void Add(int n) {
            Load();
            if (n <= 0 || s_nCount > (2000000000 - n))
                return;
            s_nCount += n;
            Save();
        }

        // Called on world change. The carry moves on every kill and writing the SD card that
        // often would be both slow and pointless, so it is persisted at world boundaries --
        // which every rift and town trip passes through.
        inline void Flush() {
            s_nTouchesThisWorld = 0;
            if (s_nSuppressedTouches > 0) {
                PRINT("[d3hack-custom] pools: %d touch(es) ignored in that world by the one-per-"
                      "second brake. A few is normal for adjacent pools; a steady stream means "
                      "0x2BC520 is the wrong anchor.",
                      s_nSuppressedTouches)
                s_nSuppressedTouches = 0;
            }
            if (s_bDirty)
                Save();
        }

        // Closing the game from inside a rift never reaches a world change, so a world-change-
        // only save would lose the whole session's carry to a HOME-close or a crash. Bound that
        // to one minute. Called from the experience path, which is the only code here that runs
        // regularly, and it writes at most one small file per minute.
        inline void SaveIfStale() {
            if (!s_bDirty || s_bReadFailed)
                return;
            const u64 uNow = svcGetSystemTick();
            if (uNow - s_uLastSaveTick < kSaveIntervalTicks)
                return;
            Save();
        }

        // 0x2BC520 has never been proven to fire exactly once per pool. It is reached
        // virtually, has no direct callers, and could just as easily be a tooltip builder
        // that runs on every frame the buff is up -- which would both inflate the count and
        // hammer the SD card, because a touch writes the state file.
        //
        // Two independent brakes, so a wrong anchor degrades instead of exploding:
        //   - at most one touch per second, which kills a per-frame caller outright
        //   - at most 16 per world, which is far more pools than a rift level contains
        // Either brake tripping is a diagnosis, and the log says so in plain words.
        inline bool ShouldCountTouch() {
            if (s_nTouchesThisWorld >= kMaxTouchesPerWorld) {
                static bool s_bSaid = false;
                if (!s_bSaid) {
                    s_bSaid = true;
                    PRINT_LINE("[d3hack-custom] pools: 16 touches in one world -- 0x2BC520 fires "
                               "more often than pools exist, so it is the wrong anchor. Counting "
                               "is paused until the next world; PoolOfReflectionXpPercent = 0 "
                               "turns the feature off entirely.");
                }
                return false;
            }
            const u64 uNow = svcGetSystemTick();
            if (s_uLastTouchTick != 0ull && (uNow - s_uLastTouchTick) < kTicksPerSecond) {
                ++s_nSuppressedTouches;
                return false;
            }
            s_uLastTouchTick = uNow;
            ++s_nTouchesThisWorld;
            return true;
        }

        // Mirrors the shift PortCheatCodes patches into 0x79FE70 (`mov x20,x1` ->
        // `add x20,xzr,x1,lsl #n`). The hook is on the function ENTRY, so the amount here has
        // not been through that shift yet and the level bound has to account for it: without
        // this the bound is out by 2^n, and a large enough value wraps the shift outright.
        inline u32 StaticShift() {
            const auto &rc = global_config.rare_cheats;
            if (!rc.active || rc.xp_multiplier <= 1)
                return 0u;
            u32 shift = 0u;
            while ((shift + 1u) <= 30u &&
                   (1u << (shift + 1u)) <= static_cast<u32>(rc.xp_multiplier))
                ++shift;
            return shift;
        }

        // Mirrors the paragon ceiling PortCheatCodes patches into the MOVZ at 0x79FE8C --
        // the value the grant loop compares ALT_LEVEL against, including the MOVZ imm16
        // rounding that patch has to live with, which is not a detail that can be skipped:
        // with MaxParagonLevel = 2000000000 the MOVZ can only carry the top 16 bits, so the
        // cap the game actually enforces is (2000000000 >> 16) << 16 = 1999962112. bAtCap has
        // to be decided against that number and not against the configured one.
        inline s32 ParagonCap() {
            const auto &rc = global_config.rare_cheats;
            if (!rc.active || rc.max_paragon_level <= 20000)
                return 20000;
            const u32  want = static_cast<u32>(rc.max_paragon_level);
            const bool hi   = want > 65535u;
            const u32  imm  = hi ? (want >> 16) : want;
            return static_cast<s32>(hi ? (imm << 16) : imm);
        }

        // (100 + percent * pools) / 100. percent <= 1000 and the count is an int, so the
        // product cannot leave 64 bits even with a corrupt count file.
        inline u64 Numerator() {
            const int nPct = global_config.rare_cheats.pool_xp_percent;
            if (nPct <= 0)
                return 100ull;
            return 100ull + (static_cast<u64>(nPct) * static_cast<u64>(Count()));
        }

        inline s32 GetAttrInt(ActorCommonData *pACD, u32 uKey) {
            if (ACD_AttributesGetInt == nullptr)
                return 0;
            FastAttribKey tKey;
            tKey.nValue = static_cast<s32>(uKey);
            return static_cast<s32>(ACD_AttributesGetInt(pACD, tKey));
        }

        // Reassembles the HI/LO pair exactly as 0x4853B0 / 0x4852D0 do: the LO half is
        // zero-extended and the HI half is inserted above it.
        inline u64 NeedForNextLevel(ActorCommonData *pACD, bool bPara) {
            const u32 uHi = static_cast<u32>(GetAttrInt(pACD, bPara ? kKeyAltXpNextHi : kKeyXpNextHi));
            const u32 uLo = static_cast<u32>(GetAttrInt(pACD, bPara ? kKeyAltXpNextLo : kKeyXpNextLo));
            return (static_cast<u64>(uHi) << 32) | static_cast<u64>(uLo);
        }

        // Same test as 0x47B8A0: paragon levels are being earned once the hero is at a level
        // cap that has actually been unlocked past 59. That routine also rejects non-hero
        // actors up front; skipping that check only makes this stricter, because a non-hero
        // reads a level cap of 0 and fails `> 59` anyway.
        inline bool IsEarningParagon(ActorCommonData *pACD) {
            const s32 nCap = GetAttrInt(pACD, kKeyLevelCap);
            if (nCap <= 59)
                return false;
            return GetAttrInt(pACD, kKeyLevel) >= nCap;
        }

        // POOL DETECTION, EVIDENCE STAGE. This counts NOTHING. It only reports.
        //
        // Two earlier attempts were wrong and both are worth remembering:
        //
        //  1. Hooking 0x2BC520. That address is a TOOLTIP BUILDER -- it formats
        //     "pool of reflection %d" at 0x2BD91C after fetching GlobalSNOGet(0x2D8) for the
        //     buff name. It is not the activation path, it never fired once, and installing a
        //     trampoline on it cost an evening of crashes.
        //
        //  2. Watching banked rest experience (REST_EXPERIENCE_HI/LO). A pool does deposit
        //     there, but with the Altar node the routine never subtracts: 0x79FFD4 branches
        //     past `sub x1, x20, x22` and 0x7A004C rewrites the pool to xpToNext on EVERY
        //     kill. So the value tracks progress to the next level and jumps at level-up.
        //     A rising edge on it counts level-ups, not pools.
        //
        // What is left is the attribute table. There is no BUFF_ICON_COUNT; instead
        // BUFF_ICON_START_TICK0..25 and BUFF_ICON_END_TICK0..25 are parameterised by power
        // SNO, and the occupied slots ARE the stacks -- that `%d` is counting them. Each pool
        // touched should write a fresh game tick into one slot, and at maximum stacks a new
        // pool should overwrite the oldest slot rather than add one, so watching for a slot
        // whose tick CHANGES survives the permanent-buff problem that killed attempt 2.
        //
        // Should is doing a lot of work in that sentence, which is the whole point of this
        // being log-only. Touch exactly one pool and read what moved.
        inline constexpr u32 kAttrBuffStartTick0 = 0x252u;
        inline constexpr int kPoolSlots          = 12;
        inline int  s_nPoolBuffSno = -1;
        inline u32  s_arSlotTick[kPoolSlots] = {};
        inline bool s_bSlotsSeeded  = false;
        inline int  s_nSlotOccupied = 0;
        inline int  s_nSlotLogs     = 0;
        inline long s_nSlotChanges  = 0;
        inline long s_nGrants       = 0;

        // Attribute keys are id | (param << 12); param -1 gives the 0xFFFFFxxx form used for
        // the unparameterised ones. Here the param is the buff's power SNO.
        inline u32 BuffSlotKey(int nSlot) {
            return (kAttrBuffStartTick0 + static_cast<u32>(nSlot)) |
                   ((static_cast<u32>(s_nPoolBuffSno) & 0xFFFFFu) << 12);
        }

        // Watch the pool buff's stack slots and count a pool when the number of OCCUPIED slots
        // goes up.
        //
        // Designed for someone playing normally, not running a lab procedure: pools turn up
        // when they turn up, and area damage means dozens of experience grants a second. So the
        // loud events -- occupied count moving, a pool counted -- are never rate limited,
        // because they are inherently rare; only the per-slot tick chatter is capped, and a
        // heartbeat keeps reporting totals after that cap so the rate is still visible.
        //
        // Counting is wired up rather than held back, because the risk here is nothing like the
        // hook that caused the crash: these are attribute READS, they cannot corrupt anything,
        // and the worst failure is a wrong integer in sd:/config/d3hack-nx/pools_u<N>.txt.
        // Delete that file to reset, or set PoolOfReflectionSlotProbe = false to stop counting.
        // Every increment is logged with the evidence that produced it.
        inline void ProbePoolSlots(ActorCommonData *pACD) {
            if (!global_config.rare_cheats.pool_slot_probe || pACD == nullptr)
                return;
            if (s_nPoolBuffSno < 0) {
                if (GlobalSNOGet == nullptr)
                    return;
                // GlobalSNOGet already exists (symbols/game.inc, SNO(*)(const SNO)) --
                // declaring a second one collided in the symbol-name hash table.
                s_nPoolBuffSno = static_cast<int>(GlobalSNOGet(static_cast<SNO>(0x2D8)));
                PRINT("[d3hack-pool] PoolOfReflectionBuff sno = %d (0x%X), watching %d slots from "
                      "attr 0x252",
                      s_nPoolBuffSno, static_cast<unsigned>(s_nPoolBuffSno), kPoolSlots)
                if (s_nPoolBuffSno <= 0 || (static_cast<u32>(s_nPoolBuffSno) & ~0xFFFFFu) != 0u) {
                    PRINT("[d3hack-pool] sno %d does not fit a 20-bit attribute param -- slot "
                          "probe disabled, the keys would be garbage",
                          s_nPoolBuffSno)
                    s_nPoolBuffSno = 0;
                    return;
                }
            }
            if (s_nPoolBuffSno <= 0)
                return;

            ++s_nGrants;
            u32 arNow[kPoolSlots];
            int nUsed = 0;
            for (int k = 0; k < kPoolSlots; ++k) {
                arNow[k] = static_cast<u32>(GetAttrInt(pACD, BuffSlotKey(k)));
                if (arNow[k] != 0u)
                    ++nUsed;
            }

            if (!s_bSlotsSeeded) {
                s_bSlotsSeeded  = true;
                s_nSlotOccupied = nUsed;
                for (int k = 0; k < kPoolSlots; ++k)
                    s_arSlotTick[k] = arNow[k];
                PRINT("[d3hack-pool] baseline: %d of %d slots occupied; [0]=%u [1]=%u [2]=%u "
                      "[3]=%u",
                      nUsed, kPoolSlots, arNow[0], arNow[1], arNow[2], arNow[3])
                return;
            }

            for (int k = 0; k < kPoolSlots; ++k) {
                if (arNow[k] == s_arSlotTick[k])
                    continue;
                ++s_nSlotChanges;
                if (s_nSlotLogs < 60) {
                    ++s_nSlotLogs;
                    PRINT("[d3hack-pool] slot %d tick %u -> %u (grant %ld)", k, s_arSlotTick[k],
                          arNow[k], s_nGrants)
                }
                s_arSlotTick[k] = arNow[k];
            }

            // Never rate limited: occupied count moving is the event worth seeing.
            if (nUsed != s_nSlotOccupied) {
                const int nGained = nUsed - s_nSlotOccupied;
                s_nSlotOccupied   = nUsed;
                if (nGained > 0) {
                    Add(nGained);
                    PRINT("[d3hack-pool] POOL: occupied %d -> %d, counted +%d, total=%d, "
                          "xp x%llu.%02llu",
                          nUsed - nGained, nUsed, nGained, Count(), Numerator() / 100ull,
                          Numerator() % 100ull)
                } else {
                    PRINT("[d3hack-pool] occupied %d -> %d (dropped %d, not counted)",
                          nUsed - nGained, nUsed, -nGained)
                }
            }

            // Heartbeat, so the rate stays visible after the per-slot log fills up.
            if ((s_nGrants % 400L) == 0L) {
                PRINT("[d3hack-pool] heartbeat: grants=%ld occupied=%d slot-changes=%ld count=%d",
                      s_nGrants, s_nSlotOccupied, s_nSlotChanges, Count())
            }
        }

        // Decide how much of the grant to hand over. Never returns
        // less than the game was going to grant on its own, on any path, so no failure in here
        // can leave a hero levelling slower than vanilla.
        inline u64 Deliver(ActorCommonData *pACD, u64 uRaw) {
            Load();

            // Everything here is in the units of the routine's x1 argument, i.e. BEFORE the
            // ExperienceMultiplier shift at 0x79FE70. uMaxX1 is the largest value that shift
            // can take without wrapping.
            const u32 uShift = StaticShift();
            const u64 uMaxX1 = 0xFFFFFFFFFFFFFFFFull >> uShift;
            if (uRaw >= uMaxX1)
                return uRaw;  // the game's own shift already wraps this; do not make it worse

            // ---- our factors, in 128-bit so nothing wraps before it is looked at ---------
            unsigned __int128 uWant = uRaw;
            const u64         uNum  = Numerator();
            if (uNum > 100ull)
                uWant = (uWant * uNum) / 100u;
            const int nBonus = global_config.rare_cheats.xp_gr_bonus;
            // BonusGRLevel() marks the latch used itself, and only when the latch is what
            // answered -- see the comment on it. Setting that flag here instead is what used
            // to expire the latch before the rift-completion award could use it.
            if (nBonus > 1 && BonusGRLevel() >= global_config.rare_cheats.xp_gr_bonus_min_gr)
                uWant *= static_cast<unsigned>(nBonus);
            // Worst case above: 2^64 * (1000 * 2^31) / 100 * 4096 < 2^118. No 128-bit wrap.

            // ---- how much of it the game can absorb without a level-up storm -------------
            // Starts at the vanilla amount, so every way of failing to inspect the hero --
            // a pointer that does not look like one, an unresolved symbol, an attribute that
            // reads zero -- ends in "grant exactly what the game was going to grant". The
            // opposite default, unbounded, would turn a failed read into the largest possible
            // level-up storm.
            bool bAtCap = false;
            u64  uBound = uRaw;
            ++s_nXpCalls;
            s_uXpRaw += uRaw;
            if (pACD == nullptr)
                ++s_nXpNoACD;
            ProbePoolSlots(pACD);

            if (pACD != nullptr) {
                const bool bPara  = IsEarningParagon(pACD);
                const s32  nLevel = GetAttrInt(pACD, bPara ? kKeyAltLevel : kKeyLevel);
                const s32  nCap   = bPara ? ParagonCap() : GetAttrInt(pACD, kKeyLevelCap);
                bAtCap            = (nCap > 0 && nLevel >= nCap);
                if (s_nTrace < 12) {
                    PRINT("[d3hack-trace] xp: acd=%p raw=%llu para=%d level=%d cap=%d",
                          static_cast<void *>(pACD), static_cast<unsigned long long>(uRaw),
                          bPara ? 1 : 0, static_cast<int>(nLevel), static_cast<int>(nCap))
                }

                // The bound above rests on level costs never decreasing. That is true of both
                // XP tables, but it is an assumption about game DATA rather than about code,
                // so measure it instead of trusting it: nLevel - s_nLastLevel is what the
                // previous grant actually bought. It should never sit far above N.
                if (s_pLastACD == pACD && s_nLastLevel >= 0 && s_bLastPara == bPara &&
                    nLevel > s_nLastLevel) {
                    const int nGained = static_cast<int>(nLevel - s_nLastLevel);
                    const int nAllow  = 2 * global_config.rare_cheats.pool_xp_levels_per_grant;
                    if (nGained > nAllow) {
                        static int s_nLogged = 0;
                        if (s_nLogged < 8) {
                            ++s_nLogged;
                            PRINT("[d3hack-custom] pools: a single grant bought %d levels, bound "
                                  "was %d -- level costs are not monotonic here. Lower "
                                  "PoolOfReflectionLevelsPerGrant if the game stutters.",
                                  nGained, global_config.rare_cheats.pool_xp_levels_per_grant)
                        }
                    }
                }
                s_pLastACD   = pACD;
                s_nLastLevel = nLevel;
                s_bLastPara  = bPara;

                const u64 uNeed = NeedForNextLevel(pACD, bPara);
                s_uXpLastNeed = uNeed;
                if (s_nTrace < 12) {
                    ++s_nTrace;
                    PRINT("[d3hack-trace] xp: need=%llu atcap=%d carry=%llu",
                          static_cast<unsigned long long>(uNeed), bAtCap ? 1 : 0,
                          static_cast<unsigned long long>(s_uCarry))
                }
                // The bound is applied at the cap too. It costs nothing there -- the loop
                // exits on its first test -- and it means a wrong answer from ParagonCap()
                // cannot turn into an unbounded grant.
                const u64 uLevels =
                    static_cast<u64>(global_config.rare_cheats.pool_xp_levels_per_grant);

                // !! THE BOUND MUST NOT BE BUILT FROM uNeed ALONE !!
                //
                // uNeed is the XP still owed for the CURRENT level, not a level's cost. It
                // falls toward zero as the level completes, so `uNeed * N` collapsed with it:
                // just after a level-up the cap was a whole level's cost times N and the
                // carry dumped in one go; just before the next one the cap was almost nothing
                // and the grant fell back to the vanilla floor. Consecutive kills therefore
                // paid wildly different amounts, and a one-shot award like Orek's rift
                // completion landed wherever it happened to fall in that cycle. Reported from
                // a real game, and raising PoolOfReflectionLevelsPerGrant did NOT help,
                // because the swing is in the multiplicand, not the multiplier.
                //
                // The safety argument only ever required each of the N iterations to be
                // covered by something no larger than the level it buys. Iteration 1 consumes
                // exactly uNeed; iterations 2..N each consume a FULL level cost, which is >=
                // any full cost seen earlier because level costs are non-decreasing. So keep
                // the largest uNeed observed -- that IS a full cost -- and use it for the
                // remaining N-1. Same "at most N level-ups" guarantee, with a cap that no
                // longer depends on where you are inside a level.
                u64 &uMaxNeed = s_uMaxNeed[bPara ? 1 : 0];
                if (uNeed > uMaxNeed)
                    uMaxNeed = uNeed;

                // uNeed == 0 is not a trustworthy answer: the loop reads it as "level up
                // unconditionally". Fall through to the vanilla floor below instead.
                u64 uCeiling = 0ull;
                if (uNeed != 0ull && uLevels != 0ull) {
                    const u64 uExtra = uLevels - 1ull;
                    u64       uRest  = 0ull;
                    if (uExtra != 0ull && uMaxNeed != 0ull)
                        uRest = (uMaxNeed > (0xFFFFFFFFFFFFFFFFull / uExtra))
                                    ? 0xFFFFFFFFFFFFFFFFull
                                    : (uMaxNeed * uExtra);
                    uCeiling = (uRest > (0xFFFFFFFFFFFFFFFFull - uNeed))
                                   ? 0xFFFFFFFFFFFFFFFFull
                                   : (uNeed + uRest);
                }
                // uCeiling is what the LOOP may consume, so convert it back into the units of
                // the argument we are about to hand over.
                uBound = uCeiling >> uShift;
            }
            if (uBound > uMaxX1)
                uBound = uMaxX1;
            if (uBound < uRaw)
                uBound = uRaw;  // never below what the game was already granting
            s_uXpLastBound = uBound;

            // ---- spend the carry, bank the remainder ------------------------------------
            // At the cap the game throws experience away itself, so banking more would be
            // pretending to save something unspendable. The existing carry is left alone
            // rather than dropped: it is per account, and another hero may still be climbing.
            if (!bAtCap)
                uWant += static_cast<unsigned __int128>(s_uCarry);

            s_uXpOut += (uWant <= static_cast<unsigned __int128>(uBound))
                            ? static_cast<u64>(uWant) : uBound;
            if (uWant > static_cast<unsigned __int128>(uBound))
                ++s_nXpCapped;
            XpCensusTick();

            if (uWant <= static_cast<unsigned __int128>(uBound)) {
                if (!bAtCap && s_uCarry != 0ull) {
                    s_uCarry = 0ull;
                    s_bDirty = true;
                }
                SaveIfStale();
                return static_cast<u64>(uWant);
            }
            if (bAtCap)
                return uBound;

            const unsigned __int128 uRest = uWant - static_cast<unsigned __int128>(uBound);
            const u64               uNew  = (uRest > static_cast<unsigned __int128>(kMaxCarry))
                                                ? kMaxCarry
                                                : static_cast<u64>(uRest);
            if (uNew == kMaxCarry && s_uCarry != kMaxCarry) {
                PRINT("[d3hack-custom] pools: carry saturated at %llu -- the multiplier is far "
                      "past what %d levels per grant can absorb; the excess is dropped",
                      static_cast<unsigned long long>(kMaxCarry),
                      global_config.rare_cheats.pool_xp_levels_per_grant)
            }
            s_uCarry = uNew;
            s_bDirty = true;
            SaveIfStale();
            return uBound;
        }
    }  // namespace pools


    // d3hack-custom: print the bytes that ended up at the hook sites, once, after patches.
    //
    // This exists to settle a question guesswork could not: exl's HookFuncImpl has two ways of
    // taking a site. If the callback is within +/-128MB it swaps in ONE 4-byte `B`. If it is
    // not, it writes `LDR X17,#8 / BR X17 / 8-byte literal` -- 16 bytes, or 20 with a leading
    // NOP for alignment -- and then any PATCH landing inside that window lands in the middle of
    // a jump, because patches are applied after hooks are installed (main.cpp: InstallHooks
    // then ApplyPatches). 0x79FE8C (patch_paragon_cap_05) sits 8 bytes past 0x79FE84.
    //
    // Read the words and stop arguing about it. Read-only, one shot.
    inline void DumpExperienceHookSite() {
        static bool s_done = false;
        if (s_done)
            return;
        s_done = true;
        const uintptr_t uBase = GameOffset(0);
        for (u32 off = 0x79FE40u; off < 0x79FEA0u; off += 0x10u) {
            const auto *p = reinterpret_cast<const u32 *>(uBase + off);
            PRINT("[d3hack-diag] site %06X: %08X %08X %08X %08X", off, p[0], p[1], p[2], p[3])
        }
        const auto *q = reinterpret_cast<const u32 *>(uBase + 0xA40AE0u);
        PRINT("[d3hack-diag] site A40AE0: %08X %08X %08X %08X", q[0], q[1], q[2], q[3])
    }

    // d3hack-custom: WHERE THIS MAY BE HOOKED, AND WHAT IT COST TO FIND OUT
    //
    // Hooking the ENTRY of AddExperience (0x79FE40) with a trampoline kills the game, and it
    // does so without the callback ever running. Proven, not guessed:
    //
    //   - A staged switch (ExperienceHookMode) ran the hook installed but inert -- the callback
    //     returns Orig immediately. Still fatal.
    //   - The entry trace fires as the callback's first statement and appears in Ryujinx's
    //     unbuffered svcOutputDebugString stream. It never appeared. The callback is never
    //     entered before the process dies.
    //   - Ryujinx: UndefinedInstructionException at 0x09E09610, opcode 0x00000001, with
    //     PC == LR and a frame pointer holding a module data address. 0x09E09610 is main
    //     +0x1905610, i.e. `app_globals`. That is a RET through a smashed frame -- a stack
    //     imbalance -- on the main game thread, roughly two seconds after the world loads.
    //   - The same emulator log covers 29 launches. Builds from 11:36 through 17:08 never
    //     faulted once. The fault appears with the first build that moved this hook.
    //
    // The mechanism is still unexplained. What is established is the boundary: this site is
    // fatal, 0x79FE6C is not, and 0x79FE6C carried 28 launches without a single fault. So the
    // hook lives at 0x79FE6C, and the two things the entry was chosen for are handled here
    // instead:
    //
    //   - the early exits. 0x79FE6C sits BEFORE `bl 0x4C8C20` (0x79FE74) and the two branches
    //     that return without granting anything, so the flag is read directly through
    //     GameRuleFlagTest rather than relied upon. Spending carried experience on a grant that
    //     never happens would lose it for good.
    //   - the ExperienceMultiplier shift at 0x79FE70, which has not run yet at this address.
    //     StaticShift() mirrors it so the level bound is computed in the units the loop will
    //     actually consume.
    //
    // If this ever has to move again: 0x79FE84 was also fatal. Two sites inside this function
    // are known bad, one is known good. Do not move it without an A/B and a Ryujinx log.
    //
    // Registers at 0x79FE6C (`mov w21, w2`), and an inline callback runs BEFORE that
    // instruction:
    //   x1   the amount, pre-shift. 0x79FE70 `mov x20,x1` has not run yet.
    //   x19  the hero, from 0x79FE60 `mov x19,x0`. ctx->X[0] is NOT the hero -- x0 was
    //        overwritten with #0x10 at 0x79FE64.
    //   w3   the caller's fourth argument, untouched between entry and here.
    //
    // The routine is entered three ways and all three pass through: `BL 0x7A0988`, the TAIL
    // CALL `B 0x7A0AA0` from the same wrapper (a BL-only scan misses it), and the routine's own
    // recursion at 0x79FFC8 for rested XP, which passes w3 = 0.
    // Snapshot of ExperienceHookMode, set at install. See the ladder in the callback.
    inline int s_nXpHookMode = 0;

    HOOK_DEFINE_INLINE(HighGRExperience) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            // ExperienceHookMode, and the ladder matters more than the feature right now:
            //
            //   1  RETURN IMMEDIATELY. No log call, no config read, no game call, no 128-bit
            //      arithmetic -- the callback touches nothing and uses no stack beyond its own
            //      frame. If this still crashes, the hook's mere presence is fatal and nothing
            //      in the body can be responsible.
            //   2  Exactly what the last build that survived 28 launches did: the greater-rift
            //      factor, saturating 64-bit arithmetic, one call to BonusGRLevel(), nothing
            //      else. No pools, no attribute reads, no logging on the hot path.
            //   3  Full: pools, the level bound, the carry.
            //
            // Read the mode from a copy taken at install time. Reaching into global_config from
            // here is itself a memory access this ladder is trying to hold constant.
            if (s_nXpHookMode <= 1)
                return;

            const int nBonus = global_config.rare_cheats.xp_gr_bonus;

            if (s_nXpHookMode == 2) {
                // Byte-for-byte the old body, apart from BonusGRLevel() now setting the latch
                // itself (a one-line semantic fix with no effect on stack or call depth).
                if (nBonus <= 1)
                    return;
                if (BonusGRLevel() < global_config.rare_cheats.xp_gr_bonus_min_gr)
                    return;
                const u64 uXP  = ctx->X[1];
                const u64 uMax = static_cast<u64>(INT64_MAX) / static_cast<u64>(nBonus);
                ctx->X[1] = uXP > uMax ? static_cast<u64>(INT64_MAX) : uXP * static_cast<u64>(nBonus);
                return;
            }

            static int s_nEnter = 0;
            const bool bTrace   = (s_nEnter < 8);
            if (bTrace) {
                ++s_nEnter;
                PRINT("[d3hack-trace] xp enter: acd=%016llX raw=%llu rested=%u",
                      static_cast<unsigned long long>(ctx->X[19]),
                      static_cast<unsigned long long>(ctx->X[1]),
                      static_cast<unsigned>(ctx->W[3]))
            }

            // w3 == 0 is the rested-XP recursion at 0x79FFC8, whose amount is derived from the
            // amount already scaled here and clamped to the rested pool. Scaling it again would
            // be double counting.
            const u64 uRaw = ctx->X[1];

            // Log every LARGE grant individually with its w3, because the census only shows
            // totals. The rested path delivered 8,843,177,984 off a 41,849,104 kill -- 211x
            // the raw amount and 15x what the scaled path produced on the same event. That
            // cannot be "derived from the amount already scaled here and clamped to the
            // rested pool", which is the reasoning this hook uses to skip w3 == 0. So find
            // out what those big grants actually are: if the rift/GR completion award arrives
            // with w3 == 0, then the single biggest XP source in the game has never been
            // multiplied at all.
            if (global_config.rare_cheats.xp_census && uRaw >= 1000000000ull) {
                PRINT("[d3hack-xpbig] w3=%u raw=%llu %s", static_cast<unsigned>(ctx->W[3]),
                      static_cast<unsigned long long>(uRaw),
                      (ctx->W[3] == 0u) ? "<-- RESTED PATH, NOT SCALED" : "(scaled path)")
            }

            if (ctx->W[3] == 0u) {
                ++pools::s_nXpRested;
                pools::s_uXpRestedRaw += uRaw;
            }
            // Fall through when asked to: the "would be double counting" reasoning is false,
            // so this path can take the same multipliers and the same level bound as any
            // other grant. Routing it through Deliver rather than scaling it inline is what
            // keeps the level-up storm protection and the carry working for it too.
            const bool bRested = (ctx->W[3] == 0u);
            if ((bRested && !global_config.rare_cheats.xp_scale_rested) || uRaw == 0ull) {
                if (!bRested)
                    ++pools::s_nXpZero;
                pools::XpCensusTick();
                return;
            }
            // 0x4C8C20 is `(SGameRuleBits >> nBit) & 1`. The routine tests bit 0x10 nine
            // instructions from here and returns without granting anything if it is set --
            // a challenge rift, for one. Read it now, because the carry cannot be un-spent.
            if (GameRuleFlagTest != nullptr && GameRuleFlagTest(0x10) != 0) {
                ++pools::s_nXpRuleOff;
                pools::XpCensusTick();
                if (bTrace)
                    PRINT_LINE("[d3hack-trace] xp: game rule 0x10 set, experience is off here");
                return;
            }
            if (bTrace)
                PRINT_LINE("[d3hack-trace] xp: gate passed");

            // The game heap is [0x10_0000_0000, 0x80_0000_0000). A bare "not tiny" test would
            // pass a stack or .text address into ACD_AttributesGetInt, which dereferences
            // [x0+0x168] with no check of its own. Failing this test costs the level bound;
            // failing to make it costs the process.
            const uintptr_t uACD  = static_cast<uintptr_t>(ctx->X[19]);
            auto           *pHero = (uACD >= 0x1000000000ull && uACD < 0x8000000000ull)
                                        ? reinterpret_cast<ActorCommonData *>(uACD)
                                        : nullptr;

            ctx->X[1] = pools::Deliver(pHero, uRaw);
        }
    };

    // d3hack-custom: read (and scale) the numbers behind every skill.
    //
    // HOW THIS WAS FOUND, because none of it is guessable:
    //
    //   1. rodata has "Script Formula 0".."Script Formula 63" and a native named
    //      "PowerGetFormulaValue" at 0xE51CB6. No adrp/add xref -- the name lives in a
    //      script-binding TABLE in rodata instead.
    //   2. Searching rodata for a qword equal to that string address lands at 0xD68A70. The
    //      table is 6 qwords per binding, {name, ptr, 0x403, func, ptr, 0x403}, so the native
    //      for PowerGetFormulaValue is the next func slot: 0x8E0480.
    //   3. 0x8E0480 is only the script-VM wrapper -- it pops arg 1 as a double, fcvtzs it to
    //      an int, and calls the real one at 0x9B7F50: `float PowerGetFormulaValue(int idx)`.
    //   4. 0x9B7F50 bounds idx at 63, resolves the CURRENT power's SNO, looks the index up in
    //      a 64-entry key table at 0xED272C, and calls 0x9A0900(sno, key) for the value.
    //
    // So every skill number in the game is an ATTRIBUTE on the power's SNO, and 0x9A0900 is
    // the single door all of them come through. Hooking it beats patching the .pow asset:
    // no asset accessor is needed (GBAssetGet at 0x6C4940 is bound to the GameBalance manager
    // and cannot fetch a Power at all), and the same hook both reads and rewrites.
    //
    // The key encodes the formula index in packed decimal -- index 0 is 0x41100, 9 is 0x41190,
    // 10 is 0x41200, 63 is 0x41730. Verified against all 64 entries of the table:
    //
    //     key = 0x41100 + (idx / 10) * 0x100 + (idx % 10) * 0x10
    //
    // which inverts exactly, so the log can name the formula without carrying the table.
    //
    // 0x9A0900 is a hot, general power-attribute getter, so the probe only reports keys in the
    // script-formula range and only the FIRST time it sees a given (sno, index) pair. Volume
    // is therefore bounded by how many distinct skill numbers actually get evaluated, not by
    // frame rate.
    //
    // Scaling is config-driven on purpose: PowerFormulaScaleSno / ...Index / ...Percent. Once
    // the probe log shows which formula of which power is the one you want, three config
    // values change it -- no rebuild, no hardcoded SNO. Index -1 scales every formula of that
    // power.
    inline constexpr u32 kFormulaKeyBase = 0x41100u;

    inline int FormulaIndexFromKey(u32 uKey) {
        if (uKey < kFormulaKeyBase)
            return -1;
        const u32 d = uKey - kFormulaKeyBase;
        if ((d & 0xFu) != 0u || d > 0x630u)
            return -1;
        const u32 uTens = d >> 8;
        const u32 uOnes = (d >> 4) & 0xFu;
        if (uOnes > 9u)
            return -1;
        return static_cast<int>(uTens * 10u + uOnes);
    }

    struct FormulaSeen {
        s32 sno;
        s32 idx;
    };
    inline constexpr int kFormulaSeenMax = 2048;
    inline FormulaSeen  s_arFormulaSeen[kFormulaSeenMax] = {};
    inline int          s_nFormulaSeen = 0;

    // d3hack-custom: find the POWER SNO behind a formula read.
    //
    // The first probe run assumed 0x9A0900's first argument was the power SNO. It is not: the
    // 215 distinct values logged were all near 2e9 and decompose as (n << 16) | k -- runtime
    // attribute-group handles, not SNOs. They change per cast, which makes them useless to key
    // a config knob on. (The one genuine SNO in that log, 488544 for the Vision of Enmity
    // power, never appeared among them, which is the tell.)
    //
    // Where the SNO actually is, read off 0x8A0260:
    //
    //     rec   = *(base + 0x120) + (handle & 0xFFFF) * 0x148     // 0x8A02B0..0x8A02B8
    //     rec[0x00]  == handle                                     // validated at 0x8A02C0
    //     rec[0x114] -> out1, and out1 is what 0x9A0900 receives   // 0x8A02D0
    //     rec[0x118] -> out2
    //
    // So the power-instance record is 0x148 bytes and the SNO is one of its fields. Rather
    // than walk that pointer chain from the globals -- which would mean calling 0x93D0 and
    // declaring another symbol, and the last new symbol collided in the name hash -- hook
    // 0x8A02D0 inline, where **x8 is already the record pointer**. No chain, no new symbol, no
    // re-entrancy.
    //
    // x8 is xzr on the not-found path (0x8A02CC falls straight into 0x8A02D0), so the null
    // check is not optional.
    //
    // Dumps the whole record for the first few distinct handles. rec[0x114] is printed too, so
    // these lines join to the [d3hack-power] value lines by their shared id.
    inline constexpr int kPowerRecDumps = 6;
    inline int           s_nPowerRecDumps = 0;
    inline u32           s_arPowerRecSeen[kPowerRecDumps] = {};

    HOOK_DEFINE_INLINE(PowerRecordProbe) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            // Retired: the record turned out to be the SCRIPT-CONTEXT record, not a power
            // record. Its +0x24 holds the script entry-point name ("OnStart",
            // "Buff0OnRemovalEffect") and there is no SNO anywhere in its 0x148 bytes. Kept
            // behind a config that is now never set, purely so the next person does not repeat
            // the dump. The real answer is AttribIdToPowerSno below.
            if (!global_config.rare_cheats.power_record_probe || s_nPowerRecDumps >= kPowerRecDumps)
                return;
            const uintptr_t uRec = static_cast<uintptr_t>(ctx->X[8]);
            if (uRec < 0x1000000000ull || uRec >= 0x8000000000ull)
                return;
            const auto *w = reinterpret_cast<const u32 *>(uRec);
            const u32   h = w[0];
            for (int i = 0; i < s_nPowerRecDumps; ++i)
                if (s_arPowerRecSeen[i] == h)
                    return;
            s_arPowerRecSeen[s_nPowerRecDumps] = h;
            ++s_nPowerRecDumps;
            PRINT("[d3hack-prec] handle %08X  attribId %08X (rec+0x114)  out2 %08X", h,
                  w[0x114 / 4], w[0x118 / 4])
            for (u32 o = 0; o < 0x148u; o += 0x20u) {
                PRINT("[d3hack-prec]   +%03X  %08X %08X %08X %08X %08X %08X %08X %08X", o,
                      w[(o + 0x00) / 4], w[(o + 0x04) / 4], w[(o + 0x08) / 4], w[(o + 0x0C) / 4],
                      w[(o + 0x10) / 4], w[(o + 0x14) / 4], w[(o + 0x18) / 4], w[(o + 0x1C) / 4])
            }
        }
    };

    // d3hack-custom: attrib-group id -> the power's real SNO.
    //
    // 0x9A0900's first argument is NOT a power SNO. It is a runtime attribute-group handle,
    // (n << 16) | k, minted per cast -- the first probe run logged 215 of them all sitting near
    // 2e9, while the one genuine SNO in that log (488544, the Vision of Enmity power) never
    // appeared among them. Keying anything on those handles is worthless: they change every
    // cast and every launch.
    //
    // The conversion exists and the game does it itself. The script binding `PowerGetSNO`
    // (0x8DF980, found in the binding table at 0xD60000..0xD80000 -- 1158 of them, 6 qwords
    // each) wraps 0x9B7B80, which is:
    //
    //     0x89D860()                 -> current power-instance handle
    //     0x8A0260(h, &attribId, &_) -> attribId, the very value 0x9A0900 takes
    //     0x9A30B0(attribId)         -> THE POWER SNO
    //
    // So the last step is all that is needed, and it takes the argument already in hand. Called
    // through GameOffset rather than a declared symbol on purpose: the last symbol added to the
    // table collided in the name hash and broke the build.
    // d3hack-custom: which Script Formula indices to scale, as a 64-bit mask.
    //
    // Condemn (X1_Crusader_Condemn, sno 266627) is why this exists. Its formulas are
    //
    //     f0=3  f1=11.6  f5=15  f11=0.5  f16=15  f17=2.5  f18=10  f19=2  f20=1  f21=15
    //
    // and f1=11.6 is the 1160% weapon damage, which pins the units: the rest are plain yards
    // and seconds. That leaves FOUR plausible radii -- f5, f16, f21 at 15 and f18 at 10 -- with
    // no way to tell them apart from the outside. Scaling them one at a time is four relaunches;
    // scaling all four at once is one, and it cannot touch damage (f1), charge time (f19) or
    // the coefficients, because those indices are simply not in the list.
    //
    // Parsed once into a bitmask. Index -1 in the single-index setting still means "all".
    inline u64  s_uFormulaMask     = 0ull;
    inline bool s_bFormulaMaskDone = false;

    inline auto FormulaMask() -> u64 {
        if (s_bFormulaMaskDone)
            return s_uFormulaMask;
        s_bFormulaMaskDone = true;
        const std::string &sIn = global_config.rare_cheats.power_formula_index_list;
        size_t             i   = 0;
        while (i < sIn.size()) {
            while (i < sIn.size() && (sIn[i] < '0' || sIn[i] > '9'))
                ++i;
            if (i >= sIn.size())
                break;
            int n = 0;
            while (i < sIn.size() && sIn[i] >= '0' && sIn[i] <= '9') {
                n = n * 10 + (sIn[i] - '0');
                ++i;
            }
            if (n >= 0 && n < 64)
                s_uFormulaMask |= (1ull << n);
        }
        return s_uFormulaMask;
    }

    inline auto AttribIdToPowerSno(u32 uAttribId) -> s32 {
        using Fn = s32 (*)(u32);
        auto *pFn = reinterpret_cast<Fn>(GameOffset(0x9A30B0));
        if (pFn == nullptr)
            return -1;
        return pFn(uAttribId);
    }

    // d3hack-custom: name a power SNO, once each, so the probe log needs no guesswork.
    //
    // SNOToString(group, sno, 0) hands back a CRefString and debug.hpp already uses it exactly
    // this way. Group 0x1F is Powers -- taken from the global-SNO registration at 0x4E1904,
    // which registers Community_Buff_NestingPortalSpawn with `mov w0, #0x1f`.
    //
    // Called at most once per distinct power (there were 31 in a play session), never on the
    // hot path, because SNOToString allocates.
    inline constexpr int kPowerNameMax = 256;
    inline s32           s_arPowerNamed[kPowerNameMax] = {};
    inline int           s_nPowerNamed = 0;

    inline void NamePowerOnce(s32 sno) {
        if (SNOToString == nullptr || s_nPowerNamed >= kPowerNameMax)
            return;
        for (int i = 0; i < s_nPowerNamed; ++i)
            if (s_arPowerNamed[i] == sno)
                return;
        s_arPowerNamed[s_nPowerNamed++] = sno;
        auto        tName = SNOToString(0x1F, sno, 0);
        const char *sz    = tName.str();
        PRINT("[d3hack-power] POWER %d = \"%s\"", sno, (sz != nullptr) ? sz : "?")
    }

    // d3hack-custom: see and bias the script RNG, per power.
    //
    // Why this exists: the Vision of Enmity proc chance is NOT a Script Formula. Power
    // Community_Buff_NestingPortalSpawn (sno 488544, global SNO 0x61B) evaluated zero formulas
    // across a whole session -- including one where a Vision actually spawned, so that is a
    // confirmed negative and not a missed sample. The roll therefore comes from the script
    // calling GetRandomFloat and comparing against a constant compiled into its bytecode.
    //
    // A constant in bytecode cannot be read. It does not need to be: bias the ROLL instead.
    // If the script does `if (rand < chance)`, multiplying rand by 0.25 makes it pass about
    // four times as often, whatever `chance` happens to be.
    //
    // HOOKED AT THE WRAPPER (0x8D5910), NOT THE RNG (0x938510). 0x938510 is the game's global
    // float RNG -- loot rolls, AI, damage, every frame. Hooking it would put a callback on one
    // of the hottest functions in the process for no benefit. The script binding above it only
    // runs during script execution, which is both rare and exactly the scope we want.
    //
    // The wrapper is REPLACED rather than wrapped, because the value has to be biased before it
    // is pushed into the script VM and Orig does the pushing. Its stock body is exactly:
    //
    //     bl 0x938510 ; fcvt d0, s0 ; bl 0xB1A8B0 ; mov w0, #1 ; ret
    //
    // which is what the callback reproduces, with the bias in the middle.
    //
    // SAFETY -- 0x9B7B80 (PowerGetCurrentSNO) MUST NOT be called blind. When no power is
    // executing, 0x89D860 returns -1, and 0x8A0260 then takes its not-found path at 0x8A02CC,
    // which sets x8 = xzr and falls straight into `ldr w9, [x8, #0x114]`. That is a guaranteed
    // null dereference in the game's own code -- it is only ever safe because the game never
    // calls it with -1. Neither does this: the handle is fetched first and checked.
    inline auto ScriptRandFloat() -> float {
        using Fn = float (*)();
        return reinterpret_cast<Fn>(GameOffset(0x938510))();
    }

    inline void ScriptPushDouble(void *pCtx, double d) {
        using Fn = void (*)(void *, double);
        reinterpret_cast<Fn>(GameOffset(0xB1A8B0))(pCtx, d);
    }

    // -1 when no power script is running. Guarded, see above.
    inline auto CurrentPowerSno() -> s32 {
        using FnH = s32 (*)();
        using FnA = void (*)(s32, u32 *, void *);
        const s32 hInst = reinterpret_cast<FnH>(GameOffset(0x89D860))();
        if (hInst == -1)
            return -1;
        u32 uAttribId = 0;
        u32 arScratch[4] {};
        reinterpret_cast<FnA>(GameOffset(0x8A0260))(hInst, &uAttribId, arScratch);
        return AttribIdToPowerSno(uAttribId);
    }

    inline constexpr int kRandSeenMax = 256;
    inline s32           s_arRandSeen[kRandSeenMax] = {};
    inline int           s_nRandSeen = 0;
    inline long          s_nRandRolls = 0;

    HOOK_DEFINE_TRAMPOLINE(ScriptRandomHook) {
        static auto Callback(void *pCtx) -> int {
            float fVal = ScriptRandFloat();

            const bool bProbe   = global_config.rare_cheats.power_random_probe;
            const int  nBiasSno = global_config.rare_cheats.power_random_bias_sno;
            const int  nBiasPct = global_config.rare_cheats.power_random_bias_pct;
            const bool bBias    = (nBiasSno != 0 && nBiasPct != 100);

            if (bProbe || bBias) {
                const s32 sno = CurrentPowerSno();
                if (sno > 0) {
                    if (bProbe) {
                        ++s_nRandRolls;
                        bool bNew = true;
                        for (int i = 0; i < s_nRandSeen; ++i)
                            if (s_arRandSeen[i] == sno)
                                bNew = false;
                        if (bNew && s_nRandSeen < kRandSeenMax) {
                            s_arRandSeen[s_nRandSeen++] = sno;
                            NamePowerOnce(sno);
                            PRINT("[d3hack-rng] power %d rolls the script RNG (roll %d.%03d)", sno,
                                  static_cast<int>(fVal),
                                  static_cast<int>((fVal < 0.0f ? -fVal : fVal) * 1000.0f) % 1000)
                        }
                        // The target power gets every roll counted, so the base rate can be
                        // worked out from rolls-per-spawn without knowing the threshold.
                        if (sno == nBiasSno && (s_nRandRolls % 50L) == 0L)
                            PRINT("[d3hack-rng] power %d: %ld script rolls so far", sno, s_nRandRolls)
                    }
                    if (bBias && sno == nBiasSno) {
                        fVal = fVal * (static_cast<float>(nBiasPct) / 100.0f);
                        static int s_nLogs = 0;
                        if (s_nLogs < 8) {
                            ++s_nLogs;
                            PRINT("[d3hack-rng] BIASED power %d roll -> %d.%03d (%d%%)", sno,
                                  static_cast<int>(fVal),
                                  static_cast<int>((fVal < 0.0f ? -fVal : fVal) * 1000.0f) % 1000,
                                  nBiasPct)
                        }
                    }
                }
            }

            ScriptPushDouble(pCtx, static_cast<double>(fVal));
            return 1;
        }
    };

    // d3hack-custom: the OTHER script RNG binding.
    //
    // A gap in the last probe, and a real one. GetRandomFloat and GetRandomInt are SEPARATE
    // script bindings -- 0x8D5910 and 0x8D5860 -- with separate inner natives (0x938510 and
    // 0x9384B0). Only the float one was hooked, so the conclusion "the Vision power never rolls
    // the script RNG" was only ever true of half the RNG surface. A script doing
    // `if (GetRandomInt(1,100) <= chance)` would have been invisible.
    //
    // Probe only, no bias: the int wrapper pops two arguments before rolling, so replacing it
    // means reproducing the argument fetch, and there is no reason to risk that before knowing
    // whether 488544 even shows up here. Orig is called normally and only the caller is logged.
    //
    // Fires on ordinary play -- the roll happens on every kill whether or not a portal results,
    // so nothing rare has to occur for this to pay out.
    inline constexpr int kRandIntSeenMax = 256;
    inline s32           s_arRandIntSeen[kRandIntSeenMax] = {};
    inline int           s_nRandIntSeen = 0;

    // Set only for the duration of the target power's GetRandomInt call, read by the hook on
    // the inner native. See the note on ScriptRandomIntHook below for why it is done this way.
    inline bool s_bBiasRandIntNow = false;
    inline int  s_nRandIntLogs    = 0;

    // d3hack-custom: bias the INT roll, at the inner native.
    //
    // 0x9384B0 is `int RandomInt(int lo, int hi)` -- the global integer RNG, called by
    // everything. It is hooked anyway because the script wrapper cannot bias its own result:
    // Orig() both rolls AND pushes the value into the script VM, so by the time the wrapper
    // sees a return there is nothing left to change. Replacing the wrapper instead would mean
    // reproducing its two-argument fetch (0xB19E80 validate, 0xB1A1B0 fetch, fcvtzs) and its
    // error paths -- more code, and more to get wrong, than a gated hook.
    //
    // So: the wrapper sets a flag around its Orig call, and this fires only when the flag is
    // set. Every other caller in the game pays one bool test.
    //
    // The bias compresses the roll toward the LOW end -- `lo + (r - lo) * pct / 100` -- which
    // mirrors what the float bias does (multiply by pct/100) and makes a `roll <= chance` test
    // pass roughly 100/pct times as often. If the Vision test turns out to be `roll >= high`
    // instead, this would make Visions RARER, not commoner; the lo/hi/roll values logged below
    // are what settles that, which is why they are logged before anything is tuned.
    // d3hack-custom: what does a Vision of Enmity roll when it builds a level?
    //
    // Established from the game's own documented behaviour, not from guesswork: every Vision
    // level has ONE randomly chosen monster type for the whole floor, and one of those types
    // is "all Treasure Goblins". So a goblin floor is not a separate world SNO -- it is the
    // outcome of a pick-one-of-N roll made while the level is generated. Bias that roll and
    // goblin floors get common, which is the whole feature request.
    //
    // WHICH roll is the open question, and the script-RNG census below cannot answer it: if
    // the pick is native worldgen it never touches the script VM at all. So record from the
    // other end -- bracket sInitializeWorld and log every distinct SMALL-SPAN RandomInt made
    // inside it. A pick-one-of-N has a narrow span; loot seeds and layout jitter are wide and
    // filter out. 0x9384B0 is the global integer RNG that everything funnels through, so this
    // sees native and script rolls alike.
    //
    // Fires on every world entry, vision or not. Nothing rare has to happen for it to pay out,
    // and diffing an ordinary floor against a goblin floor IS the method.
#include "d3/world_names.inl"   // d3hack-custom: generated World SNO -> name table

    // d3hack-custom: name a world SNO. Binary search over the generated table above.
    inline auto WorldName(s32 sno) -> const char * {
        int lo = 0;
        int hi = static_cast<int>(sizeof(kWorldNames) / sizeof(kWorldNames[0])) - 1;
        while (lo <= hi) {
            const int mid = lo + ((hi - lo) / 2);
            if (kWorldNames[mid].sno == sno)
                return kWorldNames[mid].szName;
            if (kWorldNames[mid].sno < sno)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
        return "?";
    }

    // d3hack-custom: what got rolled just before this world was built?
    //
    // Two features need the same answer. A Vision of Enmity picks ONE monster type for the
    // whole floor -- one of those types is "all Treasure Goblins" -- and a Greater Rift picks
    // a tileset world per floor. Both are pick-one-of-N rolls, and neither list exists as a
    // table in the executable: searching .text, .rodata and .data for the 164 known rift
    // tileset world SNOs found no cluster at all, so both lists are asset data.
    //
    // The pick therefore has to be caught in the act. Crucially it happens BEFORE
    // sInitializeWorld -- that function is handed a world SNO that was already chosen -- so
    // bracketing world init would miss it. Instead keep a rolling ring of every NARROW
    // RandomInt (span <= 32; loot seeds and layout jitter are wide and filter out) and dump
    // the ring when a world initialises. Whatever chose that world is in the tail of it.
    //
    // Each entry carries the CALL SITE, recovered from the return address minus the module
    // base. That is the whole point: once one site is shown to decide the tileset, biasing or
    // re-rolling it is a two-line change, and the roll -> world mapping can be read straight
    // off successive dumps.
    //
    // 0x9384B0 is the global integer RNG everything funnels through, so this sees native
    // worldgen and script alike. Fires on every world entry -- nothing rare has to happen.
    // d3hack-custom: SEPARATE budgets for the two roll sources. Fourth time.
    //
    // Run 10: 1235 narrow rolls between two rift floors, 256 distinct rows, 661 dropped -- and
    // **0 native**. The native hook at 0x11E660 demonstrably runs (the census caught 362 calls
    // through 0x11E64C during floor-1 generation), so the FILTER was wrong, twice over:
    //
    //   1. Native rolls were held to the same span <= 32 as script rolls, and nothing passed.
    //      That is itself information: every native span in this game is wider than 32, so a
    //      tileset pick has a bigger range than I assumed.
    //   2. Script rolls filled all 256 rows and dropped 661 more, so even a qualifying native
    //      row could have been crowded out.
    //
    // Fixed structurally: two tables with their own budgets, and a native span limit wide
    // enough to see the distribution. Native is keyed on (lo, hi) ONLY, keeping the last result
    // and a change count -- which is exactly the question being asked, "does this range recur
    // while its result varies per floor?"
    inline constexpr int  kScriptRollMax   = 48;
    inline constexpr int  kNativeRollMax   = 192;
    inline constexpr long kWorldGenSpanMax = 32;      // script: shuffles, tiny by nature
    inline constexpr long kNativeSpanMax   = 100000;  // native: unknown, so cast wide

    struct WorldGenRoll {
        u32 uSite;
        int nLo;
        int nHi;
        int nRes;
        u32 uCount;
    };

    struct NativeRoll {
        int nLo;
        int nHi;
        int nLast;
        u32 uCount;
        u32 uChanges;
    };

    inline WorldGenRoll s_arWorldGenRoll[kScriptRollMax] = {};
    inline int          s_nWorldGenRoll                  = 0;
    inline NativeRoll   s_arNativeRoll[kNativeRollMax]   = {};
    inline int          s_nNativeRoll                    = 0;
    inline int          s_nWorldGenTotal                 = 0;
    inline int          s_nWorldGenDropped               = 0;
    inline int          s_nWorldGenNative                = 0;
    inline int          s_nNativeDropped                 = 0;

    inline void NativeNote(int nLo, int nHi, int nRes) {
        ++s_nWorldGenNative;
        for (int k = 0; k < s_nNativeRoll; ++k) {
            NativeRoll &tRow = s_arNativeRoll[k];
            if (tRow.nLo == nLo && tRow.nHi == nHi) {
                ++tRow.uCount;
                if (tRow.nLast != nRes) {
                    ++tRow.uChanges;
                    tRow.nLast = nRes;
                }
                return;
            }
        }
        if (s_nNativeRoll >= kNativeRollMax) {
            ++s_nNativeDropped;
            return;
        }
        NativeRoll &tRow = s_arNativeRoll[s_nNativeRoll++];
        tRow.nLo      = nLo;
        tRow.nHi      = nHi;
        tRow.nLast    = nRes;
        tRow.uCount   = 1;
        tRow.uChanges = 0;
    }

    inline void WorldGenNote(int nLo, int nHi, int nRes, uintptr_t uRet) {
        const uintptr_t uBase = GameOffset(0);
        const u32       uSite = (uRet > uBase) ? static_cast<u32>(uRet - uBase) : 0u;
        const long long nSpan = static_cast<long long>(nHi) - static_cast<long long>(nLo);
        if (nSpan <= 0)
            return;
        if (uSite == 0x11E600u) {
            if (nSpan <= kNativeSpanMax)
                NativeNote(nLo, nHi, nRes);
            return;
        }
        if (nSpan > kWorldGenSpanMax)
            return;
        ++s_nWorldGenTotal;
        for (int k = 0; k < s_nWorldGenRoll; ++k) {
            WorldGenRoll &tRow = s_arWorldGenRoll[k];
            if (tRow.uSite == uSite && tRow.nLo == nLo && tRow.nHi == nHi && tRow.nRes == nRes) {
                ++tRow.uCount;
                return;
            }
        }
        if (s_nWorldGenRoll >= kScriptRollMax) {
            ++s_nWorldGenDropped;
            return;
        }
        WorldGenRoll &tRow = s_arWorldGenRoll[s_nWorldGenRoll++];
        tRow.uSite  = uSite;
        tRow.nLo    = nLo;
        tRow.nHi    = nHi;
        tRow.nRes   = nRes;
        tRow.uCount = 1;
    }

#include "d3/rift_maps.inl"   // d3hack-custom: generated banneable rift map list
#include "d3/rift_snos.inl"   // d3hack-custom: generated rift LevelArea/World SNO table
#include "d3/weather_names.inl"   // d3hack-custom: generated Weather SNO -> name

    // d3hack-custom: name ANY SNO. The group argument is a lie.
    //
    // The group sweep returned the CORRECT name for every g from 0 to 71, on all three probes.
    // 0x74A510 shows why: it loads the table with `ldr x8, [x8, #0x11e0]` and indexes it
    // `ldr x8, [x8, w1, uxtw #3]` -- by the SNO HANDLE, w1. w0 is never a table index.
    // So handles are globally unique and any value can be named without knowing its group.
    inline auto SnoName(s32 sno) -> const char * {
        if (SNOToString == nullptr)
            return nullptr;
        if (sno <= 0 || sno > 0x00FFFFFF)
            return nullptr;
        auto        tName = SNOToString(0, sno, 0);
        const char *sz    = tName.str();
        if (sz == nullptr || sz[0] == '\0')
            return nullptr;
        // SNOToString does NOT return null for a non-asset -- it hands back "[sno unknown]" or
        // a hex blob, so without this small integers like 15, 16 and 128 all "resolve" and a
        // dump fills with noise. Run 7 produced a 47-entry record holding one real asset.
        if (sz[0] == '[')
            return nullptr;
        if (sz[0] == '0' && sz[1] == 'x')
            return nullptr;
        return sz;
    }

    // d3hack-custom: rift map bans -- the name/SNO half.
    //
    // The user's requirement: "if someone wants big graveyard and not small, they can just ban
    // the small one". So the unit is ONE SNO, not a family. Graveyard has five entries and each
    // bans independently. Parsing mirrors DisableMonsterAffixes: comma separated, trimmed,
    // case-insensitive, and an unresolvable entry is REPORTED so a typo is visible.
    // 256, not 64. At 64 a 144-entry ban list was SILENTLY TRUNCATED: the tail read as
    // "not banned", so banned maps both slipped through and got chosen as replacements --
    // corruptspire was swapped for keep_war_large, which is itself on the list. There are only
    // 164 rift maps in total, so 256 cannot be hit; the overflow path below now says so out
    // loud rather than dropping entries quietly.
    inline constexpr int kBannedMapMax = 256;

    inline s32  s_arBannedMap[kBannedMapMax] = {};
    inline int  s_nBannedMap                 = 0;
    inline bool s_bBansResolved              = false;

    // d3hack-custom: the WHITELIST. If non-empty, anything not named here is disallowed.
    //
    // "if someone only wanted boneyards, that should just work -- who cares if it's only one
    // map." That is the right design call, and it is also why a whitelist exists rather than
    // making people ban 25 of 26 maps to keep one. A single entry is a valid configuration and
    // the code must not treat a small selectable set as an error.
    inline s32 s_arAllowMap[kBannedMapMax] = {};
    inline int s_nAllowMap                 = 0;

    // d3hack-custom: maps the user LIKES. Replacements are drawn from here first.
    inline s32 s_arPreferMap[kBannedMapMax] = {};
    inline int s_nPreferMap                 = 0;

    inline auto RiftMapSnoByName(const char *pName, size_t nLen) -> s32 {
        for (const auto &tRow : kRiftMaps) {
            size_t k = 0;
            while (k < nLen && tRow.szName[k] != '\0') {
                char a = tRow.szName[k];
                char b = pName[k];
                if (a >= 'A' && a <= 'Z')
                    a = static_cast<char>(a + 32);
                if (b >= 'A' && b <= 'Z')
                    b = static_cast<char>(b + 32);
                if (a != b)
                    break;
                ++k;
            }
            if (k == nLen && tRow.szName[k] == '\0')
                return tRow.sno;
        }
        return 0;
    }

    // d3hack-custom: SNO -> internal rift map name, from the generated kRiftMaps table.
    // Doubles as the validity test used when substituting a banned map: a value this does not
    // recognise is not a rift tileset and must never be written back.
    inline auto RiftMapName(s32 sno) -> const char * {
        for (const auto &tRow : kRiftMaps)
            if (tRow.sno == sno)
                return tRow.szName;
        return nullptr;
    }

    inline void ResolveRiftBans() {
        if (s_bBansResolved)
            return;
        s_bBansResolved = true;
        const std::string &sList = global_config.rare_cheats.banned_rift_maps;
        size_t             i     = 0;
        while (i < sList.size()) {
            size_t j = sList.find(',', i);
            if (j == std::string::npos)
                j = sList.size();
            size_t a = i;
            size_t b = j;
            while (a < b && (sList[a] == ' ' || sList[a] == '\t'))
                ++a;
            while (b > a && (sList[b - 1] == ' ' || sList[b - 1] == '\t'))
                --b;
            i = j + 1;
            if (a >= b)
                continue;
            const s32 sno = RiftMapSnoByName(sList.c_str() + a, b - a);
            if (sno == 0) {
                PRINT("[d3hack-rift] BAN LIST: \"%.*s\" is not a known rift map -- see "
                      "rift-maps.txt",
                      static_cast<int>(b - a), sList.c_str() + a)
                continue;
            }
            if (s_nBannedMap < kBannedMapMax) {
                s_arBannedMap[s_nBannedMap++] = sno;
            } else {
                PRINT("[d3hack-rift] BAN LIST FULL at %d -- \"%.*s\" and everything after it "
                      "was DROPPED. Raise kBannedMapMax.",
                      kBannedMapMax, static_cast<int>(b - a), sList.c_str() + a)
            }
        }
        if (s_nBannedMap > 0)
            PRINT("[d3hack-rift] %d map(s) banned", s_nBannedMap)

        // Same parse for the whitelist. An unresolvable name is REPORTED, never silently
        // dropped -- with a whitelist a typo is far more dangerous than with a ban list,
        // because one bad name can leave nothing selectable at all.
        const std::string &sAllow = global_config.rare_cheats.allowed_rift_maps;
        size_t             p      = 0;
        while (p < sAllow.size()) {
            size_t q = sAllow.find(',', p);
            if (q == std::string::npos)
                q = sAllow.size();
            size_t a = p;
            size_t b = q;
            while (a < b && (sAllow[a] == ' ' || sAllow[a] == '	'))
                ++a;
            while (b > a && (sAllow[b - 1] == ' ' || sAllow[b - 1] == '	'))
                --b;
            p = q + 1;
            if (a >= b)
                continue;
            const s32 sno = RiftMapSnoByName(sAllow.c_str() + a, b - a);
            if (sno == 0) {
                PRINT("[d3hack-rift] ALLOW LIST: \"%.*s\" is not a known rift map -- see "
                      "rift-maps.txt",
                      static_cast<int>(b - a), sAllow.c_str() + a)
                continue;
            }
            if (s_nAllowMap < kBannedMapMax)
                s_arAllowMap[s_nAllowMap++] = sno;
        }
        if (s_nAllowMap > 0)
            PRINT("[d3hack-rift] whitelist active: %d map(s) allowed, everything else "
                  "substituted", s_nAllowMap)

        // Same parse again for the preference list.
        const std::string &sPref = global_config.rare_cheats.preferred_rift_maps;
        size_t             u     = 0;
        while (u < sPref.size()) {
            size_t v = sPref.find(',', u);
            if (v == std::string::npos)
                v = sPref.size();
            size_t a = u;
            size_t b = v;
            while (a < b && (sPref[a] == ' ' || sPref[a] == '	'))
                ++a;
            while (b > a && (sPref[b - 1] == ' ' || sPref[b - 1] == '	'))
                --b;
            u = v + 1;
            if (a >= b)
                continue;
            const s32 sno = RiftMapSnoByName(sPref.c_str() + a, b - a);
            if (sno == 0) {
                PRINT("[d3hack-rift] PREFER LIST: \"%.*s\" is not a known rift map",
                      static_cast<int>(b - a), sPref.c_str() + a)
                continue;
            }
            if (s_nPreferMap < kBannedMapMax)
                s_arPreferMap[s_nPreferMap++] = sno;
        }
        if (s_nPreferMap > 0)
            PRINT("[d3hack-rift] %d preferred map(s) -- replacements drawn from these first",
                  s_nPreferMap)
    }

    // d3hack-custom: per-map monster density.
    //
    // A single global multiplier cannot suit every tileset. The user's report: Battlefields of
    // Eternity "is very empty" at the shipped x3 -- and it would be, because it is by far the
    // most open map in the pool, so the same number of spawn groups is spread over several
    // times the ground a corridor map covers. Density is groups-per-level, not groups-per-area.
    //
    // Parsed like the other list settings: "name=multiplier", comma separated, names as they
    // appear in rift-maps.txt. An unparseable entry is reported rather than ignored.
    inline constexpr int kMapDensityMax = 32;

    struct MapDensity {
        s32 sno;
        int nMul;
    };

    inline MapDensity s_arMapDensity[kMapDensityMax] = {};
    inline int        s_nMapDensity                  = 0;
    inline s32        s_snoAssignedMap               = 0;   // set at 0x4BC450, before generation

    inline void ResolveMapDensity() {
        static bool s_done = false;
        if (s_done)
            return;
        s_done = true;
        const std::string &sList = global_config.rare_cheats.map_density_overrides;
        size_t             i     = 0;
        while (i < sList.size()) {
            size_t j = sList.find(',', i);
            if (j == std::string::npos)
                j = sList.size();
            size_t a = i;
            size_t b = j;
            while (a < b && (sList[a] == ' ' || sList[a] == '\t'))
                ++a;
            while (b > a && (sList[b - 1] == ' ' || sList[b - 1] == '\t'))
                --b;
            i = j + 1;
            if (a >= b)
                continue;
            size_t eq = sList.find('=', a);
            if (eq == std::string::npos || eq >= b) {
                PRINT("[d3hack-density] \"%.*s\" has no =multiplier -- ignored",
                      static_cast<int>(b - a), sList.c_str() + a)
                continue;
            }
            size_t ne = eq;
            while (ne > a && (sList[ne - 1] == ' ' || sList[ne - 1] == '\t'))
                --ne;
            const s32 sno = RiftMapSnoByName(sList.c_str() + a, ne - a);
            if (sno == 0) {
                PRINT("[d3hack-density] \"%.*s\" is not a known rift map -- see rift-maps.txt",
                      static_cast<int>(ne - a), sList.c_str() + a)
                continue;
            }
            int nMul = 0;
            for (size_t k = eq + 1; k < b; ++k)
                if (sList[k] >= '0' && sList[k] <= '9')
                    nMul = (nMul * 10) + (sList[k] - '0');
            if (nMul < 1 || nMul > 64) {
                PRINT("[d3hack-density] \"%.*s\" multiplier %d out of range 1..64 -- ignored",
                      static_cast<int>(ne - a), sList.c_str() + a, nMul)
                continue;
            }
            if (s_nMapDensity < kMapDensityMax) {
                s_arMapDensity[s_nMapDensity].sno  = sno;
                s_arMapDensity[s_nMapDensity].nMul = nMul;
                ++s_nMapDensity;
                PRINT("[d3hack-density] %.*s -> x%d", static_cast<int>(ne - a),
                      sList.c_str() + a, nMul)
            }
        }
    }

    inline auto MapDensityFor(s32 sno) -> int {
        for (int k = 0; k < s_nMapDensity; ++k)
            if (s_arMapDensity[k].sno == sno)
                return s_arMapDensity[k].nMul;
        return 0;   // no override
    }

    // d3hack-custom: the whitelist wins when present, otherwise fall back to the ban list.
    inline auto RiftMapIsPreferred(s32 sno) -> bool {
        for (int k = 0; k < s_nPreferMap; ++k)
            if (s_arPreferMap[k] == sno)
                return true;
        return false;
    }

    inline auto RiftMapIsAllowed(s32 sno) -> bool {
        if (s_nAllowMap > 0) {
            for (int k = 0; k < s_nAllowMap; ++k)
                if (s_arAllowMap[k] == sno)
                    return true;
            return false;
        }
        return true;   // no whitelist -- RiftMapIsBanned decides
    }

    // d3hack-custom: per-map floats, LEARNED, never invented.
    //
    // Each plan entry carries three floats beside the tileset, and 0x4C1380 pulls one of them
    // inside a worldgen loop that accumulates a per-tile count and then iterates count-1
    // times. That is budget-shaped, and a budget is exactly what "you move a bit and then it
    // stops" looks like when it is wrong.
    //
    // This is the field the old "bundle" theory was reaching for. It is NOT the LevelArea --
    // that reads -1 on every GR floor and is unused. Every previous swap replaced the tileset
    // and left these three floats describing the ORIGINAL map, which fits all four failures
    // AND the two successes, where a size rule fits neither.
    //
    // Values are recorded from plans the game itself built, in the same spirit as the seen-map
    // table: a substitution may only use numbers the game has actually produced.
    struct MapFloats {
        s32   sno;
        float ar[3];
    };

    inline MapFloats s_arMapFloats[192] = {};   // 164 rift maps exist; never silently cap
    inline int       s_nMapFloats      = 0;

    // The filename changed from rift-map-floats.txt deliberately. Every file written before
    // 2026-08-26 mixes Greater Rift floats with Nephalem rift floats, because the learn pass
    // below had no rift-type gate. Measured on a 60-map cache: the 42 maps that had also been
    // SEEN in a GR floor all carry GR-shaped values (f0 0.45..1.18 varied, f2 in {1,2,3}); the
    // other 18 all carry f0==f1==f2 in {2,3,4,6,8,10} and NOT ONE of them has ever appeared in
    // a GR. Perfect separation across 60 samples. Those 18 are Nephalem budgets, and feeding
    // one to a GR floor is exactly the "swapped the tileset, kept the wrong budget" mistake
    // that produced four unleavable floors. Renaming is the migration: the old file is simply
    // never read again.
    inline constexpr const char *kMapFloatPath = "sd:/config/d3hack-nx/rift-map-gr-floats.txt";
    inline bool                  s_bMapFloatsDirty  = false;
    inline bool                  s_bMapFloatsLoaded = false;

    inline void NoteMapFloats(s32 sno, const float *pf) {
        if (sno <= 0)
            return;
        for (int k = 0; k < s_nMapFloats; ++k)
            if (s_arMapFloats[k].sno == sno)
                return;
        if (s_nMapFloats >= static_cast<int>(sizeof(s_arMapFloats) / sizeof(s_arMapFloats[0]))) {
            static bool s_bWarned = false;
            if (!s_bWarned) {
                s_bWarned = true;
                PRINT("[d3hack-plan] float cache FULL at %d maps -- further maps are dropped. "
                      "Raise s_arMapFloats.", s_nMapFloats)
            }
            return;
        }
        s_arMapFloats[s_nMapFloats].sno = sno;
        for (int i = 0; i < 3; ++i)
            s_arMapFloats[s_nMapFloats].ar[i] = pf[i];
        ++s_nMapFloats;
        s_bMapFloatsDirty = true;
    }

    // Forward declarations: the load/save pair is defined below, next to the file format.
    inline void MapFloatsLoad();
    inline void MapFloatsSave();

    inline auto MapFloatsFor(s32 sno) -> const float * {
        for (int k = 0; k < s_nMapFloats; ++k)
            if (s_arMapFloats[k].sno == sno)
                return s_arMapFloats[k].ar;
        return nullptr;
    }

    // d3hack-custom: persist the learned floats.
    //
    // Without this the table only holds maps that turned up in THIS session's plans, so
    // "I only want boneyards" would sit refusing every swap until boneyards happened to be
    // rolled -- which is not what "that should just work" means. Saved to disk, one sighting
    // ever is enough and the config works from the next boot onwards.
    //
    // Stored as raw IEEE bit patterns in decimal, not as printed decimals: 1.07f does not
    // survive a text round trip exactly, and a float that is nearly right is precisely the
    // failure being guarded against.
    //
    //     <sno> <bits0> <bits1> <bits2>    one map per line
    inline auto F2U(float f) -> u32 {
        u32 u = 0;
        __builtin_memcpy(&u, &f, 4);
        return u;
    }

    inline auto U2F(u32 u) -> float {
        float f = 0.0f;
        __builtin_memcpy(&f, &u, 4);
        return f;
    }

    inline void MapFloatsLoad() {
        if (s_bMapFloatsLoaded)
            return;
        s_bMapFloatsLoaded = true;

        nn::fs::FileHandle fh {};
        if (!R_SUCCEEDED(nn::fs::OpenFile(&fh, kMapFloatPath, nn::fs::OpenMode_Read)))
            return;   // no file yet is the normal first-run case, not an error

        s64 size = 0;
        if (R_SUCCEEDED(nn::fs::GetFileSize(&size, fh)) && size > 0 && size < 32768) {
            char buf[32768] {};
            if (R_SUCCEEDED(nn::fs::ReadFile(fh, 0, buf, static_cast<u64>(size)))) {
                const int n = static_cast<int>(size);
                int       i = 0;
                while (i < n) {
                    // '#' comments, so the shipped seed file can say what it is. The scanner
                    // below hunts digit runs and would happily read a year out of a comment,
                    // which is why the file could not be annotated before.
                    while (i < n) {
                        while (i < n && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' ||
                                         buf[i] == '\n'))
                            ++i;
                        if (i < n && buf[i] == '#') {
                            while (i < n && buf[i] != '\n')
                                ++i;
                            continue;
                        }
                        break;
                    }
                    if (i >= n)
                        break;
                    u32 arVal[4] = {0, 0, 0, 0};
                    int nGot     = 0;
                    for (; nGot < 4 && i < n; ++nGot) {
                        // Stop at a newline OR a '#': a trailing comment must never be
                        // mined for digits, and the map names in one are full of them.
                        while (i < n && buf[i] != '\n' && buf[i] != '#' &&
                               (buf[i] < '0' || buf[i] > '9'))
                            ++i;
                        if (i >= n || buf[i] == '\n' || buf[i] == '#')
                            break;
                        u32 v = 0;
                        while (i < n && buf[i] >= '0' && buf[i] <= '9') {
                            v = (v * 10u) + static_cast<u32>(buf[i] - '0');
                            ++i;
                        }
                        arVal[nGot] = v;
                    }
                    if (nGot < 4) {
                        // A short line is a damaged record, not the end of the file. Skip it
                        // and keep going -- bailing out here would silently drop every map
                        // after the first bad line.
                        if (i >= n)
                            break;
                        while (i < n && buf[i] != '\n')
                            ++i;
                        continue;
                    }
                    const float arF[3] = {U2F(arVal[1]), U2F(arVal[2]), U2F(arVal[3])};
                    NoteMapFloats(static_cast<s32>(arVal[0]), arF);
                }
            }
        }
        nn::fs::CloseFile(fh);
        if (s_nMapFloats > 0)
            PRINT("[d3hack-plan] loaded floats for %d map(s) from disk", s_nMapFloats)
    }

    // Called from sInitializeWorld -- the main thread, and a loading point where a brief
    // pause costs nothing. Never call these from a world-gen or message thread.
    inline void MapFloatsLoadIfNeeded() {
        MapFloatsLoad();
    }

    inline void MapFloatsFlush();

    inline void MapFloatsSave() {
        if (!s_bMapFloatsDirty || s_nMapFloats <= 0)
            return;
        char buf[32768];
        int  len = 0;
        for (int k = 0; k < s_nMapFloats && len < static_cast<int>(sizeof(buf)) - 64; ++k) {
            len += ::snprintf(buf + len, sizeof(buf) - static_cast<size_t>(len),
                              "%d %u %u %u\n", s_arMapFloats[k].sno,
                              F2U(s_arMapFloats[k].ar[0]), F2U(s_arMapFloats[k].ar[1]),
                              F2U(s_arMapFloats[k].ar[2]));
        }
        std::string err;
        if (fs_util::WriteAllAtomic(kMapFloatPath,
                                    std::string_view(buf, static_cast<size_t>(len)),
                                    "rift map floats", err)) {
            s_bMapFloatsDirty = false;
        } else {
            static int s_nFailed = 0;
            if (s_nFailed < 3) {
                ++s_nFailed;
                PRINT("[d3hack-plan] could not write %s (%s) -- floats stay in memory for this "
                      "session", kMapFloatPath, err.c_str())
            }
        }
    }

    inline auto RiftMapIsBanned(s32 sno) -> bool {
        for (int k = 0; k < s_nBannedMap; ++k)
            if (s_arBannedMap[k] == sno)
                return true;
        return false;
    }

    inline auto WeatherName(s32 sno) -> const char * {
        int lo = 0;
        int hi = static_cast<int>(sizeof(kWeatherNames) / sizeof(kWeatherNames[0])) - 1;
        while (lo <= hi) {
            const int mid = lo + ((hi - lo) / 2);
            if (kWeatherNames[mid].sno == sno)
                return kWeatherNames[mid].szName;
            if (kWeatherNames[mid].sno < sno)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
        return nullptr;
    }

    // d3hack-custom: is this weather a low-fog variant?
    //
    // The user made the connection, and it is the right one: the long-standing seam at high
    // ViewDolly is FOG, and fog is a weather property. Weather assets are named
    // <tileset>_<colour>_<HiFog|LowFog>_<Bright|Dark>, and the stock candidate lists run 223
    // HiFog to 60 LowFog -- so the fogged variant is what you usually get, which is exactly
    // why the seam kept showing up. Substituting within the SAME candidate list keeps the map
    // and its colour family and only drops the fog.
    //
    // Substring search rather than a suffix test: several names carry an extra tag
    // (X1_LR_Boneyards_Black_Rain_HiFog_Bright), so the fog marker is not always in a fixed
    // position.
    inline auto NameHasLowFog(const char *sz) -> bool {
        if (sz == nullptr)
            return false;
        for (int i = 0; sz[i] != '\0'; ++i)
            if (sz[i] == 'L' && sz[i + 1] == 'o' && sz[i + 2] == 'w' && sz[i + 3] == 'F' &&
                sz[i + 4] == 'o' && sz[i + 5] == 'g')
                return true;
        return false;
    }

    inline auto RiftSnoName(s32 sno, char *pchKind) -> const char * {
        int lo = 0;
        int hi = static_cast<int>(sizeof(kRiftSnos) / sizeof(kRiftSnos[0])) - 1;
        while (lo <= hi) {
            const int mid = lo + ((hi - lo) / 2);
            if (kRiftSnos[mid].sno == sno) {
                *pchKind = kRiftSnos[mid].chKind;
                return kRiftSnos[mid].szName;
            }
            if (kRiftSnos[mid].sno < sno)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
        return nullptr;
    }

    // d3hack-custom: the whole world struct, once, non-zero words only.
    //
    // sizeof(SWorld) measured 872 and the world SNO sits correctly at +0008, so the pointer is
    // the world struct and the scan covers all of it. But every field read through the typed
    // header came back IDENTICAL on town and on GR floors -- seed=1, script=0, flags=0,
    // stage=16 -- and a GR floor cannot share a seed with town. The header's offsets are wrong,
    // the same way LootSpecifier's were. Read the bytes instead of trusting names.
    inline void WorldRawDump(s32 snoWorld, const void *pWorld) {
        static int s_nDone = 0;
        if (!global_config.rare_cheats.world_gen_probe || pWorld == nullptr)
            return;
        if (snoWorld != 288454 || s_nDone >= 3)
            return;
        ++s_nDone;
        const auto *pWords = reinterpret_cast<const s32 *>(pWorld);
        PRINT("[d3hack-raw] --- SWorld dump %d, %u bytes, non-zero words ---", s_nDone,
              static_cast<u32>(sizeof(SWorld)))
        for (int k = 0; k < static_cast<int>(sizeof(SWorld) / 4); ++k)
            if (pWords[k] != 0)
                PRINT("[d3hack-raw]   +%04X = %11d  0x%08X", k * 4, pWords[k],
                      static_cast<u32>(pWords[k]))
    }

    // d3hack-custom: scan the world struct for rift SNOs. INLINE ONLY.
    //
    // Run 3 crashed the game by following every field that looked like a heap pointer, guarded
    // by `(cand >> 32) == (world >> 32)` -- a 4 GB window, so garbage starting with 0x10 got
    // dereferenced and it died on 0x1000000000. The rule that came out of it:
    // **never walk a pointer found by pattern-matching. Either the field is typed, or it is
    // not read.** This scans sizeof(SWorld) and dereferences nothing.
    inline constexpr int kRiftHitMax = 32;

    inline void RiftStructScan(s32 snoWorld, const void *pWorld) {
        ResolveRiftBans();
        if (!global_config.rare_cheats.world_gen_probe || pWorld == nullptr)
            return;
        if (reinterpret_cast<uintptr_t>(pWorld) < 0x1000ull)
            return;
        WorldRawDump(snoWorld, pWorld);

        const auto *pWords = reinterpret_cast<const s32 *>(pWorld);
        const int   nWords = static_cast<int>(sizeof(SWorld) / 4);
        int         nHits  = 0;
        for (int k = 0; k < nWords && nHits < kRiftHitMax; ++k) {
            char        chKind = '?';
            const char *szName = RiftSnoName(pWords[k], &chKind);
            if (szName == nullptr)
                continue;
            ++nHits;
            PRINT("[d3hack-rift]   +%04X  %c %d  %s%s", k * 4, chKind, pWords[k], szName,
                  RiftMapIsBanned(pWords[k]) ? "   <-- BANNED" : "")
        }
        PRINT("[d3hack-rift]   world %d \"%s\": %d rift SNO(s) in %u bytes",
              static_cast<int>(snoWorld), WorldName(snoWorld), nHits,
              static_cast<u32>(sizeof(SWorld)))
    }

    // d3hack-custom: how many random number generators does this game have?
    //
    // 0xA48200 is a plain LCG (state = state*0x6AC690C5 + carry) taking its state in x0, with
    // 632 call sites. Distinct state pointer = distinct generator. Raised from 16 because run 5
    // hit that cap exactly and so could not be trusted to have seen the one that matters.
    inline constexpr int kRngStateMax  = 48;
    inline constexpr int kRngCallerMax = 6;

    struct RngState {
        uintptr_t uState;
        u32       arCaller[kRngCallerMax];
        int       nCallers;
        u32       uCalls;
        u32       uAtGenStart;
    };

    inline RngState s_arRngState[kRngStateMax] = {};
    inline int      s_nRngState                = 0;
    inline int      s_nRngLast                 = 0;

    inline void RngNote(uintptr_t uState, uintptr_t uRet) {
        const uintptr_t uBase   = GameOffset(0);
        const u32       uCaller = (uRet > uBase) ? static_cast<u32>(uRet - uBase) : 0u;
        RngState       *pRow    = nullptr;
        if (s_nRngState > 0 && s_arRngState[s_nRngLast].uState == uState) {
            pRow = &s_arRngState[s_nRngLast];
        } else {
            for (int k = 0; k < s_nRngState; ++k)
                if (s_arRngState[k].uState == uState) {
                    pRow       = &s_arRngState[k];
                    s_nRngLast = k;
                    break;
                }
        }
        if (pRow == nullptr) {
            if (s_nRngState >= kRngStateMax)
                return;
            pRow       = &s_arRngState[s_nRngState];
            s_nRngLast = s_nRngState;
            ++s_nRngState;
            pRow->uState   = uState;
            pRow->nCallers = 0;
            pRow->uCalls   = 0;
        }
        ++pRow->uCalls;
        // Several distinct callers, not just the first: run 5 recorded only the first and it
        // was 0x0F9D3C on nearly every row -- a shared wrapper, which discriminates nothing.
        if (pRow->nCallers < kRngCallerMax) {
            for (int k = 0; k < pRow->nCallers; ++k)
                if (pRow->arCaller[k] == uCaller)
                    return;
            pRow->arCaller[pRow->nCallers++] = uCaller;
        }
    }

    inline void RngGenSnapshot() {
        for (int k = 0; k < s_nRngState; ++k)
            s_arRngState[k].uAtGenStart = s_arRngState[k].uCalls;
    }

    inline void RngGenReport(s32 snoWorld) {
        if (!global_config.rare_cheats.world_gen_probe)
            return;
        PRINT("[d3hack-rng-census] generating world %d \"%s\" -- calls DURING generation:",
              static_cast<int>(snoWorld), WorldName(snoWorld))
        for (int k = 0; k < s_nRngState; ++k) {
            const u32 uDelta = s_arRngState[k].uCalls - s_arRngState[k].uAtGenStart;
            if (uDelta == 0)
                continue;
            PRINT("[d3hack-rng-census]   state %p  %u calls  callers: %06X %06X %06X %06X",
                  reinterpret_cast<void *>(s_arRngState[k].uState), uDelta,
                  s_arRngState[k].nCallers > 0 ? s_arRngState[k].arCaller[0] : 0u,
                  s_arRngState[k].nCallers > 1 ? s_arRngState[k].arCaller[1] : 0u,
                  s_arRngState[k].nCallers > 2 ? s_arRngState[k].arCaller[2] : 0u,
                  s_arRngState[k].nCallers > 3 ? s_arRngState[k].arCaller[3] : 0u)
        }
        PRINT("[d3hack-rng-census]   %d generator(s) known (cap %d)", s_nRngState, kRngStateMax)
    }

    // d3hack-custom: find the ONE-OFF decision. Frequency is the filter now.
    //
    // 0x11E600 is a dead end, and the proof-of-life print is why: the hook fired **once**, with
    // lo=0 span=0. So that helper is barely used and is certainly not the tileset picker. The
    // 378 calls the census attributed to its generator came from the other callers in the set:
    // 0x3165D0 and 0x101360 (both `ucvtf` -> random float) and 0x0BBBAC (modulo by 0x7FFFFFFE).
    //
    // Which means this game mostly rolls FLOATS and scales them at each call site. There is no
    // single bounded-integer primitive to hook, so no probe on a shared helper can ever see a
    // "pick one of N" as a range -- which is why four rounds of range-hunting found nothing.
    //
    // Different discriminator: RARITY. Between two rift floors the RNG core is called thousands
    // of times, but choosing a tileset happens ONCE. So census the CALLERS of 0xA48200 and
    // report only those called a handful of times in the whole window. Layout, placement and
    // particle noise all call in the hundreds or thousands and filter themselves out; a
    // once-per-floor decision cannot hide in that.
    //
    // This is the same lesson as the shard probes, applied the other way round: spend the
    // budget on the rare case, and let the common case be what gets discarded.
    inline constexpr int kRareCallerMax = 512;
    inline constexpr u32 kRareCallerCap = 8;   // report callers used at most this many times

    struct RareCaller {
        u32 uSite;
        u32 uCount;
    };

    inline RareCaller s_arRareCaller[kRareCallerMax] = {};
    inline int        s_nRareCaller                  = 0;
    inline int        s_nRareDropped                 = 0;
    inline u32        s_uRngCalls                    = 0;

    inline void RareCallerNote(u32 uSite) {
        ++s_uRngCalls;
        for (int k = 0; k < s_nRareCaller; ++k)
            if (s_arRareCaller[k].uSite == uSite) {
                ++s_arRareCaller[k].uCount;
                return;
            }
        if (s_nRareCaller >= kRareCallerMax) {
            ++s_nRareDropped;
            return;
        }
        s_arRareCaller[s_nRareCaller].uSite  = uSite;
        s_arRareCaller[s_nRareCaller].uCount = 1;
        ++s_nRareCaller;
    }

    inline void RareCallerReport(s32 snoWorld) {
        if (!global_config.rare_cheats.world_gen_probe)
            return;
        int nRare = 0;
        for (int k = 0; k < s_nRareCaller; ++k)
            if (s_arRareCaller[k].uCount <= kRareCallerCap)
                ++nRare;
        PRINT("[d3hack-rare] world %d \"%s\": %u RNG calls from %d distinct callers, %d of "
              "them used <=%u times (%d callers dropped)",
              static_cast<int>(snoWorld), WorldName(snoWorld), s_uRngCalls, s_nRareCaller,
              nRare, kRareCallerCap, s_nRareDropped)
        for (u32 uWant = 1; uWant <= kRareCallerCap; ++uWant)
            for (int k = 0; k < s_nRareCaller; ++k)
                if (s_arRareCaller[k].uCount == uWant)
                    PRINT("[d3hack-rare]   %06X  x%u", s_arRareCaller[k].uSite, uWant)
        s_nRareCaller  = 0;
        s_nRareDropped = 0;
        s_uRngCalls    = 0;
    }

    // d3hack-custom: THE PICK. This is the function that chooses a rift floor's map.
    //
    // Found by rarity, after four rounds of range-hunting failed. Between two floors the RNG
    // core is called 35-72 MILLION times from ~230 distinct sites; censusing the callers and
    // keeping only those used <= 8 times left ~40 per floor, and only three fired EXACTLY once
    // on both floors. Of those, 0x4C13E8 is a float lerp and 0x78FA18 is an FNV hash bucket.
    // This one is real:
    //
    //     0x785CA0  mov  w9, #0x450             the world-entry record, again
    //     0x785CA8  ldr  w0, [x8, #8]           its world SNO
    //     0x785CAC  bl   0x8173A0               is there an explicit choice?
    //     0x785CB4  str  w0, [x19, #0x1980]     if so, store it and skip the roll
    //     0x785CB8  b.ne 0x785D44
    //     0x785CD0  ldr  w8, [x8, #0x8c]        candidate list, size in BYTES
    //     0x785CD8  lsr  w22, w8, #2            count = bytes / 4  -> array of u32
    //     0x785CE4  bl   0xa481B0               init a LOCAL generator...
    //     0x785CFC  bl   0xa481E0               ...seeded from 0x8158F0, so the choice is
    //     0x785D0C  bl   0xa48200               reproducible for a given rift
    //     0x785D14  udiv w8, w0, w22 / msub     roll % count
    //     0x785D2C  ldr  w8, [x9, w8, sxtw #2]  INDEX THE ARRAY  <-- w8 is now the chosen SNO
    //     0x785D30  str  w8, [x19, #0x1980]     <-- HOOKED HERE, before the store
    //
    // At 0x785D30 everything needed is live: w8 is the chosen value, x9 is the array, w22 is
    // the count. Reading count*4 bytes forward from x9 is bounded by the game's OWN count, not
    // by a guess, so it does not break the rule the crashed run bought.
    //
    // Banning is then the whole feature and costs three lines: if the choice is on the ban
    // list, walk the array for one that is not and overwrite w8 before the store runs.
    inline constexpr int kPickListLogMax = 64;

    inline void RiftPickReport(const s32 *pArr, int nCount, s32 snoPicked, s32 snoNew,
                               uintptr_t uCtx) {
        static int s_nLogged = 0;
        if (s_nLogged >= 10)
            return;
        ++s_nLogged;
        const char *szPick = WeatherName(snoPicked);
        PRINT("[d3hack-pick] WEATHER chosen: %d \"%s\" from %d candidates%s", snoPicked,
              szPick != nullptr ? szPick : "?", nCount,
              (snoNew != snoPicked) ? "  (BANNED -> replaced)" : "")
        // x19 is the object the pick is stored into (str w8, [x19, #0x1980]). The weather list
        // was fetched using [x19+0x68] as a key, so whatever identifies the TILESET is in here.
        // Bounded read off the pointer the game dereferences at this very instruction.
        if (uCtx > 0x1000ull) {
            const auto *pW = reinterpret_cast<const s32 *>(uCtx);
            for (int off : {0x60, 0x64, 0x68, 0x6C, 0x70, 0x74}) {
                const s32   v  = pW[off / 4];
                const char *sn = SnoName(v);
                PRINT("[d3hack-pick]   x19+%02X = %d  %s", off, v, sn != nullptr ? sn : "-")
            }
        }
        const int nShow = (nCount < 24) ? nCount : 24;
        for (int k = 0; k < nShow; ++k) {
            const char *szName = WeatherName(pArr[k]);
            PRINT("[d3hack-pick]   [%02d] %d %s", k, pArr[k],
                  szName != nullptr ? szName : "?")
        }
    }

    // d3hack-custom: THE TILESET ASSIGNMENT. This is where a rift floor gets its map.
    //
    // The weather hook handed this over. It reads the floor's tileset from x19+0x68 to choose a
    // matching weather list, and that field read as 275930 = X1_LR_Tileset_Leorics on a floor
    // the user was calling "Halls of Agony" -- a real rift tileset World SNO from
    // re/riftworlds.txt. So +0x68 IS the map. Searching for writes to it found 0x78866C:
    //
    //     0x78865C  ldr   x8, [x19, #0x1910]      a table hanging off the same object
    //     0x788660  add   x9, x25, x25, lsl #2    index * 5
    //     0x788664  lsl   x9, x9, #2              ... * 4  -> 0x14-byte records
    //     0x788668  ldrsw x8, [x8, x9]            row[index].field0
    //     0x78866C  str   w8, [x20, #0x68]        <-- HOOKED, w8 is the tileset about to land
    //
    // A per-floor plan, laid out in one table. That also explains the oddest earlier result --
    // floors 2..5 did no RNG work at all inside sInitializeWorld, because the whole rift's
    // floor plan is decided up front, not per floor on entry.
    //
    // Banning: if the assigned tileset is on the list, take another row from the SAME table.
    // Deliberately not "any map from my 164-entry list" -- those include event and dev maps
    // (x1_lr_tileset_monster_prototypes among them) that a rift may not be able to build. A row
    // already in this rift's own plan is known-good by construction. Worst case it duplicates
    // another floor's map, which is a far better failure than a broken floor.
    //
    // Every candidate is validated against the known rift-map table before use, so a garbage
    // row can never be substituted. If nothing valid and unbanned is found, the original
    // stands.
    inline constexpr int kTilesetRowMax    = 32;
    inline constexpr int kTilesetRowStride = 0x14;

    // d3hack-custom: turn x1_lr_tileset_exterior_graveyard_small into
    // "Exterior Graveyard Small" -- readable on screen, while staying close enough to the
    // config name that the user can find it in rift-maps.txt without a translation table.
    // The prefix is dropped, underscores become spaces, each word is capitalised.
    inline void PrettyMapName(const char *szRaw, char *szOut, int nCap) {
        szOut[0] = 0;
        if (szRaw == nullptr || nCap < 2)
            return;
        int i = 0;
        // Skip the "<prefix>_lr_tileset_" lead-in, whatever the prefix is (x1/p1/p4/px/p43/p6).
        for (int k = 0; szRaw[k] != 0; ++k)
            if ((szRaw[k] == 't' || szRaw[k] == 'T') && k >= 3) {
                const char *p = szRaw + k;
                if ((p[0] | 32) == 't' && (p[1] | 32) == 'i' && (p[2] | 32) == 'l' &&
                    (p[3] | 32) == 'e' && (p[4] | 32) == 's' && (p[5] | 32) == 'e' &&
                    (p[6] | 32) == 't' && p[7] == '_') {
                    i = k + 8;
                    break;
                }
            }
        int  n     = 0;
        bool bWord = true;
        for (; szRaw[i] != 0 && n < nCap - 1; ++i) {
            char c = szRaw[i];
            if (c == '_') {
                szOut[n++] = ' ';
                bWord      = true;
                continue;
            }
            if (bWord && c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 32);
            else if (!bWord && c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + 32);
            szOut[n++] = c;
            bWord      = false;
        }
        szOut[n] = 0;
    }

    inline void RiftTilesetReport(s32 snoOld, s32 snoNew, int nIdx) {
        const char *szOld = RiftMapName(snoOld);
        const char *szNew = RiftMapName(snoNew);

        static int s_nLogged = 0;
        if (s_nLogged < 16) {
            ++s_nLogged;
            PRINT("[d3hack-map] floor %d tileset: %d \"%s\"%s", nIdx, snoOld,
                  szOld != nullptr ? szOld : "?",
                  RiftMapIsBanned(snoOld) ? "   <-- BANNED" : "")
            if (snoNew != snoOld)
                PRINT("[d3hack-map]   replaced with %d \"%s\"", snoNew,
                      szNew != nullptr ? szNew : "?")
        }

        // d3hack-custom: name the map on screen, so the ban list can be written from play
        // instead of from the log. The whole point of the feature is deciding which maps you
        // dislike, and that decision happens while looking at one.
        if (!global_config.rare_cheats.map_name_overlay)
            return;
        char szPretty[64];
        PrettyMapName(szNew != nullptr ? szNew : szOld, szPretty, sizeof(szPretty));
        if (szPretty[0] == 0)
            return;
        if (snoNew != snoOld)
            d3::imgui_overlay::PostCombatLog(0.75f, 0.85f, 1.00f, "Map: %s  (replaced a banned map)",
                                             szPretty);
        else
            d3::imgui_overlay::PostCombatLog(0.75f, 0.85f, 1.00f, "Map: %s", szPretty);
    }

    HOOK_DEFINE_INLINE(RiftTilesetAssign) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            ResolveRiftBans();
            const s32 snoOld = static_cast<s32>(ctx->W[8]);
            const int nIdx   = static_cast<int>(ctx->W[25]);
            s32       snoNew = snoOld;

            // d3hack-custom: THE SUBSTITUTION IS DISARMED. This hook is a readout now.
            //
            // It picked "the first non-banned rift map in the table" -- which is failure mode
            // #1 from HANDOFF.md word for word:
            //
            //     1. any map from the table       -> highlands, stuck
            //
            // and it had no TrueGRLevel() gate, so it also fired in Nephalem rifts, which
            // HANDOFF separately records as broken by banning. HANDOFF said "SUBSTITUTION IS
            // DISABLED" and named only RiftMapAssign; this was the third copy of the same
            // write, alongside RiftAreaSwap. All three are inert now.
            //
            // The comment directly below this hook also states 0x78866C "never fired -- so it
            // writes some other object's +0x68, not this one". A write that is believed never
            // to fire is not a safe write. It is an untested one.
            //
            // A constant rather than an early return: snoNew must stay equal to snoOld so the
            // report below cannot claim a swap that did not happen.
            constexpr bool kSubstitutionEnabled = false;

            if (kSubstitutionEnabled && s_nBannedMap > 0 && RiftMapIsBanned(snoOld)) {
                const auto uTable = static_cast<uintptr_t>(ctx->X[8]);
                // x8 held the table pointer at 0x78865C and is reloaded here with the row's
                // value, so re-derive the table from x19 rather than trusting x8.
                const auto uObj = static_cast<uintptr_t>(ctx->X[19]);
                if (uObj > 0x1000ull) {
                    const auto uRows =
                        *reinterpret_cast<const uintptr_t *>(uObj + 0x1910);
                    if (uRows > 0x1000ull) {
                        const auto *pRows = reinterpret_cast<const u8 *>(uRows);
                        for (int k = 0; k < kTilesetRowMax; ++k) {
                            const s32 sno = *reinterpret_cast<const s32 *>(
                                pRows + (k * kTilesetRowStride));
                            if (RiftMapName(sno) == nullptr)   // not a known rift map
                                continue;
                            if (RiftMapIsBanned(sno))
                                continue;
                            snoNew = sno;
                            break;
                        }
                    }
                }
                if (snoNew != snoOld)
                    ctx->W[8] = static_cast<u64>(static_cast<u32>(snoNew));
                (void) uTable;
            }

            if (global_config.rare_cheats.world_gen_probe ||
                global_config.rare_cheats.map_name_overlay || snoNew != snoOld)
                RiftTilesetReport(snoOld, snoNew, nIdx);
        }
    };

    // d3hack-custom: name the floor's map on screen.
    //
    // Announced from the WEATHER hook, not from the tileset assignment. 0x78866C looked like
    // the assignment and hooked cleanly, but never fired -- so it writes some other object's
    // +0x68, not this one. The function that picks weather (0x785950) only ever READS +0x68
    // (four loads, no stores) and has no direct callers or data references, so the object is
    // built somewhere still unfound.
    //
    // None of which matters for showing a name: the weather pick runs once per floor and the
    // tileset is sitting right there in x19+0x68. Read it there and announce it. Finding the
    // assignment is only needed for BANNING, and that hunt continues separately.
    //
    // Deduped on the SNO so re-entering a floor does not spam, but a repeat of the same map on
    // a later floor still announces -- the counter resets whenever the map changes.
    // d3hack-custom: predict the NEXT floor's map.
    //
    // The user spotted this in the log and it holds up: the game PRE-BUILDS the next floor
    // while you are still fighting the current one.
    //
    //     world 288454 "x1_lr_level_01"   <- you enter floor 1
    //     floor map: 331387 fortress       <- floor 1's map
    //     world 288685 "x1_lr_level_02"   <- floor 2 built, still on floor 1
    //     floor map: 337001 wormcaves      <- floor 2's map, known in advance
    //
    // Same fact that explains why floors 2+ do zero RNG work on entry: nothing is chosen when
    // you take the portal because it was chosen earlier. So "Next Map" is a readout, not a
    // guess -- which is the only reason it is worth showing at all.
    //
    // The floor number comes from the world SNO, which is x1_lr_level_NN. Rather than parse
    // the name, keep the known ids in order: they are asset ids and do not renumber.
    inline constexpr s32 kLrLevelWorlds[] = {
        288454, 288685, 288687, 288798, 288800, 288802, 288804, 288810, 288814, 288816,
    };

    inline auto LrFloorOf(s32 snoWorld) -> int {
        for (int k = 0; k < static_cast<int>(sizeof(kLrLevelWorlds) / sizeof(kLrLevelWorlds[0]));
             ++k)
            if (kLrLevelWorlds[k] == snoWorld)
                return k + 1;
        return 0;   // not a rift floor
    }

    inline s32  s_snoLastAnnouncedMap = 0;
    inline int  s_nPickFloor          = 0;   // floor whose world was most recently initialised
    inline int  s_nCurFloor           = 0;
    inline int  s_nNextFloor          = 0;
    inline char s_szCurMap[64]        = {};
    inline char s_szNextMap[64]       = {};

    // d3hack-custom: the map announced for each floor, indexed 1..10.
    //
    // The panel used to be driven purely by "is this announce for the floor after the one I am
    // showing", with promotion to CURRENT happening only in WorldGenReport. That never fired on
    // arrival: the game PRE-BUILDS floor N+1 while you are still on floor N, so WorldGenReport
    // for floor N+1 had already run long before you took the portal. The result was a panel
    // that showed the map you were standing in as "Next" and never advanced "Current".
    //
    // Keeping the name per floor makes the panel a pure function of "which floor is being
    // pre-built", with no promotion state to get stuck.
    inline char s_arFloorMap[11][64] = {};
    inline int  s_nPrevAnnounced     = 0;

    // Last GR level seen on the announce path. The arrival path below runs on the message
    // thread and must not call TrueGRLevel() -- that is TieredLootRunGetLevel(), a game call,
    // and calling game functions from a world-gen/message thread is what killed the first
    // world-factory probe. Reuse the value instead.
    inline int s_nLastGRSeen = 0;

    // d3hack-custom: the player has ARRIVED on nFloor. Called from the world factory when the
    // RevealWorldMessage path creates the floor's world (arg2 == 1).
    //
    // PROVEN by timestamps, not assumed. Per floor the log shows a fixed sequence, and the gap
    // inside it is the whole story:
    //
    //     12:22:19  [map] floor 2 map: pandext        <- pre-build, player still on floor 1
    //     12:22:48  [wc]  CREATE world 288685 arg2=1  <- 29 s later: the portal was taken
    //
    // Floor 1 shows the same two events 2 s apart, because floor 1 is entered the moment the
    // rift opens. The 29 s on floor 2 is the player fighting through floor 1. So the reveal is
    // arrival, and the pre-build announce is not.
    inline void MapArrivedAtFloor(int nFloor);

    inline void MapPipelineReset() {
        s_nCurFloor  = 0;
        s_nNextFloor = 0;
        s_szCurMap[0]  = 0;
        s_szNextMap[0] = 0;
        for (int k = 0; k < 11; ++k)
            s_arFloorMap[k][0] = 0;
        s_snoLastAnnouncedMap = 0;
    }

    inline void CopyStr(char *pDst, int nCap, const char *pSrc) {
        int k = 0;
        for (; pSrc[k] != 0 && k < nCap - 1; ++k)
            pDst[k] = pSrc[k];
        pDst[k] = 0;
    }


    inline void AnnounceMapOnce(s32 snoTileset) {
        const char *szRaw = RiftMapName(snoTileset);
        if (szRaw == nullptr)
            return;   // not a rift tileset -- town, cellar, and so on

        char szPretty[64];
        PrettyMapName(szRaw, szPretty, sizeof(szPretty));
        if (szPretty[0] == 0)
            return;

        const bool bBanned = RiftMapIsBanned(snoTileset);
        PRINT("[d3hack-map] floor %d map: %d \"%s\"%s", s_nPickFloor, snoTileset, szRaw,
              bBanned ? "  (on the ban list)" : "")

        // Slot it by floor. A pick for a HIGHER floor than the one we are showing is the
        // pre-build, so it becomes "next"; anything else replaces "current". Going backwards
        // (or to floor 1) means a new rift, so the pipeline resets rather than showing a
        // stale prediction from the last one.
        const int nFloor = s_nPickFloor;

        // Going backwards (or back to floor 1) means a new rift, so drop the old names rather
        // than showing a stale prediction from the last one.
        if (nFloor > 0 && nFloor < s_nPrevAnnounced)
            MapPipelineReset();
        s_nPrevAnnounced = nFloor;

        if (nFloor >= 1 && nFloor <= 10)
            CopyStr(s_arFloorMap[nFloor], sizeof(s_arFloorMap[0]), szPretty);

        // The game pre-builds exactly one floor ahead, so an announce for floor F means the
        // player is standing on F-1 -- except for floor 1, which is announced as you arrive.
        // Deriving both slots from the table each time means the panel cannot get stuck.
        const int nOn = (nFloor <= 1) ? 1 : (nFloor - 1);
        s_nCurFloor   = nOn;
        s_nNextFloor  = (nOn + 1 <= 10 && s_arFloorMap[nOn + 1][0] != 0) ? (nOn + 1) : 0;
        CopyStr(s_szCurMap, sizeof(s_szCurMap), s_arFloorMap[nOn]);
        if (s_nNextFloor != 0)
            CopyStr(s_szNextMap, sizeof(s_szNextMap), s_arFloorMap[nOn + 1]);
        else
            s_szNextMap[0] = 0;
        s_snoLastAnnouncedMap = snoTileset;

        s_nLastGRSeen = TrueGRLevel();

        if (!global_config.rare_cheats.map_name_overlay)
            return;
        d3::imgui_overlay::SetMapInfo(s_szCurMap, s_szNextMap, s_nLastGRSeen);
        d3::imgui_overlay::PostCombatLog(0.75f, 0.85f, 1.00f, "Map: %s", szPretty);
    }

    // d3hack-custom: who calls the floor-setup function?
    //
    // 0x785950 takes the object whose +0x68 is the tileset, and only ever READS that field --
    // four loads, no stores. So the caller sets it. But it has NO direct callers: no BL, no B,
    // and the only reference anywhere is a single u64 in .rodata at 0xD5C3C8, i.e. it is
    // dispatched indirectly through a table. Trying to walk that table statically produced
    // 54748 "records", which is nonsense -- the record shape matched unrelated data.
    //
    // So stop reading and measure. At function entry the link register IS the caller. One
    // inline hook at 0x785950, print X[30] minus the module base, and the dispatcher names
    // itself. x0 goes with it to confirm it is the same object the weather pick later uses.
    // d3hack-custom: walk the frame chain. One run, the whole stack.
    //
    // The caller turned out to be a VIRTUAL call -- 0x4BC4B8 does
    // `ldr x8,[x21]; ldr x8,[x8]; blr x8`, so 0x785950 is vtable slot 0 and x21 is `this`.
    // Its +0x68 is already the tileset on entry, and 0x4BC420 (the enclosing function) never
    // writes +0x68, so the value is set further up.
    //
    // Chasing that one frame per launch would take all night, and the vtable pointer itself is
    // installed by a load-time relocation -- nothing in .text materialises 0xD5C3C8, so there
    // is no constructor to find statically either.
    //
    // AArch64 frames are standard here (stp x29,x30 / add x29,sp,#n), so [x29] is the previous
    // frame and [x29+8] is its return address. Walking that chain gives every caller at once.
    // Each step is validated -- frame pointers must increase, stay 16-aligned and stay in the
    // same region -- so a leaf function without a frame stops the walk instead of wandering.
    // d3hack-custom: walk, but only PRINT chains we have not seen before.
    //
    // A flat "first N walks" budget keeps getting spent by world setup before combat starts --
    // three times now on the Momentum hunt. Deduping on the chain itself fixes that properly:
    // the setup chains print once each, and any genuinely new chain (a combat hit, an
    // auto-fired primary) is still new and still prints.
    inline void WalkStack(const char *szWhat, uintptr_t uFp);

    inline auto StackChainHash(uintptr_t uFp, int nMax) -> u64 {
        const uintptr_t uBase = GameOffset(0);
        u64             h     = 1469598103934665603ull;
        uintptr_t       uPrev = 0;
        for (int k = 0; k < nMax; ++k) {
            if (uFp <= uPrev || (uFp & 15ull) != 0ull || uFp < 0x1000ull)
                break;
            if (uPrev != 0 && (uFp - uPrev) > 0x100000ull)
                break;
            const uintptr_t uNext = *reinterpret_cast<const uintptr_t *>(uFp);
            const uintptr_t uRet  = *reinterpret_cast<const uintptr_t *>(uFp + 8);
            if (uRet <= uBase || (uRet - uBase) > 0x1000000ull)
                break;
            h ^= static_cast<u64>(uRet - uBase);
            h *= 1099511628211ull;
            uPrev = uFp;
            uFp   = uNext;
        }
        return h;
    }

    inline u64 s_arChainSeen[24] = {};
    inline int s_nChainSeen      = 0;

    inline void WalkStackOnce(const char *szWhat, uintptr_t uFp) {
        const u64 h = StackChainHash(uFp, 14);
        for (int k = 0; k < s_nChainSeen; ++k)
            if (s_arChainSeen[k] == h)
                return;
        if (s_nChainSeen >= 24)
            return;
        s_arChainSeen[s_nChainSeen++] = h;
        WalkStack(szWhat, uFp);
    }

    inline void WalkStack(const char *szWhat, uintptr_t uFp) {
        const uintptr_t uBase = GameOffset(0);
        PRINT("[d3hack-stack] --- %s ---", szWhat)
        uintptr_t uPrev = 0;
        for (int k = 0; k < 14; ++k) {
            if (uFp <= uPrev || (uFp & 15ull) != 0ull || uFp < 0x1000ull)
                break;
            if (uPrev != 0 && (uFp - uPrev) > 0x100000ull)
                break;
            const uintptr_t uNext = *reinterpret_cast<const uintptr_t *>(uFp);
            const uintptr_t uRet  = *reinterpret_cast<const uintptr_t *>(uFp + 8);
            if (uRet <= uBase || (uRet - uBase) > 0x1000000ull)
                break;
            PRINT("[d3hack-stack]   [%2d] %06X", k, static_cast<u32>(uRet - uBase))
            uPrev = uFp;
            uFp   = uNext;
        }
    }

    HOOK_DEFINE_INLINE(WeatherFnEntry) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            static int s_n = 0;
            if (s_n >= 3)
                return;
            ++s_n;
            WalkStack("785950 entry", static_cast<uintptr_t>(ctx->X[29]));
            const uintptr_t uBase = GameOffset(0);
            const uintptr_t uLr   = static_cast<uintptr_t>(ctx->X[30]);
            const uintptr_t uObj  = static_cast<uintptr_t>(ctx->X[0]);
            s32 snoTile = 0;
            if (uObj > 0x1000ull)
                snoTile = *reinterpret_cast<const s32 *>(uObj + 0x68);
            const char *szTile = RiftMapName(snoTile);
            PRINT("[d3hack-caller] 785950 called from %06X  obj=%p  +68=%d \"%s\"",
                  (uLr > uBase) ? static_cast<u32>(uLr - uBase) : 0u,
                  reinterpret_cast<void *>(uObj), snoTile,
                  szTile != nullptr ? szTile : "?")
        }
    };

    // d3hack-custom: THE BAN. This is where a floor's map is written.
    //
    //     0x4BC434  mov x21, x0                    the floor object
    //     0x4BC440  mov w19, w3
    //     0x4BC444  mov w20, w2                    <- the tileset, passed IN as an argument
    //     0x4BC450  stp w20, w19, [x21, #0x68]     <- HOOKED, writes +0x68 and +0x6C together
    //     0x4BC4C4  blr x8                         ... then the floor gets built
    //
    // Every earlier scan missed this because it is an **STP**, a store-PAIR, and I was
    // searching for `str w,[x,#0x68]`, which encodes completely differently. 0x78866C looked
    // like the assignment, hooked cleanly and never fired -- it writes some other object.
    //
    // Hooking before the store means the map can be changed BEFORE anything is generated from
    // it, which is the only safe place to do this. Rewriting it after the floor exists would
    // leave the scenes and the field disagreeing.
    //
    // Substitution is deterministic -- keyed off the original SNO -- so a rift still generates
    // the same way twice, and it degrades safely: if the ban list somehow leaves nothing, the
    // original map stands rather than a floor failing to build.
    // d3hack-custom: only ever substitute a map the GAME ITSELF has used for a rift floor.
    //
    // The first version drew replacements from the whole 164-entry map table and handed the
    // user x1_lr_tileset_exterior_highlands, which is NOT a valid Greater Rift tileset -- the
    // floor loaded with an invisible wall at the entrance and the run was stuck. Being in the
    // x1_lr_tileset_ namespace does not make an asset usable as a rift floor.
    //
    // There is no reliable static test for "is this a legal rift tileset", so do not guess:
    // LEARN it. Every value the game assigns at 0x4BC450 is by definition one the game
    // considered legal for a rift, so record those and substitute only from that set. It costs
    // nothing, it cannot be wrong, and it needs no table of exceptions to maintain.
    //
    // Bootstrapping is deliberately conservative: until the pool has a usable entry, NO swap
    // happens and the banned map stands. A banned map occasionally slipping through is a far
    // better failure than a floor you cannot walk out of.
    inline constexpr int kSeenMapMax = 128;

    inline s32 s_arSeenMap[kSeenMapMax] = {};
    inline int s_nSeenMap               = 0;

    inline void NoteMapAssigned(s32 sno) {
        if (sno == 0 || s_nSeenMap >= kSeenMapMax)
            return;
        for (int k = 0; k < s_nSeenMap; ++k)
            if (s_arSeenMap[k] == sno)
                return;
        s_arSeenMap[s_nSeenMap++] = sno;
    }

    // d3hack-custom: known-unusable maps. NOT everything in the table works as a rift floor.
    //
    // x1_lr_tileset_exterior_highlands loads with an invisible wall at the entrance and traps
    // the run. It is the only one confirmed so far. Add to this list when another turns up --
    // and only on evidence, never on suspicion, because every entry here shrinks the pool.
    // d3hack-custom: a map's SIZE CLASS, from its name.
    //
    // This is the real constraint, and it took two stuck runs to see it. Substituting
    // festeringwoodsdrlg_LARGE in place of corruptspire_SMALL produced a floor the player
    // could walk a few steps into and no further; Highlands (no size suffix at all) did the
    // same earlier. The maps were never the problem -- the SIZE MISMATCH was. The game plans a
    // floor with an area budget, and a tileset built for a different budget does not fit the
    // space, so the navmesh runs out before the geometry does.
    //
    // So a replacement must be the same size class as the map it replaces. That shrinks the
    // pool, sometimes to nothing -- in which case no swap happens and the banned map stands,
    // which is the correct trade. A floor you cannot cross is far worse than a floor you
    // dislike.
    enum class MapSize { Normal, Small, Large, ExtraLarge };

    inline auto MapSizeOf(const char *sz) -> MapSize {
        if (sz == nullptr)
            return MapSize::Normal;
        // Scan for the marker anywhere: several names carry a trailing _gg / _gr after it.
        for (int i = 0; sz[i] != '\0'; ++i) {
            if (sz[i] != '_')
                continue;
            const char *p = sz + i + 1;
            if (p[0] == 'e' && p[1] == 'x' && p[2] == 't' && p[3] == 'r' && p[4] == 'a')
                return MapSize::ExtraLarge;
            if (p[0] == 'l' && p[1] == 'a' && p[2] == 'r' && p[3] == 'g' && p[4] == 'e')
                return MapSize::Large;
            if (p[0] == 's' && p[1] == 'm' && p[2] == 'a' && p[3] == 'l' && p[4] == 'l')
                return MapSize::Small;
        }
        return MapSize::Normal;
    }

    inline constexpr s32 kUnusableMaps[] = {
        // x1_lr_tileset_exterior_highlands. Blocked as a SUBSTITUTION TARGET only -- it is not
        // on the ban list, so the game may still use it if it ever chooses to.
        //
        // The distinction matters. Across every log the game has never once assigned this map
        // to a rift floor itself, so the only way a player reaches it is if this code puts them
        // there -- and when it did, the floor loaded with an invisible wall at the entrance and
        // the run was stuck. Whether that is a size mismatch or the map simply not working as a
        // rift tileset is still unproven, and either way it is not worth another trapped run to
        // find out.
        //
        // Add entries here only on evidence of a real failure. Every one shrinks the pool.
        347368,
    };

    inline auto MapIsUnusable(s32 sno) -> bool {
        for (s32 bad : kUnusableMaps)
            if (bad != 0 && bad == sno)
                return true;
        return false;
    }

    // Replacement pool, two tiers.
    //
    // The learn-only version was too strict and broke the feature outright: with a large ban
    // list the game rarely assigns a KEPT map, so the pool stayed empty and nothing was ever
    // swapped. Large variants had been substituting fine before that change -- the only map
    // that ever misbehaved was Highlands, which is now blocked by name.
    //
    // So: prefer maps the game has actually assigned this session, because those are proven
    // usable; but fall back to any kept, non-blocked map rather than giving up. The fallback is
    // how this worked when it worked.
    // Rank sizes so the rule can be "replacement must FIT", not "must match exactly".
    inline auto MapSizeRank(MapSize e) -> int {
        switch (e) {
            case MapSize::Small:      return 0;
            case MapSize::Normal:     return 1;
            case MapSize::Large:      return 2;
            case MapSize::ExtraLarge: return 3;
        }
        return 1;
    }

    // d3hack-custom: the replacement must FIT the floor, not match it.
    //
    // Exact size matching was too strict, and the evidence says so. Every substitution that
    // broke had a SMALL original -- corruptspire_small -> highlands, corruptspire_small ->
    // festeringwoodsdrlg_large, both left the player walking a few steps and stopping. Every
    // substitution into a NORMAL original worked fine, including two that swapped in _large
    // maps (boneyards -> battlefields_large, corruptspire -> keep_war_large).
    //
    // So the constraint is one-directional: a floor budgeted small cannot hold a bigger
    // tileset, but a floor with room to spare can hold a smaller one. Allowing
    // replacement <= original keeps every swap that has ever worked and blocks every one that
    // has ever failed, while giving small floors something to fall back on rather than being
    // stuck with a banned map.
    inline auto PickReplacementMap(s32 snoBanned) -> s32 {
        const int nMaxRank = MapSizeRank(MapSizeOf(RiftMapName(snoBanned)));
        for (int nPass = 0; nPass < 2; ++nPass) {
            const bool bVerifiedOnly = (nPass == 0);
            const int  nSeen         = bVerifiedOnly ? s_nSeenMap
                                                     : static_cast<int>(sizeof(kRiftMaps) /
                                                                        sizeof(kRiftMaps[0]));
            int nOk = 0;
            for (int k = 0; k < nSeen; ++k) {
                const s32 sno = bVerifiedOnly ? s_arSeenMap[k] : kRiftMaps[k].sno;
                if (RiftMapIsBanned(sno) || MapIsUnusable(sno))
                    continue;
                if (MapSizeRank(MapSizeOf(RiftMapName(sno))) > nMaxRank)
                    continue;
                ++nOk;
            }
            if (nOk == 0)
                continue;
            int nWant = static_cast<int>(static_cast<u32>(snoBanned) % static_cast<u32>(nOk));
            for (int k = 0; k < nSeen; ++k) {
                const s32 sno = bVerifiedOnly ? s_arSeenMap[k] : kRiftMaps[k].sno;
                if (RiftMapIsBanned(sno) || MapIsUnusable(sno))
                    continue;
                if (MapSizeRank(MapSizeOf(RiftMapName(sno))) > nMaxRank)
                    continue;
                if (nWant-- == 0)
                    return sno;
            }
        }
        return snoBanned;   // nothing that fits -- leave it alone
    }

    // d3hack-custom: substitute the LEVEL-AREA ID, not the tileset SNO.
    //
    // This is the fix the size rule was standing in for. Swapping the SNO at 0x4BC450 is too
    // late: by then the floor has already been budgeted from the ORIGINAL map, so a bigger
    // tileset overflows the space and the navmesh runs out mid-floor -- "you move a bit and
    // then it stops".
    //
    // What actually travels through the plan is an id, not a map:
    //
    //     0x784EE4  mov w0, w1              w1 = the floor's level-area id
    //     0x784EF0  bl  0x677180            resolve it...
    //     0x784EF4  mov w21, w0             ...to the tileset SNO, which flows downstream
    //
    // and 0x677180 is a plain lookup: 0xD9460(id) -> record, then `ldr w0, [x0, #8]`.
    //
    // The SIZE is a property of the ID, not of the SNO -- cath_small and cath_large are
    // different level areas. So substituting the id substitutes the map AND its budget
    // together, and everything downstream derives consistently. No size rule needed.
    //
    // The id -> SNO mapping is not in the executable, so LEARN it: every resolve the game
    // performs is recorded, which also guarantees any id used as a replacement is one the game
    // itself uses. Nothing is ever invented.
    inline constexpr int kAreaIdMax = 256;

    struct AreaId {
        s32 nId;
        s32 snoMap;
    };

    inline AreaId s_arAreaId[kAreaIdMax] = {};
    inline int    s_nAreaId              = 0;

    inline void NoteAreaId(s32 nId, s32 snoMap) {
        if (nId == 0 || snoMap == 0 || RiftMapName(snoMap) == nullptr)
            return;
        for (int k = 0; k < s_nAreaId; ++k)
            if (s_arAreaId[k].nId == nId)
                return;
        if (s_nAreaId >= kAreaIdMax)
            return;
        s_arAreaId[s_nAreaId].nId    = nId;
        s_arAreaId[s_nAreaId].snoMap = snoMap;
        ++s_nAreaId;
    }

    inline auto AreaIdMap(s32 nId) -> s32 {
        for (int k = 0; k < s_nAreaId; ++k)
            if (s_arAreaId[k].nId == nId)
                return s_arAreaId[k].snoMap;
        return 0;
    }

    // Pick a replacement ID whose map is kept. Deterministic, and restricted to ids the game
    // has actually resolved, so the id is always one the game considers legal.
    inline auto PickReplacementAreaId(s32 nBannedId) -> s32 {
        int nOk = 0;
        for (int k = 0; k < s_nAreaId; ++k)
            if (!RiftMapIsBanned(s_arAreaId[k].snoMap))
                ++nOk;
        if (nOk == 0)
            return nBannedId;
        int nWant = static_cast<int>(static_cast<u32>(nBannedId) % static_cast<u32>(nOk));
        for (int k = 0; k < s_nAreaId; ++k)
            if (!RiftMapIsBanned(s_arAreaId[k].snoMap) && nWant-- == 0)
                return s_arAreaId[k].nId;
        return nBannedId;
    }

    // Record every id -> map the game resolves. Read-only; it never changes the answer.
    HOOK_DEFINE_TRAMPOLINE(TilesetResolve) {
        static auto Callback(s32 nId) -> s32 {
            const s32 snoMap = Orig(nId);
            const int nWas   = s_nAreaId;
            NoteAreaId(nId, snoMap);
            // Proof of life. Two runs produced zero swaps at either level and there was no way
            // to tell whether the resolver was firing, whether it returned rift maps, or
            // whether the table was simply empty. Log the first handful of NEW pairs and every
            // rejection reason once, rather than guessing again.
            // Log ONLY resolves that matter -- ones that produced a rift map. The first
            // version burned all 16 lines on town worlds before the rift even opened, which is
            // the same budget mistake as the shard probes: the interesting case never got
            // recorded because the boring one filled the log first.
            if (s_nAreaId > nWas) {
                static int s_nLog = 0;
                if (s_nLog < 24) {
                    ++s_nLog;
                    PRINT("[d3hack-area] resolve id=0x%08X -> %d \"%s\" RECORDED (table %d)",
                          static_cast<u32>(nId), snoMap, RiftMapName(snoMap), s_nAreaId)
                }
            }
            return snoMap;
        }
    };

    // d3hack-custom: READ-ONLY probe on the WORLD FACTORY. Nothing is written here.
    //
    // 0xD9530 is where a world INSTANCE is created:
    //
    //     CreateWorld(w0 = world SNO, w1 = existing handle or -1, w2 = ?, w3..w7, 2 stack args)
    //       -> 0xD96A0 allocates the handle
    //       -> 0x676B40 initialises the record, storing the world SNO at record+8
    //
    // and record+8 is exactly what 0x677180 reads back. So every later stage -- the plan at
    // 0x784EC0, the floor setup at 0x784980, the +0x68 store at 0x4BC450, the weather pick at
    // 0x785D30 -- is downstream of the SNO handed to THIS call. Tile budget, navmesh and
    // entrance placement all derive from it here, before a handle even exists.
    //
    // That makes this the one place a map substitution could be honest. HANDOFF.md asks for
    // exactly this: "intervene where the floor plan is BUILT, not where the map is written",
    // so that the game derives the whole floor from the replacement instead of inheriting
    // anything from the original.
    //
    // It is a PROBE and not a swap because four separate substitution rules have each trapped
    // the user in a floor they could not leave, and each one looked correct on the evidence
    // available when it was written. The question this answers, from a log rather than from a
    // theory, is: does a GR floor actually come through this call, and is its map already
    // decided at that moment? Nothing gets written until the log says yes.
    //
    // Hooked INLINE at 0xD956C (`mov w22, w0`, mid-prologue, all three argument registers
    // still live) rather than as a trampoline. The real function takes eight register
    // arguments plus two on the stack; declaring a shorter signature would hand Orig() garbage
    // in w3..w7. An inline hook commits to no ABI at all -- the same reasoning that made the
    // engage-event hook at 0x853500 safe.
    inline void MapArrivedAtFloor(int nFloor) {
        if (nFloor < 1 || nFloor > 10)
            return;
        if (s_arFloorMap[nFloor][0] == 0)
            return;   // never announced -- nothing to show, and inventing a name would be worse
        s_nCurFloor = nFloor;
        CopyStr(s_szCurMap, sizeof(s_szCurMap), s_arFloorMap[nFloor]);
        if (nFloor + 1 <= 10 && s_arFloorMap[nFloor + 1][0] != 0) {
            s_nNextFloor = nFloor + 1;
            CopyStr(s_szNextMap, sizeof(s_szNextMap), s_arFloorMap[nFloor + 1]);
        } else {
            s_nNextFloor   = 0;
            s_szNextMap[0] = 0;
        }
        if (global_config.rare_cheats.map_name_overlay)
            d3::imgui_overlay::SetMapInfo(s_szCurMap, s_szNextMap, s_nLastGRSeen);
    }

    HOOK_DEFINE_INLINE(WorldCreateProbe) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            // Arrival first, and NOT behind the probe gate -- the overlay must work whether or
            // not the diagnostic is enabled. arg2 == 1 is the RevealWorldMessage path, which is
            // the client being told about the world it is entering.
            {
                const s32 snoW = static_cast<s32>(ctx->W[0]);
                const s32 nA2  = static_cast<s32>(ctx->W[2]);
                const int nFl  = LrFloorOf(snoW);
                if (nFl > 0 && nA2 == 1)
                    MapArrivedAtFloor(nFl);

                // d3hack-custom: forget the rift tileset when a NON-rift world is created.
                // Load-bearing, so it sits outside the WorldGenProbe gate below.
                //
                // s_snoAssignedMap is what GreaterRiftDensityRiftsOnly tests, and it was never
                // cleared -- so after one rift it would name that rift's map forever and the
                // gate would treat town as a rift, which is the exact thing the setting exists
                // to prevent. Both a rift tileset and an X1_LR_Level_NN floor world count as
                // "in a rift"; anything else ends it.
                if (RiftMapName(snoW) == nullptr && nFl <= 0)
                    s_snoAssignedMap = 0;
            }

            if (!global_config.rare_cheats.world_gen_probe)
                return;

            // !! DO NOT CALL GAME FUNCTIONS FROM THIS HOOK !!
            //
            // The first version called SnoName() here to pretty-print non-rift worlds. It
            // printed three town/menu worlds happily and then killed the game on the first
            // Greater Rift load:
            //
            //     PC  nn::os::GetTlsValue        <- faulted on address 0
            //     X4  main:0x74A510              <- the SNOToString table indexer
            //     LR  main:0x777B18
            //     guest stack: main:0x2bd724     <- the return address of `bl 0xD9530`
            //
            // 0x2BD720 is one of the three world-factory call sites, so the fault was inside
            // the very call this hook sits in. SNOToString reads thread-local state, and rift
            // floor creation runs on a world-gen worker thread (HLE.GuestThread.46) that has
            // no such slot -- so it dereferenced null. Town and menu worlds are created on the
            // main thread, which is exactly why the probe looked fine for three lines.
            //
            // TrueGRLevel() was dropped for the same reason: it calls TieredLootRunGetLevel(),
            // another game function on the same thread, and HANDOFF already warns that
            // generation can run before the tier is even set.
            //
            // Everything used below is a pure local array scan over mod-owned data:
            // RiftMapName walks kRiftMaps, LrFloorOf walks kLrLevelWorlds. No game state is
            // touched, so the thread does not matter. Non-rift worlds are logged as a bare
            // number -- an unnamed line is worth infinitely more than a crash.
            const s32   snoWorld = static_cast<s32>(ctx->W[0]);
            const s32   nHandle  = static_cast<s32>(ctx->W[1]);
            const s32   nArg2    = static_cast<s32>(ctx->W[2]);
            const char *szMap    = RiftMapName(snoWorld);
            const int   nFloor   = LrFloorOf(snoWorld);

            // Two budgets, deliberately. Town and menu worlds are created constantly and would
            // otherwise fill the log before the rift ever opens -- the same mistake that cost
            // the blood-shard and area-id probes their first runs.
            if (szMap != nullptr || nFloor > 0) {
                static int s_nRift = 0;
                if (s_nRift < 80) {
                    ++s_nRift;
                    PRINT("[d3hack-wc] CREATE world %d \"%s\" handle=0x%08X arg2=%d floor=%d",
                          snoWorld, (szMap != nullptr) ? szMap : "(rift level world)",
                          static_cast<u32>(nHandle), nArg2, nFloor)
                }
                return;
            }

            static int s_nOther = 0;
            if (s_nOther < 6) {
                ++s_nOther;
                PRINT("[d3hack-wc] create (non-rift) world %d handle=0x%08X", snoWorld,
                      static_cast<u32>(nHandle))
            }
        }
    };

    // d3hack-custom: READ-ONLY probe on the RIFT FLOOR PLAN. This is the real one.
    //
    // The world factory (0xD9530) turned out NOT to choose the map -- proven in game, not
    // argued: a GR floor creates world 288454 `x1_lr_level_01` with arg2=1. The map is a
    // SECOND value carried beside it, and the two are set independently:
    //
    //     0x676B40  stp w1, w8, [x0]   rec+8 = world SNO (x1_lr_level_NN, the FLOOR)
    //                                  rec+0xC = tileset World SNO (the MAP)
    //     0x676BE0  floor = 0x6777C0(worldSNO); -1 -> not a rift, skip
    //     0x676BEC  only if rec+0xC is still -1
    //     0x676BF8  only if we are the host
    //     0x676C04  bl 0x816E60   <- the pick
    //     0x676C08  str w0,[x20,#4]   rec+0xC = the chosen map
    //
    // and 0x816E60 is only a READ: `ldr w0,[x8,#0x16c]` out of a 10-slot plan at
    // riftState + floor*0x60. The plan is built once per rift, lazily, which is exactly why
    // floors 2+ do no RNG work when you take the portal.
    //
    // WHERE THE CANDIDATE LIST LIVES: nowhere in the executable. All 164 x1_lr_tileset_* SNOs
    // were scanned as dwords across .text/.rodata/.data and as movz/movk immediates across
    // .text -- no cluster of three anywhere. The plan is filled from LUA, from the
    // X1_DungeonFinderEngine script asset in romfs, through the binding `DungeonFinderAddLevel`
    // (0x8DADA0) -> 0x93E8D0 -> 0x817440. That is why four separate table hunts found nothing.
    //
    // 0x817440 RiftPlanAppend(w0 = tileset World SNO, x1 = label, w2 = LevelArea SNO,
    //                         s0/s1/s2 = three floats)
    //
    // !! WHY THIS MATTERS MORE THAN ANY EARLIER SWAP SITE !!
    // A floor entry is a BUNDLE, not a map: tileset World SNO at +0x1CC, three floats at
    // +0x1D0/+0x1D4/+0x1D8 (one of them consumed by worldgen at 0x4C1380), a 63-byte label at
    // +0x1DC, and a **LevelArea SNO at +0x21C**. Every swap tried so far replaced the tileset
    // and left the LevelArea naming the ORIGINAL map. That is a concrete candidate explanation
    // for all four "stuck in a floor I cannot leave" failures, and it is the first one that
    // does not depend on size.
    //
    // Hooked at 0x8174AC (`str w21,[x23,#0x1cc]`), straight-line code, where every field is
    // still in a register: w21 tileset, w19 LevelArea, x20 label, x23 entry, x8 riftState.
    // The floor index is derived by register arithmetic rather than by calling anything.
    //
    // NOTHING IS WRITTEN. The plan-load thread is not known, so -- per the crash this probe's
    // predecessor caused -- only mod-owned data is touched. RiftMapName scans kRiftMaps; the
    // label is an argument the caller already holds and is range-checked before printing.
    HOOK_DEFINE_INLINE(RiftPlanAppendProbe) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (!global_config.rare_cheats.world_gen_probe)
                return;

            const s32  snoMap  = static_cast<s32>(ctx->W[21]);
            const s32  snoArea = static_cast<s32>(ctx->W[19]);
            const auto uEntry  = static_cast<uintptr_t>(ctx->X[23]);
            const auto uBase   = static_cast<uintptr_t>(ctx->X[8]);
            const int  nIdx =
                (uEntry > uBase) ? static_cast<int>((uEntry - uBase) / 0x60) : -1;

            const char *szMap   = RiftMapName(snoMap);
            const auto  uLabel  = static_cast<uintptr_t>(ctx->X[20]);
            const char *szLabel = (uLabel >= 0x1000000000ull && uLabel < 0x8000000000ull)
                                      ? reinterpret_cast<const char *>(uLabel)
                                      : "?";

            static int s_nLog = 0;
            if (s_nLog < 60) {
                ++s_nLog;
                PRINT("[d3hack-plan] floor[%d] map=%d \"%s\" levelarea=%d label=\"%s\"%s",
                      nIdx, snoMap, (szMap != nullptr) ? szMap : "(unknown tileset)", snoArea,
                      szLabel, (szMap != nullptr && RiftMapIsBanned(snoMap)) ? "  BANNED" : "")
            }
        }
    };

    // d3hack-custom: READ-ONLY. Where does the map ENTER the world record?
    //
    // The floor-plan probe at 0x8174AC produced ZERO lines across a full GR load that
    // definitely picked a map (boneyards). So on this path the plan is never appended to, which
    // means the Lua roll is not what supplied the map -- or not at that moment.
    //
    // The live evidence points at the message instead. Both GR loads logged `arg2=1` at the
    // world factory, and w2 in {1,3} makes 0xD9530 SKIP worldgen. That is the
    // RevealWorldMessage handler at 0x2BD720, which passes the tileset in from the message as
    // CreateWorld's second stack argument. And 0x676BEC only runs the host-side pick if
    // rec+0xC is STILL -1 -- so a map arriving in the message suppresses the pick entirely.
    //
    // 0x676B68 `stp w1, w8, [x0]` is where both halves land: w1 -> rec+8 (the floor world,
    // x1_lr_level_NN) and w8 -> rec+0xC (the map). Reading them here answers the question
    // outright: if w8 is already the final tileset, the choice was made upstream of everything
    // examined so far and the plan is not on this path at all.
    HOOK_DEFINE_INLINE(RiftRecordInit) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (!global_config.rare_cheats.world_gen_probe)
                return;
            const s32   snoWorld = static_cast<s32>(ctx->W[1]);
            const s32   snoMap   = static_cast<s32>(ctx->W[8]);
            const int   nFloor   = LrFloorOf(snoWorld);
            const char *szMap    = RiftMapName(snoMap);
            if (nFloor <= 0 && szMap == nullptr)   // not a rift record, ignore
                return;
            static int s_nLog = 0;
            if (s_nLog < 40) {
                ++s_nLog;
                PRINT("[d3hack-rec] record init: world %d (floor %d) map=%d \"%s\"%s", snoWorld,
                      nFloor, snoMap, (szMap != nullptr) ? szMap : "-",
                      (snoMap == -1) ? "  (-1: host will pick)" : "  (came in PRE-SET)")
            }
        }
    };

    // d3hack-custom: READ-ONLY. Does the plan ever get consulted, or built?
    //
    // 0x816E98 `cbnz w9` -- w9 is the "plan loaded" flag at riftState+0x1C8, already loaded,
    // and w19 is the floor number from 0x6777C0. Zero means 0x816EE0 is about to build the
    // plan from Lua; non-zero means it was built earlier. If this hook never fires at all, then
    // 0x816E60 is not called on this path and the whole plan mechanism is a side road.
    // Set when the plan-loaded flag is seen as 0, i.e. a NEW plan is about to be built.
    // RiftPlanDump consumes it, so the dump happens once per RIFT rather than once per boot.
    // Each rift is a fresh sample of what the Lua engine can actually pick, and that pool is
    // not written down anywhere -- playing is the only way to enumerate it.
    inline bool s_bPlanDumpPending = false;

    // !! THIS HOOK ARMS THE SUBSTITUTION. It is not a diagnostic. !!
    //
    // It used to return early unless WorldGenProbe was on, which meant s_bPlanDumpPending was
    // never set, which meant RiftPlanDump did nothing -- so BannedRiftMaps was completely dead
    // for every user who did not also switch on a debug flag that is not even present in the
    // shipped config.toml and defaults to false in code. Bans resolved, the readiness line
    // printed, and not one floor was ever examined.
    //
    // This is the THIRD time this exact mistake has been made on this feature. The install site
    // below already carries a comment saying "gating it on the diagnostic meant turning
    // WorldGenProbe off silently disabled banning" -- the install was fixed and the trigger was
    // left behind. Only the verbose per-query line is a diagnostic; the flag is the feature.
    HOOK_DEFINE_INLINE(RiftPlanQuery) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (ctx->W[9] == 0)
                s_bPlanDumpPending = true;

            // Liveness. Without this, "no plan dump" cannot be told apart from "the hook never
            // installed" -- the ambiguity that made the bug above survive two rounds of looking
            // straight at it.
            {
                static int s_nSeen = 0;
                if (++s_nSeen == 1)
                    PRINT_LINE("[d3hack-plan] plan query hook is LIVE (0x816E98)");
            }

            if (!global_config.rare_cheats.world_gen_probe)
                return;
            static int s_nLog = 0;
            if (s_nLog < 40) {
                ++s_nLog;
                PRINT("[d3hack-plan] query floor %d: plan-loaded flag = %d%s",
                      static_cast<int>(ctx->W[19]), static_cast<int>(ctx->W[9]),
                      (ctx->W[9] == 0) ? "  (about to build it from Lua)" : "")
            }
        }
    };

    // d3hack-custom: READ-ONLY. Dump the whole rift floor plan once it exists.
    //
    // The append probe at 0x8174AC has now sat through several GR loads and fired ZERO times,
    // including one where the plan was demonstrably built:
    //
    //     [d3hack-rec]  record init: world 288454 (floor 1) map=-1  (-1: host will pick)
    //     [d3hack-plan] query floor 1: plan-loaded flag = 0  (about to build it from Lua)
    //     [d3hack-map]  floor 1 map: 453211 "p43_lr_tileset_interiorgrift_desert"
    //
    // So the host-side pick DOES run and the plan IS loaded -- but not through RiftPlanAppend.
    // The likely writer is the Lua binding at 0x819220, which hands out a RAW POINTER to plan
    // entry i and has zero BL callers; a Lua script writing through that pointer is invisible
    // to any `str [x,#0x1cc]` scan, which is exactly the caveat the static trace flagged.
    //
    // Chasing the writer is the wrong move. The plan is a plain struct and by this point it is
    // populated, so READ IT. 0x816EAC is the game's own read (`ldr w0,[x8,#0x16c]`) with
    // x8 = riftState + floor*0x60 and w19 = floor, so the base is one subtraction away.
    //
    // Read-side offsets are the write-side ones minus 0x60, i.e. floor N (1-based) is the entry
    // written at index N-1:
    //
    //     +0x16C  tileset World SNO      (write side +0x1CC)
    //     +0x17C  char[0x3F] label       (write side +0x1DC)
    //     +0x1BC  LevelArea SNO          (write side +0x21C)
    //
    // This is the world<->LevelArea pairing a correct substitution needs, and it exists nowhere
    // in the executable -- the candidate list is Lua bytecode in romfs. Dumped once per boot.
    //
    // Plain memory reads of a struct the game is about to read itself. No game calls, so the
    // thread does not matter -- the rule the first probe's crash established.
    // d3hack-custom: report substitution readiness at world entry, BEFORE a rift is opened.
    //
    // The refusal explanation below only prints once a rift has already been rolled and
    // refused. That is one wasted rift before the user learns anything, and if they are not
    // reading D3Debug.txt they learn nothing at all. This line lands at every world entry, so
    // the very first thing in the log after a boot says whether the config can work.
    inline void ReportSubstitutionReadiness() {
        if (!global_config.rare_cheats.rift_map_substitute)
            return;
        ResolveRiftBans();

        int nEligible = 0;
        int nKnown    = 0;
        int nPrefOk   = 0;
        for (const auto &tRow : kRiftMaps) {
            if (!RiftMapIsAllowed(tRow.sno) || RiftMapIsBanned(tRow.sno))
                continue;
            ++nEligible;
            if (MapFloatsFor(tRow.sno) == nullptr)
                continue;
            ++nKnown;
            if (RiftMapIsPreferred(tRow.sno))
                ++nPrefOk;
        }

        if (nEligible == 0) {
            PRINT_LINE("[d3hack-plan] SUBSTITUTION CANNOT WORK: every map is banned. Remove at "
                       "least one name from BannedRiftMaps.");
            return;
        }
        if (nKnown == 0) {
            PRINT("[d3hack-plan] SUBSTITUTION NOT READY: %d map(s) eligible, 0 with known "
                  "floats. Nothing can be substituted in until the game rolls one of them into "
                  "a Greater Rift plan. If your list is Nephalem-only tilesets it will never "
                  "happen -- see rift-maps.txt.", nEligible)
            return;
        }
        PRINT("[d3hack-plan] substitution ready: %d eligible, %d usable now (%d of them "
              "preferred), %d map(s) cached", nEligible, nKnown, nPrefOk, s_nMapFloats)
    }

    // d3hack-custom: say WHY a substitution refused, in terms the config owner can act on.
    //
    // A replacement is only ever drawn from maps whose three plan floats are already known,
    // and floats are learned from Greater Rift plans the game itself rolled. That makes three
    // distinct failure states, and the old single line could not tell them apart:
    //
    //   1. Nothing is eligible at all -- the ban list covers every map. Fix: unban something.
    //   2. Maps are eligible but none has known floats YET. Fix: play, or ship the seed file.
    //   3. Maps are eligible, but they are maps the GR engine never rolls, so their floats can
    //      never be learned from GR play. This is the one that never resolves on its own, and
    //      it is what a preference list full of Nephalem-only tilesets looks like.
    //
    // Printed once per rift, next to the plan dump, so it is always beside the evidence.
    inline void ExplainRefusal(int nRefused) {
        int nEligible = 0;
        int nKnown    = 0;
        for (const auto &tRow : kRiftMaps) {
            if (!RiftMapIsAllowed(tRow.sno) || RiftMapIsBanned(tRow.sno))
                continue;
            ++nEligible;
            if (MapFloatsFor(tRow.sno) != nullptr)
                ++nKnown;
        }

        if (nEligible == 0) {
            PRINT("[d3hack-plan] %d floor(s) NOT swapped: EVERY map is banned. BannedRiftMaps "
                  "covers all %d names, so there is nothing to substitute in. Unban at least "
                  "one map.", nRefused, static_cast<int>(sizeof(kRiftMaps) / sizeof(kRiftMaps[0])))
            return;
        }

        PRINT("[d3hack-plan] %d floor(s) NOT swapped. %d map(s) are eligible, %d of them have "
              "known floats. A map can only be substituted IN once the game has rolled it into "
              "a Greater Rift plan at least once.", nRefused, nEligible, nKnown)

        // Name them. Capped, because an empty AllowedRiftMaps with a short ban list leaves
        // well over a hundred and a log full of names helps nobody.
        int nShown = 0;
        for (const auto &tRow : kRiftMaps) {
            if (!RiftMapIsAllowed(tRow.sno) || RiftMapIsBanned(tRow.sno))
                continue;
            if (MapFloatsFor(tRow.sno) != nullptr)
                continue;
            if (nShown >= 20) {
                PRINT("[d3hack-plan]   ... and %d more", nEligible - nKnown - nShown)
                break;
            }
            ++nShown;
            PRINT("[d3hack-plan]   waiting on floats: %s", tRow.szName)
        }
    }

    HOOK_DEFINE_INLINE(RiftPlanDump) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            const bool bSwap = global_config.rare_cheats.rift_map_substitute;
            if (!global_config.rare_cheats.world_gen_probe && !bSwap)
                return;
            if (!s_bPlanDumpPending)
                return;

            ResolveRiftBans();
            // NO FILE I/O HERE. This runs on the world-generation thread, and a save-filesystem
            // commit through the emulator can block it for many seconds -- which presents as
            // "I can move but the world is frozen and terrain stops streaming, then it catches
            // up". Loading and saving both happen at sInitializeWorld now, via
            // MapFloatsLoadIfNeeded() / MapFloatsFlush(), where a pause is already expected.

            const int  nFloor = static_cast<int>(ctx->W[19]);
            const auto uCur   = static_cast<uintptr_t>(ctx->X[8]);
            if (nFloor < 1 || nFloor > 10)
                return;
            if (uCur < 0x1000000000ull || uCur >= 0x8000000000ull)
                return;

            const uintptr_t uBase = uCur - static_cast<uintptr_t>(nFloor) * 0x60ull;
            s_bPlanDumpPending    = false;

            // PASS 1 -- LEARN, before touching anything. Every float recorded here came out of
            // a plan the game built itself, so a substitution can only ever reuse the game's
            // own numbers. Nothing is invented.
            //
            // GREATER RIFT ENTRIES ONLY -- the same four-byte "_GR_" label test the swap uses.
            // 0x816E60 serves Nephalem rifts too, and a Nephalem plan's three floats are NOT
            // the same quantity: measured across a 60-map cache, every map also seen in a GR
            // carries f0 in 0.45..1.18 with f2 in {1,2,3}, while every map never seen in a GR
            // carries f0==f1==f2 in {2,3,4,6,8,10}. Learning both into one table let the GR
            // substitution hand a floor a Nephalem budget -- the precise failure this whole
            // design exists to prevent.
            for (int n = 1; n <= 10; ++n) {
                const uintptr_t uRow = uBase + static_cast<uintptr_t>(n) * 0x60ull;
                const s32       sno  = *reinterpret_cast<const s32 *>(uRow + 0x16C);
                if (sno <= 0)
                    continue;
                const auto *pLbl = reinterpret_cast<const char *>(uRow + 0x17C);
                if (!(pLbl[0] == '_' && pLbl[1] == 'G' && pLbl[2] == 'R' && pLbl[3] == '_'))
                    continue;
                NoteMapFloats(sno, reinterpret_cast<const float *>(uRow + 0x170));
            }

            // PASS 2 -- SUBSTITUTE, in the plan, before a single floor exists.
            //
            // This is the property every previous attempt lacked. 0x4BC450, 0x78866C and
            // 0x784EE4 all fire during or after floor construction, by which point the floor
            // has been budgeted from the ORIGINAL map. Here the rift has been rolled but
            // nothing has been built: the very next instruction is the game's own first read
            // of this table.
            //
            // The tileset AND its three floats move together, which is the whole point. If a
            // replacement's floats are not known yet the swap is REFUSED rather than guessed
            // -- leaving the original's floats behind is the suspected cause of four unleavable
            // floors, and repeating it silently would be the worst possible outcome.
            int nSwapped = 0;
            int nRefused = 0;
            if (bSwap) {
                for (int n = 1; n <= 10; ++n) {
                    const uintptr_t uRow   = uBase + static_cast<uintptr_t>(n) * 0x60ull;
                    const s32       snoOld = *reinterpret_cast<const s32 *>(uRow + 0x16C);
                    if (snoOld <= 0)
                        continue;
                    if (RiftMapIsAllowed(snoOld) && !RiftMapIsBanned(snoOld))
                        continue;

                    // GREATER RIFTS ONLY, and the plan says so itself.
                    //
                    // 0x816E60 serves Nephalem rifts too, and HANDOFF records that banning
                    // there produced floors that do not work. Every other rift feature in this
                    // file gates on TrueGRLevel() > 0 -- but that calls the game
                    // (TieredLootRunGetLevel) and this hook runs on the world-gen thread, which
                    // is exactly what killed the first world-factory probe.
                    //
                    // No game call is needed. Every populated GR plan entry carries a Lua label
                    // beginning "_GR_" -- observed across four rifts and 30+ entries
                    // (_GR_Angels03, _GR_Undead01, _GR_Sewers01, ...). Reading four bytes out
                    // of the struct is a data-only gate with no thread risk.
                    //
                    // Fails SAFE: an entry that is not recognisably a GR floor keeps whatever
                    // the game gave it.
                    const auto *pLbl = reinterpret_cast<const char *>(uRow + 0x17C);
                    if (!(pLbl[0] == '_' && pLbl[1] == 'G' && pLbl[2] == 'R' && pLbl[3] == '_'))
                        continue;

                    // Two passes: PREFERRED maps first, then anything eligible.
                    //
                    // Pass 0 considers only maps on PreferredRiftMaps -- the "I love the
                    // graveyard map" case. Pass 1 is the whole eligible set and catches the
                    // common configuration where no preference is set, or where none of the
                    // preferred maps have cached floats yet.
                    //
                    // Within a pass the choice spreads by floor index rather than always
                    // taking the first, otherwise every floor of the rift is the identical
                    // map. Deterministic -- no RNG in a worldgen hook -- but varied. With
                    // exactly one candidate every floor is that map, which is the correct
                    // answer to "I only want boneyards".
                    s32          snoNew = 0;
                    const float *pfNew  = nullptr;
                    for (int nPass = 0; nPass < 2 && snoNew == 0; ++nPass) {
                        const bool bPreferOnly = (nPass == 0);
                        if (bPreferOnly && s_nPreferMap == 0)
                            continue;   // no preference configured, go straight to pass 1

                        int nOk = 0;
                        for (int k = 0; k < s_nMapFloats; ++k) {
                            const s32 c = s_arMapFloats[k].sno;
                            if (!RiftMapIsAllowed(c) || RiftMapIsBanned(c))
                                continue;
                            if (bPreferOnly && !RiftMapIsPreferred(c))
                                continue;
                            ++nOk;
                        }
                        if (nOk == 0)
                            continue;

                        int nWant = n % nOk;
                        for (int k = 0; k < s_nMapFloats; ++k) {
                            const s32 snoCand = s_arMapFloats[k].sno;
                            if (!RiftMapIsAllowed(snoCand) || RiftMapIsBanned(snoCand))
                                continue;
                            if (bPreferOnly && !RiftMapIsPreferred(snoCand))
                                continue;
                            if (nWant-- > 0)
                                continue;
                            snoNew = snoCand;
                            pfNew  = s_arMapFloats[k].ar;
                            break;
                        }
                    }
                    if (snoNew == 0 || pfNew == nullptr) {
                        ++nRefused;
                        continue;
                    }

                    *reinterpret_cast<s32 *>(uRow + 0x16C) = snoNew;
                    for (int i = 0; i < 3; ++i)
                        *reinterpret_cast<float *>(uRow + 0x170 + i * 4) = pfNew[i];
                    ++nSwapped;
                    PRINT("[d3hack-plan] floor %2d SWAP %d \"%s\" -> %d \"%s\" (floats carried)",
                          n, snoOld, RiftMapName(snoOld), snoNew, RiftMapName(snoNew))
                }
                // A refusal used to print one vague line and nothing else, which is how a
                // user ends up reporting "the ban list does nothing" with no way to see why.
                // There are three genuinely different reasons and they need three different
                // fixes, so name which one this is and name the maps involved.
                if (nRefused > 0)
                    ExplainRefusal(nRefused);
            }

            // PASS 3 -- report what the game will actually run.
            int nUsed   = 0;
            int nBanned = 0;

            PRINT_LINE("[d3hack-plan] --- rift floor plan, 10 slots ---");
            for (int n = 1; n <= 10; ++n) {
                const uintptr_t uRow    = uBase + static_cast<uintptr_t>(n) * 0x60ull;
                const s32       snoMap  = *reinterpret_cast<const s32 *>(uRow + 0x16C);
                const s32       snoArea = *reinterpret_cast<const s32 *>(uRow + 0x1BC);

                // The label is a fixed 0x3F buffer filled by strncpy, which does not guarantee
                // a terminator. Copy it out and terminate it rather than trusting it.
                char        szLabel[64];
                const auto *pSrc = reinterpret_cast<const char *>(uRow + 0x17C);
                for (int k = 0; k < 63; ++k)
                    szLabel[k] = pSrc[k];
                szLabel[63] = 0;

                // Printed as milli-units because the log macro has no float formatting.
                int arMilli[3];
                for (int k = 0; k < 3; ++k) {
                    const float f = *reinterpret_cast<const float *>(uRow + 0x170 + k * 4);
                    arMilli[k]    = static_cast<int>(f * 1000.0f);
                }

                const char *szMap   = RiftMapName(snoMap);
                const bool  bReject = (snoMap > 0) && (!RiftMapIsAllowed(snoMap) ||
                                                       RiftMapIsBanned(snoMap));
                if (snoMap != -1) {
                    ++nUsed;
                    if (bReject)
                        ++nBanned;
                }
                PRINT("[d3hack-plan] floor %2d: map=%d \"%s\" levelarea=%d f=%d/%d/%d "
                      "label=\"%s\"%s",
                      n, snoMap, (szMap != nullptr) ? szMap : "-", snoArea, arMilli[0],
                      arMilli[1], arMilli[2], szLabel, bReject ? "  REJECTED" : "")
            }

            PRINT("[d3hack-plan] --- %d floors, %d still unwanted, %d swapped, %d refused "
                  "(known floats: %d maps) ---",
                  nUsed, nBanned, nSwapped, nRefused, s_nMapFloats)
        }
    };

    // d3hack-custom: WHO WRITES the gem-upgrade counters? The getter was the wrong layer.
    //
    // Hooking the READ at 0x243ABC changed the number on screen and nothing else: the log said
    // `max=3 bonus=2 -> bonus=7 (total 10)` and the player got exactly 5 -- which is stock
    // 3 + 2. So that function renders the count; it does not own it. The budget is decided and
    // STORED when the rift completes, and a getter downstream of the decision can only ever
    // change the label.
    //
    // That is the second wrong layer in a row on this feature. Both times the hook fired with
    // exactly the numbers expected, and both times that proved nothing. The write is what
    // matters, so log the writes.
    //
    // re/attrxref.py finds no SETs for 0x580/0x581/0x582, which means they are not written by a
    // movn-materialised key in native code -- so a script or a generic path does it. Hooking
    // the setter itself catches every writer regardless of how the key was built.
    // d3hack-custom: WHERE does a set bonus's weapon gate live?
    //
    // PowerFormulaProbe saw NOTHING from any DH set power even with the set equipped, so set
    // bonuses are not script formulas and that instrument can never see them. What they ARE is
    // attribute grants: the set record hands out ITEM_POWER_PASSIVE[power], and the power grants
    // a skill-damage multiplier keyed to the skill SNO.
    //
    //     0x4AC POWER_DAMAGE_PERCENT_BONUS
    //     0x4AD POWER_INSTANCE_DAMAGE_PERCENT_BONUS
    //     0x5A7 MULTIPLICATIVE_DAMAGE_PERCENT_BONUS
    //     0x5A8 MULTIPLICATIVE_DAMAGE_PERCENT_BONUS_FOR_SKILL   <- "Impale damage +75000%"
    //     0x5A9 MULTIPLICATIVE_DAMAGE_PERCENT_BONUS_FOR_PLAYER
    //
    // The KEY carries the skill SNO as its param, so the log says which skill each bonus is
    // for. Diff a run with a melee weapon against one with a bow: the bonus that disappears is
    // the gated one, and its caller is where the gate is decided.
    inline auto IsDamageBonusAttr(s32 nAttr) -> bool {
        // 0x50D ITEM_POWER_PASSIVE is the one that matters most: a set bonus IS
        // "ITEM_POWER_PASSIVE[powerSNO] = 1" (proven from the set-bonus records), so if the
        // melee gate works by simply not granting the power when a bow is equipped, this
        // attribute is where it shows -- keyed by the power SNO, which the log prints.
        return nAttr == 0x50D || nAttr == 0x4AC || nAttr == 0x4AD || nAttr == 0x5A7 ||
               nAttr == 0x5A8 || nAttr == 0x5A9;
    }

    inline constexpr int kDmgSeenMax = 48;
    inline u64           s_arDmgSeen[kDmgSeenMax] = {};
    inline int           s_nDmgSeen               = 0;

    inline void NoteDamageBonus(const char *szWhich, s32 nAttr, s64 nParam, double dVal,
                                u64 uFrom) {
        const u64 uKey = (static_cast<u64>(nAttr) << 48) ^
                         (static_cast<u64>(nParam) << 24) ^
                         static_cast<u64>(static_cast<s64>(dVal)) ^ (uFrom << 3);
        for (int k = 0; k < s_nDmgSeen; ++k)
            if (s_arDmgSeen[k] == uKey)
                return;
        if (s_nDmgSeen >= kDmgSeenMax)
            return;
        s_arDmgSeen[s_nDmgSeen++] = uKey;
        PRINT("[d3hack-dmg] %s 0x%03X skill=%lld val=%d.%03d  from +%llX", szWhich,
              static_cast<u32>(nAttr), static_cast<long long>(nParam),
              static_cast<int>(dVal),
              static_cast<int>((dVal < 0 ? -dVal : dVal) * 1000.0) % 1000,
              static_cast<unsigned long long>(uFrom))
    }

    // The write probes caught NOTHING for the damage attributes, so these values do not travel
    // through ACD_AttributesSetInt/Float. Watch the READ instead: whatever consumes the bonus
    // has to ask for it, and the ask carries the skill SNO in the key.
    HOOK_DEFINE_TRAMPOLINE(DamageBonusReadProbe) {
        static auto Callback(ActorCommonData *tACD, FastAttribKey tKey) -> int64 {
            const auto ret = Orig(tACD, tKey);
            if (!global_config.rare_cheats.damage_bonus_probe)
                return ret;
            const s32 nAttr = static_cast<s32>(KeyGetAttrib(tKey));
            if (!IsDamageBonusAttr(nAttr))
                return ret;
            NoteDamageBonus("GET ", nAttr, KeyGetParam(tKey),
                            static_cast<double>(static_cast<s32>(ret)),
                            reinterpret_cast<u64>(__builtin_return_address(0)) -
                                exl::util::modules::GetTargetStart());
            return ret;
        }
    };

    // The OTHER getter. There are exactly two, and ACD_AttributesGetInt caught nothing --
    // including ITEM_POWER_PASSIVE, which the item/power layer certainly reads. LobbyHooks
    // hooks THIS one (FastAttribGetValueInt) for its ITEM_EQUIPPED_BUT_DISABLED override, which
    // is the clue: item-side attributes are read through the FastAttribGroup, not the ACD.
    HOOK_DEFINE_TRAMPOLINE(DamageBonusFastRead) {
        static auto Callback(const FastAttribGroup *ptGroup, FastAttribKey tKey) -> int64 {
            const auto ret = Orig(ptGroup, tKey);
            if (!global_config.rare_cheats.damage_bonus_probe)
                return ret;
            const s32 nAttr = static_cast<s32>(KeyGetAttrib(tKey));
            // Proof of life: the hook is running even if the filter never matches.
            {
                static int s_nAll = 0;
                if ((++s_nAll % 20000) == 1 && s_nAll < 100000)
                    PRINT("[d3hack-dmg] FastAttrib getter alive: %d reads, latest attr 0x%03X",
                          s_nAll, static_cast<u32>(nAttr))
            }
            if (!IsDamageBonusAttr(nAttr))
                return ret;
            NoteDamageBonus("FGET", nAttr, KeyGetParam(tKey),
                            static_cast<double>(static_cast<s32>(ret)),
                            reinterpret_cast<u64>(__builtin_return_address(0)) -
                                exl::util::modules::GetTargetStart());
            return ret;
        }
    };

    // d3hack-custom: SET BONUS WITHOUT THE WEAPON REQUIREMENT.
    //
    // Measured, both weapons, one hit each:
    //
    //     0x50D  skill=318386  = 60.000   always -- melee AND bow
    //     0x5A8  skill=131366  = 61.000   melee   (1 + 60)
    //     0x5A8  skill=131366  =  1.000   bow     (the 60 is left out)
    //
    // So the 6000% passive is never revoked. The weapon check happens when the passives are
    // folded into MULTIPLICATIVE_DAMAGE_PERCENT_BONUS_FOR_SKILL for the skill, and with a bow
    // the contribution is simply omitted. 0x5A8 is read from +97B6AC / +1B303F0, which is
    // damage-calculation code -- not UI -- so re-adding it at the read reaches the damage.
    //
    // The passive VALUE is learned from the live 0x50D read rather than hardcoded as 60: the
    // number changes with set rank and future patches, and inventing it is how the gem-upgrade
    // work went wrong twice.
    //
    // Only ADDS, and only when the fold clearly excluded it (ret < 1 + passive). With a melee
    // weapon the value already includes it and nothing is touched.
    inline constexpr s32 kShadowGatedPower = 318386;   // the melee-gated 6000% passive
    inline constexpr s32 kShadowGatedSkill = 131366;   // Impale
    inline s32           s_nPendingSkill   = 0;

    // !! DO NOT PUT THIS ON THE ATTRIBUTE GETTER !!
    //
    // The first version hooked FastAttribGetValueFloat, which the liveness counter measured at
    // 20,000+ calls per second. A trampoline there runs on EVERY float attribute read in the
    // game and the user reported the result as "network lag while offline" -- frame stutter,
    // not networking. Under Ryujinx the per-call trampoline cost is far worse than it would be
    // on hardware.
    //
    // These two hooks sit on the SINGLE consumer instead, which runs once per damage
    // calculation:
    //
    //     0097B690  ldr w1, [x24, #0x10]    the skill SNO
    //     0097B694  mov w0, #0x5a8          <- HOOK A: capture w1
    //     0097B69C  bl  0x69AEC0            build key(attr, skill)
    //     0097B6A8  bl  0x46FAC0            ACD_AttributesGetFloat -> s0
    //     0097B6AC  add x1, x19, #0x35f     <- HOOK B: s0 still holds the value
    //     0097B6B4  mov v9.16b, v0.16b      the result is consumed here
    //
    // Both hooked instructions are INTEGER ops. HANDOFF records that an inline hook ON a float
    // instruction (an fcvt at 0x904C64) appeared to corrupt float registers globally, so the
    // hook points are deliberately chosen to avoid that, even though the context save/restore
    // still carries the float registers.
    //
    // The passive value is read on demand rather than cached from the getter: the key format is
    // (param << 12) | attr, verified against the observed 0xFFFFF580 for attr 0x580 param -1.
    inline auto MakeAttribKey(s32 nAttr, s32 nParam) -> FastAttribKey {
        FastAttribKey k {};
        k.nValue = (static_cast<s32>(static_cast<u32>(nParam) << 12)) | (nAttr & 0xFFF);
        return k;
    }

    HOOK_DEFINE_INLINE(SetBonusSkillCapture) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            s_nPendingSkill = static_cast<s32>(ctx->W[1]);
        }
    };

    HOOK_DEFINE_INLINE(SetBonusAnyWeapon) {
        static void Callback(exl::hook::InlineFloatCtx *ctx) {
            if (s_nPendingSkill != kShadowGatedSkill)
                return;
            auto *pACD = reinterpret_cast<ActorCommonData *>(ctx->X[27]);
            if (pACD == nullptr || ACD_AttributesGetFloat == nullptr)
                return;
            const float flPassive =
                ACD_AttributesGetFloat(pACD, MakeAttribKey(0x50D, kShadowGatedPower));
            if (flPassive <= 0.0f)
                return;                      // set not equipped -- nothing to re-add
            const float flCur = ctx->S[0];
            if (flCur >= (1.0f + flPassive))
                return;                      // melee weapon: already folded in
            ctx->S[0] = flCur + flPassive;
            static int s_nLog = 0;
            if (s_nLog < 4) {
                ++s_nLog;
                PRINT("[d3hack-set] weapon gate bypassed: skill %d  %d.%03d -> %d.%03d",
                      kShadowGatedSkill, static_cast<int>(flCur),
                      static_cast<int>(flCur * 1000.0f) % 1000, static_cast<int>(ctx->S[0]),
                      static_cast<int>(ctx->S[0] * 1000.0f) % 1000)
            }
        }
    };

    // THE FLOAT GETTERS -- the gap in the previous six probes.
    //
    // MULTIPLICATIVE_DAMAGE_PERCENT_BONUS_FOR_SKILL and friends are FLOAT attributes, and every
    // getter watched so far returned int64. LobbyHooks keeps a whole separate
    // FastAttribGetFloatValue hook for exactly this reason. Watching int getters for float
    // attributes is why 80k reads produced nothing: the traffic was real, the filter was on the
    // wrong pipe.
    HOOK_DEFINE_TRAMPOLINE(DamageBonusFastReadF) {
        static auto Callback(FastAttribGroup *ptGroup, FastAttribKey tKey) -> float {
            const auto ret = Orig(ptGroup, tKey);
            if (!global_config.rare_cheats.damage_bonus_probe)
                return ret;
            const s32 nAttr = static_cast<s32>(KeyGetAttrib(tKey));

            {
                static int s_nAllF = 0;
                if (global_config.rare_cheats.damage_bonus_probe &&
                    (++s_nAllF % 20000) == 1 && s_nAllF < 100000)
                    PRINT("[d3hack-dmg] FLOAT getter alive: %d reads, latest attr 0x%03X",
                          s_nAllF, static_cast<u32>(nAttr))
            }
            if (!IsDamageBonusAttr(nAttr))
                return ret;
            NoteDamageBonus("FGETF", nAttr, KeyGetParam(tKey), static_cast<double>(ret),
                            reinterpret_cast<u64>(__builtin_return_address(0)) -
                                exl::util::modules::GetTargetStart());
            return ret;
        }
    };

    HOOK_DEFINE_TRAMPOLINE(DamageBonusAcdReadF) {
        static auto Callback(ActorCommonData *tACD, FastAttribKey tKey) -> float {
            const auto ret = Orig(tACD, tKey);
            if (!global_config.rare_cheats.damage_bonus_probe)
                return ret;
            const s32 nAttr = static_cast<s32>(KeyGetAttrib(tKey));
            if (!IsDamageBonusAttr(nAttr))
                return ret;
            NoteDamageBonus("AGETF", nAttr, KeyGetParam(tKey), static_cast<double>(ret),
                            reinterpret_cast<u64>(__builtin_return_address(0)) -
                                exl::util::modules::GetTargetStart());
            return ret;
        }
    };

    HOOK_DEFINE_TRAMPOLINE(DamageBonusFloatProbe) {
        static void Callback(ActorCommonData *tACD, FastAttribKey tKey, float flValue) {
            Orig(tACD, tKey, flValue);
            if (!global_config.rare_cheats.damage_bonus_probe)
                return;
            const s32 nAttr = static_cast<s32>(KeyGetAttrib(tKey));
            if (!IsDamageBonusAttr(nAttr))
                return;
            NoteDamageBonus("SETF", nAttr, KeyGetParam(tKey), static_cast<double>(flValue),
                            reinterpret_cast<u64>(__builtin_return_address(0)) -
                                exl::util::modules::GetTargetStart());
        }
    };

    // The two call sites that GRANT the budget, found by logging every write with its caller:
    //
    //     +77BBEC  writes MAX   (0x581) = 3
    //     +77BC94  writes BONUS (0x582) = 2   <- only non-zero when the rift was empowered
    //     +77BCA4  writes USED  (0x580) = 0
    //
    // (Writes from +8774xx are zero-init on a fresh ACD and must NOT be touched -- forcing a
    // value there would grant upgrades outside a rift entirely.)
    //
    // Changing the value HERE changes what is stored, which is the number the game actually
    // spends against. Both earlier attempts modified reads downstream of this and moved only
    // the on-screen label.
    inline constexpr u64 kGemGrantMax   = 0x77BBEC;
    inline constexpr u64 kGemGrantBonus = 0x77BC94;
    inline s32           s_nGemMaxSeen  = 0;

    // d3hack-custom: TEMPORARY buff-stack probe. Gears of Dreadlands "Momentum".
    //
    // Goal: the GoD 6pc auto-fires your last primary while strafing, but those auto-fired
    // primaries do not grant Momentum stacks. Making them grant stacks is additive, whereas
    // stopping the decay runs into the native timed-event wall the shrine work already hit
    // (BUFF_ICON_END_TICK is never written through the attribute setter).
    //
    // Static analysis is exhausted: "Momentum" appears NOWHERE in .text/.rodata/.data (the
    // display name lives in romfs strings), and attrxref finds ZERO references to
    // BUFF_ICON_COUNT0..2 because the keys are computed as base+index rather than materialised
    // as constants. So the grant site can only be found by watching the writes.
    //
    // !! THIS HOOKS A HOT FUNCTION. It is a diagnostic, not a feature. Turn BuffStackProbe off
    // when the call site is known -- leaving a trampoline on the attribute setters is what
    // produced the world-freeze regression.
    //
    // Both setters are covered. Watching only the int path for a float attribute is what made
    // the Shadow's Mantle gate take seven probes; there are FOUR accessors, not two.
    inline auto IsBuffStackAttr(s32 nAttr) -> bool {
        // BUFF_ICON_COUNT0..11, and now the START/END tick ranges too.
        //
        // HANDOFF concluded "buff expiry is a native timed event, not an attribute" -- but that
        // was measured against the ACD setters ONLY, before the id-based setter at 0x69ED90 was
        // known. The end tick may well travel through that one. If it does, extending it keeps
        // the buff alive, which is what Momentum actually needs: a stack grant refreshes the
        // DURATION, and holding the count without holding the duration is exactly the shortcut
        // that failed.
        return (nAttr >= 0x2FF && nAttr <= 0x30A) ||   // BUFF_ICON_COUNT0..11
               (nAttr >= 0x252 && nAttr <= 0x261) ||   // BUFF_ICON_START_TICK0..15
               (nAttr >= 0x272 && nAttr <= 0x281);     // BUFF_ICON_END_TICK0..15
    }

    // 48 was far too small. Entering a world registers ~41 distinct buffs, each writing
    // count=1, which filled the table before any actual STACKING happened -- so the one event
    // this probe exists to catch was dropped. Exactly the budget mistake HANDOFF records for
    // the shard and area-id probes: the boring case fills the log first.
    inline constexpr int kBuffSeenMax = 256;
    inline u64           s_arBuffSeen[kBuffSeenMax] = {};
    inline int           s_nBuffSeen                = 0;

    // count==1 is buff REGISTRATION and there is a lot of it. count>=2 is a STACK, which is the
    // whole point, so registration gets a small quota and stacks are never rationed.
    inline int s_nBuffReg = 0;

    inline void NoteBuffStack(const char *szWhich, s32 nAttr, s64 nParam, int nVal, u64 uFrom) {
        if (nVal <= 1 && ++s_nBuffReg > 12)
            return;   // registration quota spent; stacks below are still logged
        const u64 uKey = (static_cast<u64>(nAttr) << 48) ^ (static_cast<u64>(nParam) << 20) ^
                         (static_cast<u64>(static_cast<u32>(nVal)) << 8) ^ uFrom;
        for (int k = 0; k < s_nBuffSeen; ++k)
            if (s_arBuffSeen[k] == uKey)
                return;
        if (s_nBuffSeen >= kBuffSeenMax)
            return;
        s_arBuffSeen[s_nBuffSeen++] = uKey;
        PRINT("[d3hack-buffstack] %s attr 0x%03X power=%lld count=%d  from +%llX", szWhich,
              static_cast<u32>(nAttr), static_cast<long long>(nParam), nVal,
              static_cast<unsigned long long>(uFrom))
    }

    // THE THIRD SETTER. Neither ACD setter carries buff stacks -- both hooks installed, the
    // liveness counter proved they ran, and BUFF_ICON_COUNT never appeared.
    //
    // attrxref.py has known about this one the whole time: its SETS list is
    // (0x69ED90, 0x46FB90, 0x46FAC0). 0x46FB90 is ACD_AttributesSetInt; 0x69ED90 is a THIRD
    // setter with 480 callers that is not in the symbol table at all. Its shape is
    // (w0 = actor id, x1 = key, w2 = value) -- id-based rather than ACD-pointer-based.
    //
    // Hooked INLINE at 0x69EDB8, where w0/w1/w2 are all still live. An inline hook commits to
    // no ABI: guessing a trampoline signature is what corrupted registers the last time a
    // function was hooked on inference rather than evidence.
    // Gears of Dreadlands Momentum, found by watching this setter:
    //
    //     SETID attr 0x309 power=484289 count=2..16      BUFF_ICON_COUNT10
    //
    // 484289 is P69_ItemPassive_Unique_Ring_010, the GoD 2-PIECE power -- not the 6pc, and not
    // a separate buff SNO. Stacks climb to 16 and then tick back down one at a time.
    //
    // Safe to hook permanently: the liveness counter measured this setter at well under 50,000
    // writes across a whole session, so it is nothing like the hot getters that caused the
    // world-freeze regression.
    inline constexpr s32 kMomentumPower = 484289;
    inline constexpr s32 kMomentumAttr  = 0x309;
    inline s32           s_nMomentumPeak = 0;
    inline s32           s_nMomentumNow  = 0;   // last value the game wrote

    // d3hack-custom: does the GoD auto-fired primary reach the Momentum grant path?
    //
    // The stack walk split grant from refresh cleanly. Frames [0]..[8] are shared buff/script
    // machinery; the grant-only chain is:
    //
    //     [13] 8565F0  in 0x856080   (one caller: 0x855DEC)
    //     [12] 852428  in 0x8522E0   (14 callers, 0x83xxxx-0x85xxxx -- an event dispatch)
    //     [11] 9A5E64  in 0x9A4400
    //     [10] 992D68  in 0x992C50
    //     [ 9] 9B1C80  in 0x9B1AF0
    //
    // versus the periodic refresh, which diverges from [9] onward.
    //
    // Logging entry to the two OUTER grant frames says where the auto-fire stops:
    //   both fire on manual and auto  -> divergence is deeper, below 0x8522E0
    //   only manual reaches 0x856080  -> the auto-fire never enters the grant path at all
    // Cold: per hit, not per attribute write.
    // !! __builtin_return_address(0) IS USELESS IN AN INLINE HOOK !!
    //
    // It returns the exlaunch stub, not the game's caller -- which is why every inline probe
    // reported the same "+1AA6104". It works in a TRAMPOLINE callback (that is why the
    // engage-event hunt could use it) and nowhere else. Use WalkStack(ctx->X[29]) instead: it
    // reads real frames, and it is what separated grant from decay in the first place.
    // d3hack-custom: dump 0x856080's ARGUMENTS, and whether this call reached the grant.
    //
    // The function is entered on every hit but only sometimes calls 0x8522E0 (250 entries vs
    // ~25 dispatches while Momentum drained), so the discriminator is in its inputs. Reading
    // the whole 1392-byte function cold is possible but slow; diffing the arguments between a
    // manual primary and an auto-fired one names the differing field directly.
    //
    // w27 = [x1] masked to 16 bits is a HANDLE, not a power SNO, so the interesting value is
    // elsewhere in the argument set -- hence dumping all of them rather than guessing which.
    //
    // Deduped on the argument signature so a burst of identical hits prints once.
    // d3hack-custom: GO AT THE AUTO-FIRE DIRECTLY.
    //
    // Established, not assumed:
    //   - 0x856080 is entered per power use, with w3 = the power SNO
    //         77552  DemonHunter_Bolas   (manual)  -> Momentum grants
    //         134030 DemonHunter_Strafe  (strafing) -> no grant
    //   - the auto-fired Bolas NEVER enters 0x856080; the entries seen while strafing are
    //     Strafe's own ticks. That is why no stack is granted.
    //   - every other argument is IDENTICAL between the two:
    //         w0=E610E880 [x1]=7857009A w2=7BB98688 w4=FFFFFFFF w6=0 w7=0
    //
    // So re-invoke 0x856080 with the primary's SNO and the arguments already in registers.
    // Throttled to every Nth Strafe tick, and only while the buff is actually up -- the 6pc
    // auto-fire itself only works when you already have stacks, so this matches the mechanic
    // rather than granting from nothing.
    //
    // A re-entry guard is essential: the injected call re-enters this same hook, and without
    // it that is infinite recursion. It only injects on Strafe, so the injected Bolas call
    // cannot inject again -- the guard is belt and braces.
    inline constexpr s32 kStrafeSno = 134030;
    inline s32           s_nLastPrimary = 0;

    // Learned from the game's own writes -- never invented. The previous duration attempt
    // ejected the player to town because it added ~1620 ticks per refresh, compounding, so the
    // end tick ran far outside any value the game produces. This cancels the drain exactly:
    // one stack's worth of time per suppressed decay, derived as baseDuration / maxStacks
    // (540 / 20 = 27 ticks), so the remaining lifetime stays where it was instead of growing.
    inline s32 s_nMomEndAttr = 0;   // which BUFF_ICON_END_TICK slot Momentum uses
    inline s32 s_nMomEndVal  = 0;   // the last end tick the GAME wrote
    inline s32 s_nMomBaseDur = 0;   // first observed end-start, latched
    inline s32  s_nMomMaxSeen = 0;   // highest stack count observed
    inline bool s_bMomWantRefresh = false;
    inline u32  s_uMomActorId     = 0;   // captured where it is VALID

    // d3hack-custom: WHICH powers actually reach the grant block?
    //
    // 0x8565CC is the block that ends in `bl 0x8522E0`. Reaching it means the grant path runs.
    // While strafing, 0x856080 was entered ~250 times but 0x8522E0 only ~25 -- so most Strafe
    // ticks are routed away before here, and neither injecting a call nor relabelling w3
    // changed that. w23 holds the original w3 (the power SNO) by this point.
    //
    // This says whether Strafe EVER reaches the grant block. If it does and still grants
    // nothing, the rejection is inside 0x8522E0 or the script below it. If it never does, the
    // routing decision upstream is the target and this narrows where to read.
    HOOK_DEFINE_INLINE(MomentumGrantBlock) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (global_config.rare_cheats.momentum_autofire_every <= 0)
                return;
            const s32  nPow = static_cast<s32>(ctx->W[23]);
            static s32 s_ar[12] = {};
            static int s_n = 0;
            for (int k = 0; k < s_n; ++k)
                if (s_ar[k] == nPow)
                    return;
            if (s_n >= 12)
                return;
            s_ar[s_n++] = nPow;
            PRINT("[d3hack-mblock] grant block reached with power %d (momentum=%d)", nPow,
                  s_nMomentumNow)
        }
    };

    // d3hack-custom: keep Momentum alive while strafing, using the game's OWN re-arm.
    //
    // BUFF_ICON_END_TICK (0x27C) is a DISPLAY MIRROR, not the authority. Expiry is a scheduled
    // timed event whose callback is 0x98F900, and every buff schedules exactly that one. Moving
    // the mirror out of range is what ejected the player to town; it never touched the timer.
    //
    // Inside the callback:
    //     0098F9A0  madd x8, x25, x8, x24    buff = x24 + x25*0x2E0
    //     0098F9A4  ldr  w8, [x8, #0x84]     the "infinite duration" flag
    //     0098F9A8  cbz  w8, 0x98F9D8        0 -> expire and remove      <-- HOOKED
    //     0098F9B4  ldr  w2, [x20, #0x70]    else re-arm for a full stock duration
    //     0098F9CC  bl   0x81B810            ...using the same callback, forever
    //
    // buff+0x84 is set at apply from bit 14 of buffDef+0x258, which the game sets when a buff
    // has zero duration. So "infinite" is a supported state and the expiry path already handles
    // it. Forcing w8 at the branch takes that path for one expiry without modifying the buff,
    // so nothing is left set afterwards -- important, because while +0x84 is set the game's own
    // RefreshBuffDuration (0x98E190) early-returns and the display stops updating.
    //
    // Why this cannot crash the way the previous attempts did: it issues NO calls. The reschedule
    // is the game's own code, on the game thread, at the moment the buff system is already
    // executing for this exact buff. 0x69ED90 -- which resolves actors with no bounds check, and
    // is the reason every external write crashed -- is never reached.
    inline bool s_bMomStrafing = false;

    // d3hack-custom: suppress Momentum's PERIODIC Lua effect while strafing.
    //
    // The decrement is not a native write and not the expiry -- it is a periodic script effect.
    // Periodic events use a DIFFERENT scheduler from expiry: 0x9E1110 ScheduleRepeatingEvent
    // (callback in x2), not 0x81B810 (callback in x4), which is why an earlier sweep for buff
    // timers only ever found expiry. Its callback is 0x98F250, and Lua is entered at exactly
    // one instruction:
    //
    //     0098F440  madd x28, x25, x8, x24     buff = x24 + x25*0x2E0
    //     0098F444  ldr  w9, [x28, #0x74]!     interval (pre-index: x28 becomes buff+0x74)
    //     0098F448  ldur x8, [x28, #0x1c]      x8 = buff+0x90 = the Lua thunk
    //     0098F464  blr  x8                    <-- HOOKED: Lua runs here
    //
    // Rather than skipping the instruction or rewriting control flow, x8 is pointed at a bare
    // `ret` (0x24397C). The call happens, returns immediately, and everything after it -- the
    // lockout attribute write and the reschedule -- runs untouched. No branch surgery, no
    // external calls, and nothing left modified on the buff.
    //
    // Both mirrors are now understood: BUFF_ICON_END_TICK mirrors a scheduled timed event, and
    // BUFF_ICON_COUNT mirrors a count the script keeps. Clamping either only moved the display,
    // which is why the count froze at 4 on screen while the buff died underneath.
    inline constexpr uintptr_t kBareRet = 0x24397C;

    // d3hack-custom: STRETCH the periodic interval rather than suppressing the tick.
    //
    // Suppressing the Lua call at 0x98F464 stopped the decay but also removed the movement
    // speed -- the periodic effect evidently applies the speed bonus as well as decrementing.
    // Killing the call kills both.
    //
    // The reschedule reads the interval from buff+0x74 and tail-calls the repeating scheduler:
    //     0098F4E0  ldr  w1, [x28]        x28 = buff+0x74, w1 = interval ticks
    //     0098F4E4  cbz  w1, ...          0 would stop the loop entirely   <-- HOOKED
    //     0098F514  b    0x9E1110         re-arm with x2 = 0x98F250
    //
    // Multiplying w1 in place is the same shape as the gem-grant fix: edit an argument of a
    // call the game is already making. The tick still runs -- speed still applies, one stack
    // still comes off -- just far less often, so stacks last proportionally longer.
    //
    // w1 is never set to 0, because 0 means "stop rescheduling" and the loop would not restart.
    HOOK_DEFINE_INLINE(MomentumSlowTick) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            const int nPct = global_config.rare_cheats.momentum_duration_pct;
            if (nPct <= 100 || !s_bMomStrafing)
                return;
            const s32 nCur = static_cast<s32>(ctx->W[1]);
            if (nCur <= 0)
                return;   // already stopping -- do not resurrect it
            const auto uBuff = static_cast<uintptr_t>(ctx->X[28]) - 0x74ull;
            if (uBuff < 0x1000000000ull || uBuff >= 0x8000000000ull)
                return;
            if (*reinterpret_cast<const s32 *>(uBuff + 0x20) != kMomentumPower)
                return;
            s64 nNew = (static_cast<s64>(nCur) * nPct) / 100;
            if (nNew > 0x7FFFFF)
                nNew = 0x7FFFFF;   // keep it a sane tick count
            ctx->W[1] = static_cast<u64>(static_cast<u32>(nNew));
            static int s_nLog = 0;
            if (s_nLog < 6) {
                ++s_nLog;
                PRINT("[d3hack-momentum] periodic interval %d -> %d ticks (momentum=%d)", nCur,
                      static_cast<int>(nNew), s_nMomentumNow)
            }
        }
    };

    HOOK_DEFINE_INLINE(MomentumNoTick) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (!global_config.rare_cheats.momentum_no_decay || !s_bMomStrafing)
                return;
            const auto uBuff = static_cast<uintptr_t>(ctx->X[24]) +
                               static_cast<uintptr_t>(ctx->X[25]) * 0x2E0ull;
            if (uBuff < 0x1000000000ull || uBuff >= 0x8000000000ull)
                return;
            if (*reinterpret_cast<const s32 *>(uBuff + 0x20) != kMomentumPower)
                return;
            ctx->X[8] = static_cast<u64>(GameOffset(kBareRet));
            static int s_nLog = 0;
            if (s_nLog < 6) {
                ++s_nLog;
                PRINT("[d3hack-momentum] periodic Lua tick suppressed (momentum=%d)",
                      s_nMomentumNow)
            }
        }
    };

    HOOK_DEFINE_INLINE(MomentumKeepAlive) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (!global_config.rare_cheats.momentum_no_decay)
                return;
            if (!s_bMomStrafing)
                return;
            if (ctx->W[8] != 0)
                return;   // already infinite -- leave it alone
            const auto uBuff = static_cast<uintptr_t>(ctx->X[24]) +
                               static_cast<uintptr_t>(ctx->X[25]) * 0x2E0ull;
            if (uBuff < 0x1000000000ull || uBuff >= 0x8000000000ull)
                return;
            if (*reinterpret_cast<const s32 *>(uBuff + 0x20) != kMomentumPower)
                return;
            ctx->W[8] = 1;   // take the game's own re-arm branch
            static int s_nLog = 0;
            if (s_nLog < 6) {
                ++s_nLog;
                PRINT("[d3hack-momentum] expiry re-armed while strafing (momentum=%d)",
                      s_nMomentumNow)
            }
        }
    };

    HOOK_DEFINE_INLINE(MomentumAutoFire) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            const int nEvery = global_config.rare_cheats.momentum_autofire_every;
            static bool s_bIn = false;
            if (s_bIn)
                return;

            const s32 nPower = static_cast<s32>(ctx->W[3]);
            if (nEvery <= 0 && !global_config.rare_cheats.momentum_no_decay)
                return;
            s_bMomStrafing = (nPower == kStrafeSno);
            if (nPower != kStrafeSno) {
                // Remember the primary actually being used. Skip the object-interaction
                // powers (Axe_Operate_Gizmo / _NPC) that also come through here.
                if (nPower > 0 && nPower != 30021 && nPower != 30022)
                    s_nLastPrimary = nPower;
                return;
            }
            // THE END-TICK REFRESH IS REMOVED. Calling 0x69ED90 externally crashes, in
            // every context tried: from inside the setter itself (re-entrant, half-built
            // frame), and from an unrelated hook with a correctly captured actor id. The last
            // attempt logged "held at 20" and then died on the call. Writing the value in place
            // when the GAME writes it is safe; issuing our own write to that setter is not.
            //
            // What remains works and has never crashed: the decay is suppressed, so stacks hold
            // at their high-water mark. The buff still expires on its own timer, which is the
            // part that cannot be reached from out here.
            (void) s_bMomWantRefresh;

            if (s_nLastPrimary == 0 || s_nMomentumNow <= 0) {
                // Say WHY nothing happened. A silent gate is indistinguishable from a hook
                // that never installed -- which is exactly how this went unnoticed once.
                static int s_nWhy = 0;
                if (s_nWhy < 3) {
                    ++s_nWhy;
                    PRINT("[d3hack-momentum] strafe tick skipped: lastPrimary=%d momentum=%d",
                          s_nLastPrimary, s_nMomentumNow)
                }
                return;
            }

            if (nEvery <= 0)
                return;
            static int s_nTick = 0;
            if ((++s_nTick % nEvery) != 0)
                return;

            // RELABEL the real Strafe hit instead of injecting a synthetic call.
            //
            // Injecting 0x856080 with the primary's SNO ran, but granted nothing: the count
            // fell 16,15,14,13,12,11 straight through re-fires #5..#9. The build pattern says
            // why -- stacks arrive +1,+3,+4,+4,+4, i.e. ONE PER ENEMY HIT, not one per cast.
            // A synthetic call carries a Strafe tick's arguments and connects with nothing, so
            // there is no hit and no grant.
            //
            // The Strafe tick itself IS a real hit on real targets. Presenting it as the
            // primary's power SNO means the on-hit path sees a primary hit that actually
            // landed. Only every Nth tick is relabelled, so most hits stay Strafe.
            //
            // KNOWN RISK: that tick's damage and procs may be attributed to the primary rather
            // than Strafe. Watch for damage numbers changing character while strafing; if they
            // do, this trade is not worth it and MomentumAutoFireEvery = 0 reverts it.
            ctx->W[3] = static_cast<u64>(static_cast<u32>(s_nLastPrimary));
            (void) s_bIn;

            static int s_nLog = 0;
            if (s_nLog < 40) {
                ++s_nLog;
                PRINT("[d3hack-momentum] relabel #%d strafe tick as power %d (momentum=%d)",
                      s_nLog, s_nLastPrimary, s_nMomentumNow)
            }
        }
    };

    HOOK_DEFINE_INLINE(MomentumGrantArgs) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (!global_config.rare_cheats.buff_stack_probe)
                return;
            const u32 a0 = static_cast<u32>(ctx->W[0]);
            const u32 a2 = static_cast<u32>(ctx->W[2]);
            const u32 a3 = static_cast<u32>(ctx->W[3]);
            const u32 a4 = static_cast<u32>(ctx->W[4]);
            const u32 a6 = static_cast<u32>(ctx->W[6]);
            const u32 a7 = static_cast<u32>(ctx->W[7]);
            u32       id = 0;
            const auto ux1 = static_cast<uintptr_t>(ctx->X[1]);
            if (ux1 >= 0x1000000000ull && ux1 < 0x8000000000ull)
                id = *reinterpret_cast<const u32 *>(ux1);
            const u64 h = (static_cast<u64>(a0) << 32) ^ (static_cast<u64>(a2) << 24) ^
                          (static_cast<u64>(a3) << 16) ^ (static_cast<u64>(a4) << 8) ^
                          (static_cast<u64>(a6) << 4) ^ a7;
            static u64 s_ar[20] = {};
            static int s_n = 0;
            for (int k = 0; k < s_n; ++k)
                if (s_ar[k] == h)
                    return;
            if (s_n >= 20)
                return;
            s_ar[s_n++] = h;
            PRINT("[d3hack-margs] w0=%08X [x1]=%08X w2=%08X w3=%08X w4=%08X w6=%08X w7=%08X "
                  "momentum=%d",
                  a0, id, a2, a3, a4, a6, a7, s_nMomentumNow)
        }
    };

    HOOK_DEFINE_INLINE(MomentumGrantOuter) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (!global_config.rare_cheats.buff_stack_probe)
                return;
            WalkStackOnce("856080 entry", static_cast<uintptr_t>(ctx->X[29]));
            // Chains cannot say WHICH phase they came from, so count instead. Strafe-only for
            // ten seconds and compare: if this climbs while Momentum does not, the auto-fired
            // primary DOES dispatch and the script declines to grant. If it does not climb, the
            // auto-fire never reaches the hit path at all. Those need different fixes.
            {
                static int s_n = 0;
                if ((++s_n % 25) == 0)
                    PRINT("[d3hack-mgrant] 856080 entries: %d   momentum=%d", s_n,
                          s_nMomentumNow)
            }
        }
    };

    HOOK_DEFINE_INLINE(MomentumGrantDispatch) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (!global_config.rare_cheats.buff_stack_probe)
                return;
            WalkStackOnce("8522E0 dispatch", static_cast<uintptr_t>(ctx->X[29]));
            {
                static int s_n = 0;
                if ((++s_n % 25) == 0)
                    PRINT("[d3hack-mgrant] 8522E0 dispatches: %d   momentum=%d", s_n,
                          s_nMomentumNow)
            }
        }
    };

    HOOK_DEFINE_INLINE(BuffStackProbeId) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            const s32 nAttrEarly = static_cast<s32>(ctx->W[1]) & 0xFFF;

            // Track the live Momentum count UNCONDITIONALLY. It was previously assigned inside
            // the momentum_no_decay branch, so with that feature off the counter stayed 0 and
            // every other feature that reads it silently did nothing. Shared state must not
            // live behind one feature's flag -- that is now three bugs of this exact shape in
            // this feature alone.
            if (nAttrEarly == kMomentumAttr) {
                const s64 nPm = static_cast<s64>(static_cast<s32>(ctx->W[1])) >> 12;
                if (nPm == kMomentumPower) {
                    // w0 here is THIS function's actor id. At the Strafe hook w0 is a different
                    // function's first argument entirely -- passing that as an actor id is what
                    // crashed on the first strafe tick. Capture it where it is valid.
                    s_uMomActorId   = static_cast<u32>(ctx->W[0]);
                    const s32 nPrev = s_nMomentumNow;
                    s_nMomentumNow  = static_cast<s32>(ctx->W[2]);
                    if (s_nMomentumNow > s_nMomMaxSeen)
                        s_nMomMaxSeen = s_nMomentumNow;
                    // Trajectory, not a snapshot. Six "momentum=20" lines right after building
                    // to 20 manually proved nothing about whether the re-fire actually grants.
                    if (global_config.rare_cheats.momentum_autofire_every > 0) {
                        static int s_nT = 0;
                        if (s_nT < 60 && s_nMomentumNow != nPrev) {
                            ++s_nT;
                            PRINT("[d3hack-mtrack] %d -> %d", nPrev, s_nMomentumNow)
                        }
                    }
                }
            }

            // Record the end-tick slot and the last value the GAME wrote, plus the base
            // duration. Everything the refresh uses comes from these.
            {
                const s64 nPe = static_cast<s64>(static_cast<s32>(ctx->W[1])) >> 12;
                if (nPe == kMomentumPower) {
                    static s32 s_nStartSeen = 0;
                    if (nAttrEarly >= 0x252 && nAttrEarly <= 0x261) {
                        s_nStartSeen = static_cast<s32>(ctx->W[2]);
                    } else if (nAttrEarly >= 0x272 && nAttrEarly <= 0x281) {
                        s_nMomEndAttr = nAttrEarly;
                        s_nMomEndVal  = static_cast<s32>(ctx->W[2]);
                        if (s_nMomBaseDur == 0 && s_nStartSeen > 0 &&
                            (s_nMomEndVal - s_nStartSeen) > 0)
                            s_nMomBaseDur = s_nMomEndVal - s_nStartSeen;
                    }
                }
            }

            // d3hack-custom: LENGTHEN the Momentum buff instead of freezing its count.
            //
            // Measured on the id setter, letting stacks run out untouched:
            //
            //     attr 0x25C power=484289 = 719     BUFF_ICON_START_TICK10
            //     attr 0x27C power=484289 = 1259    BUFF_ICON_END_TICK10
            //
            // 540 ticks of life. **The end tick IS writable through this setter**, which
            // overturns the note that buff expiry is a native timed event -- that was measured
            // against the ACD setters only, before 0x69ED90 was known.
            //
            // Stacks drain because the TIMER drains; a primary hit both adds a stack and
            // refreshes this tick. So extending it is the honest fix, where holding the count
            // (MomentumNoDecay) only masked the number while the timer expired underneath and
            // dropped it to 0 with no warning.
            //
            // The slot index is not hardcoded: any START/END tick attribute carrying the
            // Momentum power is accepted, so a different buff slot still works.
            if (global_config.rare_cheats.momentum_duration_pct > 100) {
                const s64 nP = static_cast<s64>(static_cast<s32>(ctx->W[1])) >> 12;
                if (nP == kMomentumPower) {
                    // START_TICK is written ONCE when the buff is applied; END_TICK is
                    // rewritten on every refresh. So `end - start` grows against a stale
                    // baseline (540, 720, 960, 1200 ...) and multiplying it compounds. Latch
                    // the FIRST duration as the base and add a constant extension instead.
                    static s32 s_nStart = 0;
                    static s32 s_nBaseDur = 0;
                    if (nAttrEarly >= 0x252 && nAttrEarly <= 0x261) {
                        s_nStart = static_cast<s32>(ctx->W[2]);
                    } else if (nAttrEarly >= 0x272 && nAttrEarly <= 0x281 && s_nStart > 0) {
                        const s32 nEnd = static_cast<s32>(ctx->W[2]);
                        if (s_nBaseDur == 0 && (nEnd - s_nStart) > 0)
                            s_nBaseDur = nEnd - s_nStart;   // 540 ticks, latched once
                        const s32 nDur = s_nBaseDur;
                        if (nDur > 0) {
                            const s64 nNew =
                                static_cast<s64>(nEnd) +
                                (static_cast<s64>(nDur) *
                                 (global_config.rare_cheats.momentum_duration_pct - 100)) / 100;
                            ctx->W[2] = static_cast<u64>(static_cast<u32>(nNew));
                            static int s_nLog = 0;
                            if (s_nLog < 4) {
                                ++s_nLog;
                                PRINT("[d3hack-momentum] base %d ticks, end %d -> %d (+%d)",
                                      nDur, nEnd, static_cast<int>(nNew),
                                      static_cast<int>(nNew) - nEnd)
                            }
                        }
                    }
                }
            }

            // d3hack-custom: hold Momentum at its high-water mark.
            //
            // Building still works normally and a value of 0 still clears it -- that is the
            // buff genuinely ending, and blocking it would strand stacks forever. Only the
            // downward tick while the buff is ALIVE is suppressed.
            //
            // This is deliberately not "make the auto-fired primary grant a stack". Every write
            // here arrives through one generic wrapper (+1AA6104), so grant and decay are
            // indistinguishable by caller, and finding the auto-fire path would be another
            // multi-probe hunt. Holding the peak achieves the same thing for a strafe build.
            if (global_config.rare_cheats.momentum_no_decay && nAttrEarly == kMomentumAttr) {
                const s64 nP = static_cast<s64>(static_cast<s32>(ctx->W[1])) >> 12;
                if (nP == kMomentumPower) {
                    const s32 nVal = static_cast<s32>(ctx->W[2]);
                    s_nMomentumNow = nVal;
                    if (nVal <= 0) {
                        s_nMomentumPeak = 0;          // buff ended -- let it clear
                    } else if (nVal >= s_nMomentumPeak) {
                        s_nMomentumPeak = nVal;       // building
                    } else {
                        ctx->W[2] = static_cast<u64>(static_cast<u32>(s_nMomentumPeak));
                        // NO WRITE HERE. This hook sits INSIDE 0x69ED90, partway through its
                        // prologue; calling that same function from here re-enters it with a
                        // half-built frame and crashed the game. A re-entry guard prevents
                        // recursion, not corruption. The refresh is issued from the Strafe-tick
                        // hook instead, which is an unrelated context.
                        s_bMomWantRefresh = true;
                        static int s_nLog = 0;
                        if (s_nLog < 4) {
                            ++s_nLog;
                            PRINT("[d3hack-momentum] held at %d (game wanted %d)",
                                  s_nMomentumPeak, nVal)
                        }
                    }
                }
            }

            // d3hack-custom: WHO grants a Momentum stack?
            //
            // Every write to 0x309 arrives through one generic wrapper (+1AA6104), so grant and
            // decay are indistinguishable by immediate caller. Walk the frame chain instead --
            // the same technique that cracked the floor-plan chain at 0x785950.
            //
            // A stack GRANT is an increase; a decay tick is a decrease. Walking both and
            // diffing the chains names the code that decides to grant, which is the thing the
            // auto-fired primary needs to trip.
            if (global_config.rare_cheats.buff_stack_probe && nAttrEarly == kMomentumAttr) {
                const s64 nP = static_cast<s64>(static_cast<s32>(ctx->W[1])) >> 12;
                if (nP == kMomentumPower) {
                    const s32  nVal = static_cast<s32>(ctx->W[2]);
                    s_nMomentumNow = nVal;
                    static s32 s_nLast = 0;
                    static int s_nUp = 0, s_nDown = 0;
                    const bool bUp = (nVal > s_nLast);
                    // Two of each is plenty and keeps the log readable; a burst of decay ticks
                    // must not crowd out the grant, which is the case that matters.
                    if (nVal > 0 && ((bUp && s_nUp < 2) || (!bUp && s_nDown < 2))) {
                        if (bUp) ++s_nUp; else ++s_nDown;
                        char szWhat[48];
                        ::snprintf(szWhat, sizeof(szWhat), "momentum %s %d -> %d",
                                   bUp ? "GRANT" : "decay", s_nLast, nVal);
                        WalkStack(szWhat, static_cast<uintptr_t>(ctx->X[29]));
                    }
                    s_nLast = nVal;
                }
            }

            if (!global_config.rare_cheats.buff_stack_probe)
                return;
            const s32 nAttr = nAttrEarly;
            {
                static int s_nAll = 0;
                if ((++s_nAll % 50000) == 1 && s_nAll < 200000)
                    PRINT("[d3hack-buffstack] id setter alive: %d writes, latest attr 0x%03X",
                          s_nAll, static_cast<u32>(nAttr))
            }
            if (!IsBuffStackAttr(nAttr))
                return;
            const s64 nParam = static_cast<s64>(static_cast<s32>(ctx->W[1])) >> 12;
            NoteBuffStack("SETID", nAttr, nParam, static_cast<int>(ctx->W[2]),
                          reinterpret_cast<u64>(__builtin_return_address(0)) -
                              exl::util::modules::GetTargetStart());
        }
    };

    HOOK_DEFINE_TRAMPOLINE(BuffStackProbeInt) {
        static void Callback(ActorCommonData *tACD, FastAttribKey tKey, s32 nValue) {
            Orig(tACD, tKey, nValue);
            if (!global_config.rare_cheats.buff_stack_probe)
                return;
            const s32 nAttr = static_cast<s32>(KeyGetAttrib(tKey));
            {
                static int s_nAll = 0;
                if ((++s_nAll % 50000) == 1 && s_nAll < 200000)
                    PRINT("[d3hack-buffstack] int setter alive: %d writes", s_nAll)
            }
            if (!IsBuffStackAttr(nAttr))
                return;
            NoteBuffStack("SETI", nAttr, KeyGetParam(tKey), nValue,
                          reinterpret_cast<u64>(__builtin_return_address(0)) -
                              exl::util::modules::GetTargetStart());
        }
    };

    HOOK_DEFINE_TRAMPOLINE(BuffStackProbeFloat) {
        static void Callback(ActorCommonData *tACD, FastAttribKey tKey, float flValue) {
            Orig(tACD, tKey, flValue);
            if (!global_config.rare_cheats.buff_stack_probe)
                return;
            const s32 nAttr = static_cast<s32>(KeyGetAttrib(tKey));
            if (!IsBuffStackAttr(nAttr))
                return;
            NoteBuffStack("SETF", nAttr, KeyGetParam(tKey), static_cast<int>(flValue),
                          reinterpret_cast<u64>(__builtin_return_address(0)) -
                              exl::util::modules::GetTargetStart());
        }
    };

    // The FEATURE lives on two cold inline hooks, NOT on this trampoline.
    //
    // ACD_AttributesSetInt carries every attribute write in the game. Running the empowered-gem
    // logic here cost a trampoline per write and, with the float-getter hook, was reported as
    // "network lag while offline" -- frame stutter. These two fire once per rift completion:
    //
    //     0077BBE0  sub x1, x8, #5      key 0x581 MAX,   w2 = the value   <- capture
    //     0077BC80  cinc w2, w25, ne    the bonus value
    //     0077BC84  sub x1, x8, #4      key 0x582 BONUS, w2 = the value   <- modify
    //
    // Both are integer instructions and w2 already holds the value being stored, so this still
    // changes the WRITE -- the number the game spends against. Read-side attempts before this
    // only ever moved the on-screen label.
    HOOK_DEFINE_INLINE(GemGrantMaxCapture) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            s_nGemMaxSeen = static_cast<s32>(ctx->W[2]);
        }
    };

    // !! "bonus > 0" IS NOT THE EMPOWERMENT TEST. It fired on ordinary rifts. !!
    //
    // That was this feature's third wrong layer, and like the first two it logged exactly the
    // numbers expected. A user reported the upgrades arriving on non-empowered Greater Rifts;
    // the disassembly says why in five instructions:
    //
    //     0077BC5C  ldr  w8, [x9, #0x30]   flag out of a per-game hash map
    //     0077BC60  cbz  w8, 0x77BC6C
    //     0077BC64  mov  w25, #1             -> w25 = 1   (EMPOWERED)
    //     0077BC6C  mov  w25, wzr            -> w25 = 0
    //     0077BC74  bl   0x51ADF0          returns 1 when attribute 0x595 < 1
    //     0077BC80  cinc w2, w25, ne       BONUS = w25 + (that ? 1 : 0)
    //
    // and 0x595 is Tiered_Loot_Run_Death_Count. So BONUS is the sum of TWO independent
    // +1s -- empowered, and completed the rift without dying -- which is exactly the stock
    // rule. A deathless ordinary rift therefore stores 1, `nBonus > 0` passed, and the
    // upgrades were granted with no empowerment anywhere in sight. Stock empowered reading 2
    // was 1+1, never a single "empowered" value.
    //
    // w25 is untouched between the cinc and this hook, so the empowerment flag is simply
    // there to be read. That is the gate now, and the deathless half is what W[2] != W[25]
    // reports -- logged rather than acted on, because the game already handles it.
    HOOK_DEFINE_INLINE(GemGrantBonusSet) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            const s32  nBonus     = static_cast<s32>(ctx->W[2]);
            const s32  nEmpowered = static_cast<s32>(ctx->W[25]);
            const bool bDeathless = (nBonus != nEmpowered);

            // Log EVERY grant, not only the ones we change. A probe that speaks up only when
            // it acts cannot distinguish "the gate held" from "the hook never ran", and this
            // feature has now been wrong three times with a convincing log each time.
            static int s_nLog = 0;
            if (s_nLog < 8) {
                ++s_nLog;
                PRINT("[d3hack-gw] grant: max=%d bonus=%d  (empowered=%d deathless=%d)",
                      s_nGemMaxSeen, nBonus, nEmpowered ? 1 : 0, bDeathless ? 1 : 0)
            }

            const int nWant = global_config.rare_cheats.empowered_gem_upgrades;
            if (nWant <= 0)
                return;
            if (nEmpowered == 0)
                return;   // ordinary rift -- leave it alone, empowering has to mean something
            const s32 nNew = static_cast<s32>(nWant) - s_nGemMaxSeen;
            if (nNew <= nBonus)
                return;   // never take attempts away
            ctx->W[2] = static_cast<u64>(static_cast<u32>(nNew));
            if (s_nLog <= 8)
                PRINT("[d3hack-gw] GRANT: max=%d bonus=%d -> %d  (total %d)", s_nGemMaxSeen,
                      nBonus, nNew, nWant)
        }
    };

    HOOK_DEFINE_TRAMPOLINE(GemUpgradeWriteProbe) {
        static void Callback(ActorCommonData *tACD, FastAttribKey tKey, s32 nValue) {
            const s32 nAttrEarly = static_cast<s32>(KeyGetAttrib(tKey));

            // d3hack-custom: RETIRED -- duplicate-legendary equipping already exists.
            //
            // `EquipMultipleLegendaries` drives a branch patch at 0x4FB9E4
            // (patch_cheat_multi_legendary_01_branch) that skips the duplicate check outright.
            // That is older, more direct and already shipped, so the attribute-write
            // suppression written here was redundant and is gone rather than left armed
            // alongside it.
            //
            // The RE is kept because it is correct and was not obvious:
            //     0049BE68  mov w22, #-0xab3   key 0x54D ITEM_EQUIPPED_BUT_DISABLED
            //     0049BE78  bl  0x46FB90       set 0x54D = 1
            //     0049BE7C  add x1, x22, #1    key 0x54E ..._DUPLICATE_LEGENDARY
            //     0049BE88  bl  0x46FB90       set 0x54E = 1
            // The pair -- disabled, then WHY -- is what identifies the duplicate branch.
            // 0x49C448 is the clear path, writing 0.

            // Diagnostic: EVERY write to the two disable flags, with its caller. 0x49BE78 is
            // the duplicate-legendary branch, but an item can be disabled from elsewhere, and
            // "only one of my two crafted primals equips" may or may not travel that path.
            // Distinct (attr, value, caller) triples only.
            if (global_config.rare_cheats.rift_reward_probe &&
                (nAttrEarly == 0x54D || nAttrEarly == 0x54E)) {
                const u64 uC = reinterpret_cast<u64>(__builtin_return_address(0)) -
                               exl::util::modules::GetTargetStart();
                static u64 s_arSeen[24] = {};
                static int s_nSeen      = 0;
                const u64  uKey = (static_cast<u64>(nAttrEarly) << 40) ^
                                 (static_cast<u64>(static_cast<u32>(nValue)) << 32) ^ uC;
                bool bNew = true;
                for (int k = 0; k < s_nSeen; ++k)
                    if (s_arSeen[k] == uKey)
                        bNew = false;
                if (bNew && s_nSeen < 24) {
                    s_arSeen[s_nSeen++] = uKey;
                    PRINT("[d3hack-dup] WRITE 0x%03X = %d   from +%llX",
                          static_cast<u32>(nAttrEarly), nValue,
                          static_cast<unsigned long long>(uC))
                }
            }

            Orig(tACD, tKey, nValue);
            if (!global_config.rare_cheats.rift_reward_probe)
                return;
            const s32 nAttr = nAttrEarly;
            if (nAttr != 0x580 && nAttr != 0x581 && nAttr != 0x582)
                return;
            const u64 uFrom = reinterpret_cast<u64>(__builtin_return_address(0)) -
                              exl::util::modules::GetTargetStart();
            static int s_nLog = 0;
            if (s_nLog < 40) {
                ++s_nLog;
                const char *szName = (nAttr == 0x580)   ? "USED"
                                     : (nAttr == 0x581) ? "MAX"
                                                        : "BONUS";
                PRINT("[d3hack-gw] WRITE %s (0x%03X) = %d   from +%llX", szName,
                      static_cast<u32>(nAttr), nValue,
                      static_cast<unsigned long long>(uFrom))
            }
        }
    };

    // d3hack-custom: WHICH attribute carries Urshi's gem-upgrade count? Observe, do not guess.
    //
    // The first attempt at this shipped a floor on JEWEL_UPGRADES_BONUS (0x582) because
    // re/attrxref.py showed it read in exactly ONE place, and that was read as "this is the
    // counter". It is not. The JEWEL_ prefix means PER-GEM, its siblings sit in the same
    // gem-rank code, and forcing it to 10 on a gem with more upgrades already applied produced
    // a NEGATIVE remaining count on screen. "Read once" is not "means what I want".
    //
    // So this logs the whole TIERED_LOOT_RUN_* block instead and lets the game say which one
    // moves when a rift is empowered and when Urshi hands out attempts:
    //
    //     0x576 KEY_LEVEL      0x577 IN_LEVEL         0x578 IS_BOSS
    //     0x585 PARTICIPATING  0x586 REWARD_CHOICE_STATE   0x587 REWARD_RECEIVES_KEY
    //     0x595 DEATH_COUNT    0x596 CORPSE_RESURRECTION_ALLOWED_GAMETIME
    //
    // Distinct (attribute, value, caller) triples only -- this getter is one of the hottest
    // functions in the game and an unfiltered log would bury the interesting read instantly.
    // The caller is captured the way the engage-event hunt did it: __builtin_return_address(0)
    // minus the module base names the function that cared.
    inline constexpr int kRiftRewardSeenMax = 64;

    struct RiftRewardSeen {
        s32 nAttr;
        s32 nVal;
        u64 uFrom;
    };

    inline RiftRewardSeen s_arRiftReward[kRiftRewardSeenMax] = {};
    inline int            s_nRiftReward                      = 0;

    HOOK_DEFINE_TRAMPOLINE(RiftRewardProbe) {
        static auto Callback(ActorCommonData *tACD, FastAttribKey tKey) -> int64 {
            const auto ret = Orig(tACD, tKey);
            if (!global_config.rare_cheats.rift_reward_probe)
                return ret;

            const s32 nAttr = static_cast<s32>(KeyGetAttrib(tKey));
            if (nAttr < 0x570 || nAttr > 0x59F)
                return ret;

            const s32 nVal = static_cast<s32>(ret);
            const u64 uFrom =
                reinterpret_cast<u64>(__builtin_return_address(0)) -
                exl::util::modules::GetTargetStart();

            for (int k = 0; k < s_nRiftReward; ++k)
                if (s_arRiftReward[k].nAttr == nAttr && s_arRiftReward[k].nVal == nVal &&
                    s_arRiftReward[k].uFrom == uFrom)
                    return ret;
            if (s_nRiftReward >= kRiftRewardSeenMax)
                return ret;
            s_arRiftReward[s_nRiftReward++] = RiftRewardSeen {nAttr, nVal, uFrom};
            PRINT("[d3hack-rr] attr 0x%03X = %d   (read from +%llX)", static_cast<u32>(nAttr),
                  nVal, static_cast<unsigned long long>(uFrom))
            return ret;
        }
    };

    // d3hack-custom: gem-upgrade attempts from an EMPOWERED greater rift.
    //
    // THE REAL TOTAL, at 0x243A9C:
    //
    //     243AA8  bl 0x46FAB0   -> MAX    (0x581, 3 -- the base attempts a GR grants)
    //     243AAC  mov w21, w0
    //     243AB8  bl 0x46FAB0   -> BONUS  (0x582, 1 when the rift was empowered)
    //     243ABC  add w21, w0, w21        MAX + BONUS      <-- HOOKED HERE
    //     243AC8  bl 0x46FAB0   -> USED   (0x580, counts up as attempts are spent)
    //     243ACC  sub w0, w21, w0         (MAX + BONUS) - USED
    //
    // Confirmed by observation, not by name. A probe over the whole 0x570..0x59F block across
    // a normal GR and an empowered one showed MAX pinned at 3 in both, USED climbing 0,1,2,3 as
    // attempts were spent, and BONUS being the only value that moves with empowerment.
    //
    // THE FIRST ATTEMPT HOOKED THE WRONG FUNCTION. 0x243990 sits in a sibling that computes
    // only `BONUS - USED` -- the bonus slice, never the total -- so forcing BONUS to 10 there
    // logged a confident "1 -> 10" while the game showed nonsense. A hook firing with the
    // numbers you expected is not evidence that it is the right hook.
    //
    // Here BONUS is adjusted so MAX + BONUS lands exactly on the configured total, and only
    // when BONUS is already non-zero -- that is the empowerment test, so a normal rift keeps
    // its stock 3. Never reduces: a target at or below MAX leaves the stock bonus alone.
    HOOK_DEFINE_INLINE(EmpoweredGemUpgrades) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            const int nWant = global_config.rare_cheats.empowered_gem_upgrades;
            if (nWant <= 0)
                return;
            const int nBonus = static_cast<int>(static_cast<s32>(ctx->W[0]));
            if (nBonus <= 0)
                return;   // not empowered -- leave it alone
            const int nMax = static_cast<int>(static_cast<s32>(ctx->W[21]));
            if (nMax >= nWant)
                return;   // already at or above the target; never take attempts away
            const int nNew = nWant - nMax;
            ctx->W[0]      = static_cast<u64>(static_cast<u32>(nNew));
            static int s_nLog = 0;
            if (s_nLog < 6) {
                ++s_nLog;
                PRINT("[d3hack-gem] empowered: max=%d bonus=%d -> bonus=%d (total %d)", nMax,
                      nBonus, nNew, nWant)
            }
        }
    };

    // The substitution itself, at the top of the plan function where w1 is still just an id.
    HOOK_DEFINE_INLINE(RiftAreaSwap) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            ResolveRiftBans();
            if (s_nBannedMap == 0)
                return;
            if (TrueGRLevel() <= 0)   // greater rifts only, same as everything else here
                return;
            const s32 nId  = static_cast<s32>(ctx->W[1]);
            const s32 snoM = AreaIdMap(nId);
            static int s_nSeen = 0;
            if (s_nSeen < 24) {
                ++s_nSeen;
                const char *szPlan = (snoM != 0) ? RiftMapName(snoM) : nullptr;
                PRINT("[d3hack-area] plan floor: id=0x%08X -> %s (table %d)",
                      static_cast<u32>(nId), (szPlan != nullptr) ? szPlan : "UNKNOWN id",
                      s_nAreaId)
            }
            // d3hack-custom: THE WRITE IS DISARMED. Read-only from here down.
            //
            // This hook was built on the theory that w1 is a level-area ID, so substituting it
            // would substitute the map AND its budget together. That theory is wrong, and the
            // disassembly says so plainly:
            //
            //     0x677180 -> 0xD9460, which walks a table of 0x98-byte records comparing
            //     [rec + 0x10] against w0, with a 16-bit count at [tbl + 0x108] and a 16-bit
            //     index. That is a HANDLE TABLE. w1 is a world HANDLE, not a level-area id,
            //     and the returned SNO is just whatever world that handle already names.
            //
            // So `s_arAreaId` maps handle -> map, and handles are per-allocation. A handle only
            // enters the table when 0x677180 resolves it at 0x784EF0 -- which is AFTER this
            // hook runs for that same floor. The current floor's handle is therefore always
            // UNKNOWN here, and the only way AreaIdMap() returns non-zero is a handle value
            // REUSED from an earlier floor. Substituting then hands the plan builder a stale
            // handle naming a world that has already been generated.
            //
            // That is a much better explanation of failure #4 -- "ruins_frost_small into a
            // NORMAL floor, stuck" -- than any of the size rules were. The player was not
            // dropped into a wrong-sized map; they were dropped into a previous floor's world
            // object.
            //
            // HANDOFF.md says substitution is disabled. It was disabled in RiftMapAssign only;
            // this one was left armed. Both are off now. The logging stays, because
            // "plan floor: id=... -> UNKNOWN id" is the evidence that this is a handle and not
            // an id, and it costs nothing to keep printing it.
            if (true)
                return;

            if (snoM == 0 || !RiftMapIsBanned(snoM))
                return;
            const s32 nNew = PickReplacementAreaId(nId);
            if (nNew == nId)
                return;
            const s32 snoNew = AreaIdMap(nNew);
            if (snoNew == 0 || RiftMapIsBanned(snoNew))
                return;
            ctx->W[1] = static_cast<u64>(static_cast<u32>(nNew));
            static int s_nLog = 0;
            if (s_nLog < 20) {
                ++s_nLog;
                PRINT("[d3hack-ban] area %d \"%s\" -> area %d \"%s\"  (known ids: %d)", nId,
                      RiftMapName(snoM), nNew, RiftMapName(snoNew), s_nAreaId)
            }
        }
    };

    HOOK_DEFINE_INLINE(RiftMapAssign) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            ResolveRiftBans();
            ResolveMapDensity();
            const s32 snoOld = static_cast<s32>(ctx->W[20]);
            if (RiftMapName(snoOld) == nullptr)   // not a rift tileset at all
                return;
            // Record BEFORE the ban test: what the game picks is what is known-good, whether
            // or not the user wants it. This is the only source of truth for "usable here".
            NoteMapAssigned(snoOld);
            s_snoAssignedMap = snoOld;   // per-map density keys off this; updated again on a swap

            // NEVER substitute a SMALL floor. Evidence, not theory:
            //
            //   boneyards (normal)      -> battlefields_large     worked
            //   corruptspire (normal)   -> keep_war_large         worked
            //   corruptspire_small      -> highlands (normal)     STUCK
            //   corruptspire_small      -> festeringwoods_large   STUCK
            //   fortress_small          -> bog_small  (SAME size) STUCK
            //
            // That last one settles it. Matching the size class exactly still trapped the
            // player, so the earlier "must fit" rule was treating a symptom. Whatever a small
            // floor carries -- tile budget, entrance placement, something not yet found -- does
            // not survive having its tileset swapped at all.
            //
            // A banned map you have to walk through is a bad outcome. A floor you cannot leave
            // is a far worse one and it has now happened three times, so small floors keep
            // whatever the game gave them until the real cause is understood.
            // SUBSTITUTION IS DISABLED. Do not re-enable without a real cause.
            //
            // Four rules were tried and every one was wrong in a way that trapped the player in
            // an unleavable floor:
            //
            //   1. any map from the table        -> highlands, stuck
            //   2. "must fit" (<= size)          -> festeringwoods_large into a small, stuck
            //   3. same size class exactly       -> bog_small into fortress_small, stuck
            //   4. never substitute smalls       -> ruins_frost_small into a NORMAL, stuck
            //
            // Each rule was consistent with the evidence available when it was written, and
            // each was still incomplete. The pattern is not size. Something else about a floor
            // is derived from its tileset before this point, and swapping the tileset leaves
            // that derived state describing a different map.
            //
            // Until that is understood, the honest position is that this feature does not
            // work. A map you dislike costs a few minutes; a floor you cannot walk out of costs
            // the run, and it has now happened four times.
            //
            // Everything else stays live and is genuinely useful: the map-name overlay, the
            // next-map prediction, per-map density, and the log that names every floor. Only
            // the swap is off.
            if (true)
                return;

            // GREATER RIFTS ONLY.
            //
            // The user reported normal rifts breaking with the ban on, and they were right to:
            // this same code path builds Nephalem rift floors, and those draw from a different
            // set of tilesets with different rules. Substituting there produced floors that do
            // not work.
            //
            // TieredLootRunGetLevel() returns -1 when not in a Greater Rift, which is exactly
            // the gate needed. It is logged the first few times because the density hook
            // already carries a warning that generation can run before the tier is set -- if
            // that happens here the gate would silently disable banning, and a log line is how
            // that gets noticed rather than guessed at.
            const int nGR = TrueGRLevel();
            {
                static int s_nGate = 0;
                if (s_nGate < 40) {
                    ++s_nGate;
                    // w19 is the OTHER half of the store pair, landing at +0x6C. If it is
                    // the floor's area budget / size class, the size-matching restriction can
                    // go away entirely: substitute any map and set this to match. Logged with
                    // the map's own size class so the correlation is readable at a glance,
                    // and run through SnoName in case it is an asset id rather than a scalar.
                    const s32   nW19  = static_cast<s32>(ctx->W[19]);
                    const char *szW19 = SnoName(nW19);
                    const MapSize eSz = MapSizeOf(RiftMapName(snoOld));
                    PRINT("[d3hack-ban] assign: gr=%d map=%d \"%s\" size=%s | +6C=%d (0x%08X) %s%s",
                          nGR, snoOld, RiftMapName(snoOld),
                          (eSz == MapSize::Small) ? "small"
                              : (eSz == MapSize::Large) ? "large"
                              : (eSz == MapSize::ExtraLarge) ? "xlarge" : "normal",
                          nW19, static_cast<u32>(nW19), szW19 != nullptr ? szW19 : "-",
                          (nGR <= 0) ? "  -- NOT a greater rift, left alone" : "")
                }
            }
            if (nGR <= 0)
                return;

            if (s_nBannedMap == 0 || !RiftMapIsBanned(snoOld))
                return;
            const s32 snoNew = PickReplacementMap(snoOld);
            // Belt and braces: a replacement that is itself banned is worse than no swap at
            // all, because it looks like the ban is working while it is not.
            if (snoNew == snoOld || RiftMapName(snoNew) == nullptr ||
                RiftMapIsBanned(snoNew) || MapIsUnusable(snoNew))
                return;
            ctx->W[20]       = static_cast<u64>(static_cast<u32>(snoNew));
            s_snoAssignedMap = snoNew;
            static int s_nLog = 0;
            if (s_nLog < 20) {
                ++s_nLog;
                PRINT("[d3hack-ban] %d \"%s\" -> %d \"%s\"  (fits size, pool %d)",
                      snoOld, RiftMapName(snoOld), snoNew, RiftMapName(snoNew), s_nSeenMap)
            }
        }
    };

    HOOK_DEFINE_INLINE(RiftMapPick) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            const auto *pArr   = reinterpret_cast<const s32 *>(static_cast<uintptr_t>(ctx->X[9]));
            const int   nCount = static_cast<int>(ctx->W[22]);
            const s32   snoOld = static_cast<s32>(ctx->W[8]);
            s32         snoNew = snoOld;

            {   // announce first: a malformed weather list must not cost us the map name
                const auto uEarly = static_cast<uintptr_t>(ctx->X[19]);
                if (uEarly > 0x1000ull)
                    AnnounceMapOnce(*reinterpret_cast<const s32 *>(uEarly + 0x68));
            }
            if (pArr == nullptr || nCount <= 0 || nCount >= 4096 ||
                reinterpret_cast<uintptr_t>(pArr) < 0x1000ull)
                return;

            // This picks the floor's WEATHER, not its tileset -- every candidate resolves to a
            // Weather asset (X1_LR_Fortress_*, X1_LR_Boneyards_*). Map banning does NOT belong
            // here; swapping a weather would change the lighting and leave the map alone.
            // What DOES belong here is fog.
            if (global_config.rare_cheats.prefer_low_fog && !NameHasLowFog(WeatherName(snoOld))) {
                int nLow = 0;
                for (int k = 0; k < nCount; ++k)
                    if (NameHasLowFog(WeatherName(pArr[k])))
                        ++nLow;
                if (nLow > 0) {
                    // Keep the game's own roll doing the choosing: find where its pick landed
                    // and reuse that index across the low-fog subset. Variety is preserved and
                    // a given rift still generates the same way twice.
                    int nIdx = 0;
                    for (int k = 0; k < nCount; ++k)
                        if (pArr[k] == snoOld) {
                            nIdx = k;
                            break;
                        }
                    int nWant = nIdx % nLow;
                    for (int k = 0; k < nCount; ++k)
                        if (NameHasLowFog(WeatherName(pArr[k])) && nWant-- == 0) {
                            snoNew = pArr[k];
                            break;
                        }
                    if (snoNew != snoOld)
                        ctx->W[8] = static_cast<u64>(static_cast<u32>(snoNew));
                }
            }

            // The tileset lives at x19+0x68 and this hook runs once per floor, so this is
            // the cheapest reliable place to announce the map. Single aligned read off the
            // pointer the game dereferences on the very next instruction.
            const auto uObj = static_cast<uintptr_t>(ctx->X[19]);
            if (uObj > 0x1000ull)
                AnnounceMapOnce(*reinterpret_cast<const s32 *>(uObj + 0x68));

            if (global_config.rare_cheats.world_gen_probe)
                RiftPickReport(pArr, nCount, snoOld, snoNew, uObj);
        }
    };;;

    HOOK_DEFINE_TRAMPOLINE(RngCoreHook) {
        static auto Callback(void *pState) -> u32 {
            if (global_config.rare_cheats.world_gen_probe) {
                const uintptr_t uRet  = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
                const uintptr_t uBase = GameOffset(0);
                RngNote(reinterpret_cast<uintptr_t>(pState), uRet);
                if (uRet > uBase)
                    RareCallerNote(static_cast<u32>(uRet - uBase));
            }
            return Orig(pState);
        }
    };

    // d3hack-custom: the NATIVE bounded-roll primitive -- the one every earlier probe missed.
    //
    //     0x11E600  stp  d9, d8, [sp, #-0x60]!      function start
    //     0x11E638  ldp  w23, w24, [x22, #0xc]      w23 = lo, w24 = span
    //     0x11E648  bl   0xa48200                   the LCG core
    //     0x11E64C  udiv w9, w0, w25 / msub         rand % (span+1)
    //     0x11E660  add  w8, w8, w23                <-- HOOKED, result = lo + offset
    //
    // Every previous probe watched the SCRIPT RandomInt wrapper at 0x8D5860, so none of them
    // could see this. Hooked where w8/w23/w24 are live and the add has not run; exlaunch's
    // inline hook branches to a trampoline afterwards, so the original instruction still runs.
    HOOK_DEFINE_INLINE(NativeRangeRoll) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (!global_config.rare_cheats.world_gen_probe)
                return;
            const int nLo   = static_cast<int>(ctx->W[23]);
            const int nSpan = static_cast<int>(ctx->W[24]);
            // d3hack-custom: UNFILTERED proof-of-life. Two runs reported "0 native rolls" and
            // the reason could be the hook not firing, or the register roles being wrong, or
            // the span filter. Those are three different bugs and inference cannot separate
            // them -- so print the raw registers for the first few calls and stop guessing.
            static int s_nRaw = 0;
            if (s_nRaw < 10) {
                ++s_nRaw;
                PRINT("[d3hack-nativeraw] w8=%d w23(lo)=%d w24(span)=%d  x22=%p", 
                      static_cast<int>(ctx->W[8]), nLo, nSpan,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(ctx->X[22])))
            }
            if (nSpan <= 0)   // WorldGenNote applies the native span limit now
                return;
            const int nRes = static_cast<int>(ctx->W[8]) + nLo;
            WorldGenNote(nLo, nLo + nSpan, nRes, GameOffset(0x11E600));
        }
    };

    // d3hack-custom: the world-entry record, at all three sInitializeWorld call sites.
    //
    // Run 6 hooked only 0x812114 and got nothing -- verified installed, simply never executed.
    // Site 3 is the live path. The register holding the world SNO's address differs per site:
    //
    //   0x812114  ldr w0, [x8, #8]     x8  = record base
    //   0x8125E8  ldr w0, [x27]        x27 = -> world SNO
    //   0x8156CC  ldr w0, [x21]        x21 = record base + 8
    //
    // Forward-only scanning from a pointer the game just dereferenced; never backwards, never
    // through a pointer found by pattern-matching.
    inline constexpr int kEntryRecBytes = 0x450;

    inline int s_nEntryDumps = 0;

    inline void EntryRecordDump(const char *szSite, uintptr_t uPtr) {
        if (!global_config.rare_cheats.world_gen_probe)
            return;
        if (uPtr < 0x1000ull || s_nEntryDumps >= 8)
            return;
        ++s_nEntryDumps;
        const auto *pWords = reinterpret_cast<const s32 *>(uPtr);
        PRINT("[d3hack-entry] --- %s, %d bytes forward, named SNOs ---", szSite, kEntryRecBytes)
        int nNamed = 0;
        for (int k = 0; k < kEntryRecBytes / 4; ++k) {
            const char *szName = SnoName(pWords[k]);
            if (szName == nullptr)
                continue;
            ++nNamed;
            PRINT("[d3hack-entry]   +%03X = %7d  %s%s", k * 4, pWords[k], szName,
                  RiftMapIsBanned(pWords[k]) ? "   <-- BANNED" : "")
        }
        PRINT("[d3hack-entry]   %s: %d named SNO(s)", szSite, nNamed)
    }

    HOOK_DEFINE_INLINE(WorldEntryRecord) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            EntryRecordDump("site1 812114 x8", static_cast<uintptr_t>(ctx->X[8]));
        }
    };

    HOOK_DEFINE_INLINE(WorldEntryRecord2) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            EntryRecordDump("site2 8125E8 x27", static_cast<uintptr_t>(ctx->X[27]));
        }
    };

    HOOK_DEFINE_INLINE(WorldEntryRecord3) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            EntryRecordDump("site3 8156CC x21", static_cast<uintptr_t>(ctx->X[21]));
        }
    };

    inline void WorldGenReport(s32 snoWorld, const void *pWorld) {
        // d3hack-custom: remember which floor is being built, so the map pick that follows
        // can be filed against it. Runs regardless of the probe -- the overlay depends on it.
        {
            const int nFloor = LrFloorOf(snoWorld);
            if (nFloor > 0) {
                s_nPickFloor = nFloor;
                // Arriving on the floor we had queued as "next" promotes it.
                if (nFloor == s_nNextFloor && s_szNextMap[0] != 0) {
                    CopyStr(s_szCurMap, sizeof(s_szCurMap), s_szNextMap);
                    s_nCurFloor    = nFloor;
                    s_szNextMap[0] = 0;
                    s_nNextFloor   = 0;
                    if (global_config.rare_cheats.map_name_overlay)
                        d3::imgui_overlay::SetMapInfo(s_szCurMap, s_szNextMap, TrueGRLevel());
                }
            } else if (snoWorld == 332336 || snoWorld == 71150) {
                MapPipelineReset();   // back in town: clear the panel
                if (global_config.rare_cheats.map_name_overlay)
                    d3::imgui_overlay::SetMapInfo("", "", 0);
            }
        }

        if (!global_config.rare_cheats.world_gen_probe)
            return;
        PRINT("[d3hack-worldgen] world %d \"%s\" -- native %d rolls / %d ranges (%d dropped) "
              "| script %d rolls / %d rows (%d dropped)",
              static_cast<int>(snoWorld), WorldName(snoWorld), s_nWorldGenNative, s_nNativeRoll,
              s_nNativeDropped, s_nWorldGenTotal, s_nWorldGenRoll, s_nWorldGenDropped)
        // Narrow native ranges first -- a pick-one-of-N lives here if it lives anywhere.
        for (int nPass = 0; nPass < 2; ++nPass)
            for (int k = 0; k < s_nNativeRoll; ++k) {
                const NativeRoll &tRow  = s_arNativeRoll[k];
                const bool        bNarr = (tRow.nHi - tRow.nLo) <= 256;
                if (bNarr != (nPass == 0))
                    continue;
                PRINT("[d3hack-worldgen]   NATIVE%s RandomInt(%d, %d) last=%d  x%u  changed %u",
                      bNarr ? "*" : " ", tRow.nLo, tRow.nHi, tRow.nLast, tRow.uCount,
                      tRow.uChanges)
            }
        for (int k = 0; k < s_nWorldGenRoll; ++k) {
            const WorldGenRoll &tRow = s_arWorldGenRoll[k];
            PRINT("[d3hack-worldgen]   script %06X  RandomInt(%d, %d) = %d   x%u", tRow.uSite,
                  tRow.nLo, tRow.nHi, tRow.nRes, tRow.uCount)
        }
        s_nWorldGenRoll    = 0;
        s_nNativeRoll      = 0;
        s_nWorldGenTotal   = 0;
        s_nWorldGenDropped = 0;
        s_nWorldGenNative  = 0;
        s_nNativeDropped   = 0;
        RareCallerReport(snoWorld);
        RiftStructScan(snoWorld, pWorld);
    }


    HOOK_DEFINE_TRAMPOLINE(RandomIntInnerHook) {
        static auto Callback(int nLo, int nHi) -> int {
            const int r = Orig(nLo, nHi);
            if (global_config.rare_cheats.world_gen_probe)   // d3hack-custom: see WorldGenNote
                WorldGenNote(nLo, nHi, r,
                             reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
            if (!s_bBiasRandIntNow)
                return r;
            const int nPct = global_config.rare_cheats.power_random_bias_pct;
            if (s_nRandIntLogs < 12) {
                ++s_nRandIntLogs;
                PRINT("[d3hack-rng] target power GetRandomInt(%d, %d) = %d", nLo, nHi, r)
            }
            if (nPct == 100 || nHi <= nLo)
                return r;
            const long long nSpan = static_cast<long long>(r) - static_cast<long long>(nLo);
            return nLo + static_cast<int>((nSpan * nPct) / 100);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(ScriptRandomIntHook) {
        static auto Callback(void *pCtx) -> int {
            const bool bProbe   = global_config.rare_cheats.power_random_probe;
            const int  nBiasSno = global_config.rare_cheats.power_random_bias_sno;
            const bool bWant    = (nBiasSno != 0);

            bool bTarget = false;
            if (bProbe || bWant) {
                const s32 sno = CurrentPowerSno();
                if (sno > 0) {
                    bTarget = (sno == nBiasSno);
                    if (bProbe) {
                        bool bNew = true;
                        for (int i = 0; i < s_nRandIntSeen; ++i)
                            if (s_arRandIntSeen[i] == sno)
                                bNew = false;
                        if (bNew && s_nRandIntSeen < kRandIntSeenMax) {
                            s_arRandIntSeen[s_nRandIntSeen++] = sno;
                            NamePowerOnce(sno);
                            PRINT("[d3hack-rng] power %d rolls GetRandomInt", sno)
                        }
                    }
                }
            }

            s_bBiasRandIntNow = bTarget;
            const int ret     = Orig(pCtx);
            s_bBiasRandIntNow = false;
            return ret;
        }
    };

    // d3hack-custom: multiply monster density.
    //
    // Density is not a GameBalance value and there is no XVar for it -- both were checked and
    // eliminated. It is LevelArea (`.lvl`) asset data, consumed by an isolated cluster at
    // 0x94B700..0x94D89C. Two multipliers compose there: how many spawn GROUPS get placed in a
    // level area, and how many monsters each spawn ITEM contributes. Tripling either triples
    // trash.
    //
    // This hooks the first, at the one integer that is literally the placement count:
    //
    //     0x94BC44  add  w27, w8, w27      w27 = (int)(density/1600 * N) + stochastic round-up
    //     0x94BC54  tbz  w8, #1, 0x94BCAC  the debug path rejoins here
    //     0x94BCAC  cmp  w27, #1           <-- HOOKED
    //     0x94BCB0  b.lt 0x94BD3C          nothing to place
    //     0x94BCB4  ... place one spawn group ...
    //     0x94BCFC  subs w27, w27, #1
    //     0x94BD00  b.ne 0x94BCB4          loop
    //
    // Verified by disassembly, not taken on trust: the `subs`/`b.ne` pair proves w27 is the
    // loop counter, and the `tbz` proves both paths converge on the hooked instruction.
    //
    // WHY THIS SITE AND NOT THE FLOATS. Every other quantity in this path -- the population
    // density scalar, the per-item scalar, the 1600.0 divisor -- is a float in a VECTOR
    // register, and exlaunch's InlineCtx exposes GPRs only (X/W/SP; see
    // lib/hook/nx64/inline_impl.hpp). w27 is an integer, so it is the only one an inline hook
    // can touch at all. It also sits AFTER the `min(..., 10.0f)` clamp at 0x94BC10, so unlike
    // scaling the density float it cannot be silently swallowed by that ceiling.
    //
    // `cmp` is safe to displace: exlaunch relocates and re-executes it after the callback, so
    // the flags are recomputed from the modified w27.
    //
    // Pack composition is untouched -- this places more groups of the normal shape rather than
    // inflating each group, so elite/champion ratios stay as designed.
    //
    // The count is clamped to 512. If the placement routine at 0x94D8A0 cannot find room it
    // just logs "Could not pick a valid spot to put spawn group" and moves on, so the real
    // ceiling is spatial and this only guards against a runaway multiply.
    // Reset at every world entry, NOT once per boot. The old single budget of 8 was spent
    // in TOWN during the first load -- the archived logs show all eight lines arriving right
    // after "rift level = -1" -- so every firing inside an actual rift floor was invisible.
    // "Density does not work in Greater Rifts" was unfalsifiable from the log for that reason
    // alone.
    inline int s_nDensityLogs = 0;

    inline void ResetDensityLogs() { s_nDensityLogs = 0; }

    // d3hack-custom: turn every Health Well into a Pool of Reflection.
    //
    // Done as a TYPE SWAP at the source rather than by bolting a pool grant onto the well's use
    // handler, because the ask was to see them as reflection pools in game. Rewriting the actor
    // SNO means the model, the gizmo type, the interaction, the sound and the UI all follow --
    // the game genuinely creates a Pool of Reflection, and there is nothing special-cased about
    // it afterwards.
    //
    // Actor SNOs come from the PC server's own tables (DiIiS WorldGenerator.cs, GizmosToSpawn):
    //
    //     healthwell_global  = 138989
    //     PoolOfReflection   = 373463
    //
    // Where the swap happens: 0x94D570 is the per-spawn-item routine in the level-area
    // population cluster, and its SECOND argument is the LevelAreaSpawnItem. Verified by
    // disassembly rather than assumed --
    //
    //     0x94D590  ldp w23, w8, [x1, #0x14]   SpawnCountMin / SpawnCountMax
    //     0x94D78C  ldr w8,     [x21, #8]      SpawnType
    //     0x94D798  ldr s1,     [x21, #0x1C]   per-item scalar
    //
    // -- which pins x1 as the item and matches the layout field-for-field. The item's
    // SNOHandle is its first two words: +0x00 group (1 = Actor), +0x04 the actor SNO.
    //
    // The write goes into the loaded LevelArea asset, so it sticks for the session and every
    // later spawn of that item is already a pool. Relaunching restores stock data -- nothing is
    // written to disk.
    //
    // Health wells placed by fixed SCENE MARKERS rather than level-area population are NOT
    // covered by this. Rifts are procedural and populate through this path, town and campaign
    // set-pieces may not. WellSpawnProbe names every actor SNO that comes through here, once
    // each, which is how to find any variant this misses instead of guessing at one.
    inline constexpr s32 kSnoHealthWell = 138989;
    inline constexpr s32 kSnoPoolOfRefl = 373463;

    inline constexpr int kWellSeenMax = 128;
    inline s32           s_arWellSeen[kWellSeenMax] = {};
    inline int           s_nWellSeen  = 0;
    inline int           s_nWellSwaps = 0;

    HOOK_DEFINE_TRAMPOLINE(HealthWellToPool) {
        static auto Callback(void *pPop, s32 *pItem, void *a2, void *a3, u64 a4, u64 a5) -> u64 {
            const uintptr_t uItem = reinterpret_cast<uintptr_t>(pItem);
            if (uItem >= 0x1000000000ull && uItem < 0x8000000000ull) {
                const s32 nGroup = pItem[0];
                const s32 nSno   = pItem[1];
                if (nGroup == 1 && nSno > 0) {
                    if (global_config.rare_cheats.well_spawn_probe && s_nWellSeen < kWellSeenMax) {
                        bool bNew = true;
                        for (int i = 0; i < s_nWellSeen; ++i)
                            if (s_arWellSeen[i] == nSno)
                                bNew = false;
                        if (bNew) {
                            s_arWellSeen[s_nWellSeen++] = nSno;
                            // group 1 = Actor, so this names them without a lookup table
                            const char *sz = nullptr;
                            if (SNOToString != nullptr) {
                                auto tName = SNOToString(1, nSno, 0);
                                sz         = tName.str();
                            }
                            PRINT("[d3hack-well] spawn actor %d = \"%s\"", nSno,
                                  (sz != nullptr) ? sz : "?")
                        }
                    }
                    if (global_config.rare_cheats.wells_as_pools && nSno == kSnoHealthWell) {
                        pItem[1] = kSnoPoolOfRefl;
                        if (s_nWellSwaps < 8) {
                            ++s_nWellSwaps;
                            PRINT("[d3hack-well] health well %d -> pool of reflection %d",
                                  kSnoHealthWell, kSnoPoolOfRefl)
                        }
                    }
                }
            }
            return Orig(pPop, pItem, a2, a3, a4, a5);
        }
    };

    // d3hack-custom: the OTHER half -- wells placed by fixed scene markers.
    //
    // The level-area swap at 0x94D570 only catches procedurally populated wells. Set-piece
    // wells come from MarkerSet data instead, and that path was found by its own error string:
    //
    //     "Error creating inactive marker for actor '%s' (Actor SNO '%s', Type '%s')"  0xE283D2
    //     xref -> 0x4EF7D0, inside the marker-to-actor routine
    //
    // Inside it, the marker's actor SNO is at marker+0x14 -- proven twice over. At 0x4EF790 the
    // error path builds an SNOHandle from it with an explicit group of 1 (Actor):
    //
    //     0x4EF790  ldr w8, [x20, #0x14]
    //     0x4EF794  mov w9, #1
    //     0x4EF7A4  stp w9, w8, [sp]        {group=1, sno}
    //
    // and at 0x4EF6A0 the live path loads the same field straight into the asset lookup:
    //
    //     0x4EF6A0  ldr w1, [x20, #0x14]    <-- the actor SNO
    //     0x4EF6B0  bl  0x6C3AB0            asset get (mgr, sno, 0)
    //
    // So hooking the instruction AFTER that load gives w1 already populated, and rewriting it
    // makes the marker resolve the Pool of Reflection asset instead. The displaced instruction
    // is a plain `ldr x24, [x24, #0x778]` -- no PC-relative operand, safe to relocate.
    //
    // Only the REGISTER is rewritten, deliberately. Writing 373463 back into marker+0x14 would
    // also work and would stick, but I have not proven that field lives in writable heap rather
    // than a read-only mapping, and a wrong guess there is a fault rather than a wrong colour.
    // The register write cannot fault. If it turns out the actor is created from the marker
    // field again later rather than from the handle this call returns, the log will show the
    // swap firing while wells still look like wells -- and that is the signal to escalate to
    // the memory write.
    HOOK_DEFINE_INLINE(MarkerWellToPool) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (!global_config.rare_cheats.wells_as_pools)
                return;
            if (static_cast<s32>(ctx->W[1]) != kSnoHealthWell)
                return;
            ctx->W[1] = static_cast<u32>(kSnoPoolOfRefl);

            // ESCALATION, 2026-08-22. The register rewrite alone fired twice in a dungeon and
            // the well still rendered as a well, so the actor is built from the marker field
            // rather than from the handle this lookup returns. x20 is the marker base (it is
            // what 0x4EF6A0 reads +0x14 from), so write the field itself; every later read,
            // including the real spawn, then sees the pool.
            //
            // Guarded on the heap range every other pointer in this mod is checked against.
            // The concern that held this back was a read-only mapping, and the guard is what
            // makes it safe to try: a bad pointer is skipped rather than faulted on.
            const uintptr_t uMarker = static_cast<uintptr_t>(ctx->X[20]);
            bool            bWrote  = false;
            if (uMarker >= 0x1000000000ull && uMarker < 0x8000000000ull) {
                auto *pSno = reinterpret_cast<s32 *>(uMarker + 0x14);
                if (*pSno == kSnoHealthWell) {
                    *pSno  = kSnoPoolOfRefl;
                    bWrote = true;
                }
            }

            static int s_nLogs = 0;
            if (s_nLogs < 8) {
                ++s_nLogs;
                PRINT("[d3hack-well] marker health well %d -> pool of reflection %d (field write %d)",
                      kSnoHealthWell, kSnoPoolOfRefl, bWrote ? 1 : 0)
            }
        }
    };

    // d3hack-custom: THE actor-creation funnel. This is the one that works.
    //
    // 0x0086E2E0 is `CreateActor(SpawnParams *x0)` and every actor in the world is born here --
    // 28 call sites converge on it, and all five spawn wrappers tail into it. `params + 0x04`
    // is the actor SNO, and it is the DECIDING read, not a copy:
    //
    //     0x86E308  ldr w22, [x0, #4]        -> actor mgr (*0x114B778) -> 0x6C3AB0 asset fetch
    //     0x86E7B4  ldp w3, w8, [x19, #4]    -> actor init on the 0x360 Actor record
    //     0x86ECCC  -> 0x86F8E0 -> 0x86F984  -> GizmoGroup tag 0x10301 on that asset
    //     0x86E87C  str w27, [x8, #0x14]!    -> ACD+0x14, stride 0x290  (a COPY, downstream)
    //
    // Model, ACD, actor object and gizmo classification all derive from that single field.
    //
    // WHY THE FIRST TWO ATTEMPTS FAILED, recorded so it is not retried:
    //   - 0x94D570 (LevelArea spawn items) never sees a well at all. A probe named every actor
    //     it placed across a dungeon and a Nephalem Rift and they were monsters, every one.
    //   - 0x4EF6A0 writes ACD+0x14 -- the copy made at 0x86E87C, downstream of every decision.
    //     Its enclosing routine registers an inactive marker for an actor that ALREADY EXISTS
    //     (reached only via 0x878490 <- 0x872470); there is no path from 0x86E2E0 to it. That
    //     is why the write succeeded and changed nothing.
    //
    // Gizmo type needs no separate patch. It is not a field on the spawn path at all -- it is a
    // TagMap entry on the Actor asset, key 0x10301, and 0x87708C proves gizmo-ness is a pure
    // function of the actor SNO. Swapping the SNO changes the tag from Healthwell to ExpPool
    // and appearance, physmesh and animset follow from the same asset.
    //
    // Inline hook rather than the obvious codecave: `sub sp, sp, #0x1c0` has no PC-relative
    // operand so exlaunch relocates it safely, and this sidesteps the cave's two hazards --
    // x30 is live here (the prologue consumes it) and a `bl` would destroy it.
    //
    // Blast radius is one SNO. Nothing else in the game is touched.
    inline constexpr int kFunnelSeenMax = 192;
    inline s32           s_arFunnelSeen[kFunnelSeenMax] = {};
    inline int           s_nFunnelSeen = 0;

    HOOK_DEFINE_INLINE(ActorSpawnWellToPool) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            const uintptr_t uParams = static_cast<uintptr_t>(ctx->X[0]);
            if (uParams < 0x1000000000ull || uParams >= 0x8000000000ull)
                return;
            auto *pSno = reinterpret_cast<s32 *>(uParams + 4);

            // Probe: name every DISTINCT actor born through the funnel. Zero swaps last run
            // was ambiguous -- it could mean no well spawned, or that wells bypass this
            // function entirely. This tells the two apart instead of guessing again.
            if (global_config.rare_cheats.well_spawn_probe && s_nFunnelSeen < kFunnelSeenMax) {
                const s32 nSno = *pSno;
                // Filtered to GIZMO-shaped SNOs. The unfiltered probe filled all 192 slots in
                // TOWN on vendors and dropped items (x1_Axe_norm_base_01, x1_Helm_hell_base_08,
                // A1_UniqueVendor_Miner_InTown_01, ...) and never got near a well. Items and
                // monsters are the overwhelming majority of what this funnel creates, so an
                // unfiltered distinct-list is useless here. Health well is 138989 and Pool of
                // Reflection is 373463; this window brackets both and drops the item/vendor
                // noise, which sits far below it.
                const bool bInteresting = (nSno >= 130000 && nSno <= 140000) ||
                                          (nSno >= 370000 && nSno <= 380000);
                if (nSno > 0 && bInteresting) {
                    bool bNew = true;
                    for (int i = 0; i < s_nFunnelSeen; ++i)
                        if (s_arFunnelSeen[i] == nSno)
                            bNew = false;
                    if (bNew) {
                        s_arFunnelSeen[s_nFunnelSeen++] = nSno;
                        const char *sz = nullptr;
                        if (SNOToString != nullptr) {
                            auto tName = SNOToString(1, nSno, 0);
                            sz         = tName.str();
                        }
                        PRINT("[d3hack-well] funnel actor %d = \"%s\"", nSno,
                              (sz != nullptr) ? sz : "?")
                    }
                }
            }

            if (!global_config.rare_cheats.wells_as_pools)
                return;
            if (*pSno != kSnoHealthWell)
                return;
            *pSno = kSnoPoolOfRefl;
            static int s_nLogs = 0;
            if (s_nLogs < 8) {
                ++s_nLogs;
                PRINT("[d3hack-well] SPAWN well %d -> pool %d (funnel 0x86E2E0)", kSnoHealthWell,
                      kSnoPoolOfRefl)
            }
        }
    };

    HOOK_DEFINE_INLINE(GreaterRiftDensity) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            // d3hack-custom: a per-map override beats the global multiplier outright, so an
            // open map can be dense without making corridors unplayable.
            // Per-map override is GR-only as well: s_snoAssignedMap is only meaningful for a
            // greater-rift floor, and a stale value must not leak into a normal rift.
            // !! TrueGRLevel() IS -1 HERE. It always has been. !!
            //
            // Every density line ever logged reads "gr=-1", across every archived session. So
            // `(TrueGRLevel() > 0)` was never once true at this call site, which means
            // MapDensityOverrides has NEVER applied to anything -- all sixteen configured
            // entries parsed, logged, resolved and then sat unreachable behind a condition
            // that cannot hold. The tier is simply not set yet when worldgen runs; the hook
            // below already anticipated exactly this for the rifts_only gate and said so in a
            // comment, and nobody carried the thought one line up.
            //
            // s_snoAssignedMap is a sound key on its own: RiftMapAssign only ever stores a
            // value that RiftMapName() recognises, so it is a rift tileset or it is zero.
            //
            // Known, bounded side effect to watch for: it is never cleared, so after a rift it
            // still names that rift's map while you are in town. The global multiplier already
            // applies in town (rifts_only is off by default and the startup lines prove it), so
            // this changes a 3 to a 10 there rather than switching something on -- but if town
            // feels wrong after a rift, this is why.
            const int nOverride = MapDensityFor(s_snoAssignedMap);

            // Rift floors and everything else get separate multipliers. A number that makes a
            // rift floor worth clearing turns town into a slideshow.
            //
            // "In a rift" is s_snoAssignedMap != 0, and that is Greater AND Nephalem rifts:
            // what can be tested at world-generation time is "this floor was handed a rift
            // tileset", not which kind of rift owns it. The tier that would separate them is
            // -1 here -- see the gate below and every gr=-1 in the logs. Doing better would
            // mean reading the plan's _GR_ labels, which lives behind RiftMapSubstitute, and
            // making density depend on the substitution setting is the coupling that has
            // already produced three dead features in this file.
            const bool bRiftFloor = (s_snoAssignedMap != 0);
            const int  nBase      = bRiftFloor
                                        ? global_config.rare_cheats.gr_density_multiplier
                                        : global_config.rare_cheats.world_density_multiplier;
            const int  nMul       = (nOverride > 0) ? nOverride : nBase;
            if (nMul <= 1)
                return;

            // GreaterRiftDensityRiftsOnly tests the MAP, not the tier.
            //
            // It used to be `TrueGRLevel() <= 0`, which is -1 at this call site in every log
            // ever recorded -- so switching the setting ON disabled density everywhere,
            // including in the rifts it was meant to restrict it to. The skip message even
            // told you to turn the setting off, which was correct advice for a broken gate and
            // is why it never read as a bug.
            //
            // s_snoAssignedMap is the honest signal: RiftMapAssign (0x4BC450) stores it before
            // generation and only ever stores a tileset RiftMapName() recognises, and
            // WorldCreateProbe now zeroes it when a non-rift world is created.
            //
            // Ordering caveat, from the logs and not assumed: this hook fires BEFORE the
            // [d3hack-wc] CREATE line for the same floor. So a town generation that runs
            // before the create that clears the flag can still see the previous rift's map.
            // The line below reports the decision every time, which is how that gets caught if
            // it happens rather than argued about.
            const int nGR = TrueGRLevel();
            if (global_config.rare_cheats.gr_density_rifts_only && !bRiftFloor) {
                if (s_nDensityLogs < 8) {
                    ++s_nDensityLogs;
                    PRINT("[d3hack-density] skipped, not a rift world: groups=%u (gr=%d)",
                          static_cast<unsigned>(ctx->W[27]), nGR)
                }
                return;
            }

            const u32 uWas = ctx->W[27];
            if (uWas == 0u)
                return;
            u64        uNew      = static_cast<u64>(uWas) * static_cast<u64>(nMul);
            const bool bClamped  = (uNew > 512ull);
            if (bClamped)
                uNew = 512ull;
            ctx->W[27] = static_cast<u32>(uNew);

            if (s_nDensityLogs < 8) {
                ++s_nDensityLogs;
                // Name the map and say whether an override matched. Without those two facts
                // the line cannot answer the only question anyone asks of it -- "did MY
                // setting apply to THIS floor" -- and reports it in gr=-1 form that looks
                // like a failure when it is merely an unset tier.
                const char *szMap = RiftMapName(s_snoAssignedMap);
                PRINT("[d3hack-density] gr=%d map=%d \"%s\" spawn groups %u -> %u (x%d %s)%s",
                      nGR, s_snoAssignedMap, (szMap != nullptr) ? szMap : "-",
                      static_cast<unsigned>(uWas), static_cast<unsigned>(uNew), nMul,
                      (nOverride > 0) ? "PER-MAP" : (bRiftFloor ? "rift" : "world"),
                      bClamped ? "  CLAMPED at 512 -- the multiplier is capped here" : "")
            }
        }
    };

    HOOK_DEFINE_TRAMPOLINE(PowerFormulaHook) {
        static auto Callback(u32 uAttribId, u32 uKey) -> float {
            const float fRaw = Orig(uAttribId, uKey);

            const int nIdx = FormulaIndexFromKey(uKey);
            if (nIdx < 0)
                return fRaw;  // not a Script Formula -- some other power attribute

            const int nScaleSno = global_config.rare_cheats.power_formula_sno;
            const int nPctCfg   = global_config.rare_cheats.power_formula_percent;
            const bool bProbe   = global_config.rare_cheats.power_formula_probe;
            const bool bScaling = (nScaleSno != 0 && nPctCfg != 100);
            if (!bProbe && !bScaling)
                return fRaw;

            const s32 snoPower = AttribIdToPowerSno(uAttribId);
            if (snoPower <= 0)
                return fRaw;

            if (bProbe && s_nFormulaSeen < kFormulaSeenMax) {
                bool bNew = true;
                for (int i = 0; i < s_nFormulaSeen; ++i)
                    if (s_arFormulaSeen[i].sno == snoPower && s_arFormulaSeen[i].idx == nIdx)
                        bNew = false;
                if (bNew) {
                    NamePowerOnce(snoPower);
                    s_arFormulaSeen[s_nFormulaSeen].sno = snoPower;
                    s_arFormulaSeen[s_nFormulaSeen].idx = nIdx;
                    ++s_nFormulaSeen;
                    // Printed as milli-units as well as the raw float: PRINT goes through a
                    // formatter whose %f handling is not worth trusting for a value this
                    // important, and an integer is what a config knob will want anyway.
                    PRINT("[d3hack-power] sno %d formula %2d = %d.%03d (key %05X)", snoPower, nIdx,
                          static_cast<int>(fRaw),
                          static_cast<int>((fRaw < 0.0f ? -fRaw : fRaw) * 1000.0f) % 1000,
                          static_cast<unsigned>(uKey))
                }
            }

            const int nPct = nPctCfg;
            if (!bScaling || snoPower != nScaleSno)
                return fRaw;
            const u64 uMask = FormulaMask();
            if (uMask != 0ull) {
                if (((uMask >> nIdx) & 1ull) == 0ull)
                    return fRaw;
            } else {
                const int nWant = global_config.rare_cheats.power_formula_index;
                if (nWant >= 0 && nIdx != nWant)
                    return fRaw;
            }

            const float fOut = fRaw * (static_cast<float>(nPct) / 100.0f);
            static int  s_nScaleLogs = 0;
            if (s_nScaleLogs < 16) {
                ++s_nScaleLogs;
                PRINT("[d3hack-power] SCALED sno %d formula %d: %d.%03d -> %d.%03d (%d%%)", snoPower,
                      nIdx, static_cast<int>(fRaw),
                      static_cast<int>((fRaw < 0.0f ? -fRaw : fRaw) * 1000.0f) % 1000,
                      static_cast<int>(fOut),
                      static_cast<int>((fOut < 0.0f ? -fOut : fOut) * 1000.0f) % 1000, nPct)
            }
            return fOut;
        }
    };

    // d3hack-custom: 0x7A0BA0 -- the routine that actually grants a Pool of Reflection.
    //
    // Identified rather than guessed, which is the whole difference from 0x2BC520:
    //   - a real function entry: the prologue `str d8,[sp,#-0x30]!` sits immediately after
    //     `udf #0` padding at 0x7A0B94, so it is not the middle of something else
    //   - exactly ONE caller in the whole of .text, `BL` from 0x7D0618
    //   - one argument, the hero: 0x7A0BB4 `mov x19, x0`, and x1..x7 are written before they
    //     are ever read, so nothing else is passed in
    //   - the body is unmistakable: 0x7A0C3C reads the rested-XP pool, 0x7A0C28 multiplies a
    //     fraction out of a game-balance record by a level's worth of experience, 0x7A0C40
    //     adds it, 0x7A0C5C caps it, 0x7A0C64 asks whether the Altar node is active, and it
    //     writes the result back through 0x485880
    //
    // Contrast 0x2BC520, which was hooked on the strength of "touches PoolOfReflectionBuff and
    // formats a string": that turned out to be a TOOLTIP BUILDER -- it formats the localised
    // key "pool of reflection %d" at 0x2BD91C -- and installing a trampoline on it crashed the
    // game on world entry for an entire evening. The lesson is not "never hook"; it is that an
    // address needs an entry, a caller count and a signature before it gets one.
    //
    // Four arguments are declared and forwarded even though only x0 is used, so x0..x3 reach
    // Orig exactly as they arrived and the return value is passed straight through. The count
    // is taken AFTER Orig, so the game's own bookkeeping happens first.
    HOOK_DEFINE_TRAMPOLINE(PoolGranted) {
        static auto Callback(void *a0, u64 a1, u64 a2, u64 a3) -> u64 {
            const u64 ret = Orig(a0, a1, a2, a3);
            if (global_config.rare_cheats.pool_grant_hook) {
                pools::Add(1);
                PRINT("[d3hack-pool] POOL GRANTED: count=%d, xp x%llu.%02llu", pools::Count(),
                      pools::Numerator() / 100ull, pools::Numerator() % 100ull)
            }
            return ret;
        }
    };

    // d3hack-custom: 0x2BC520 -- touches PoolOfReflectionBuff (GlobalSNOGet 0x2D8) and formats
    // the "pool of reflection %d" string, so it is the pool-activation path.
    //
    // UNVERIFIED whether it fires once per pool or repeatedly (it could be a tooltip builder;
    // it has no direct callers, so it is reached virtually). The log line below is the check:
    // touch ONE pool and the count should rise by exactly one. If it balloons, this is the
    // wrong anchor and the right one is the SMSG_ACTIVATE_POOL_OF_REFLECTION (0xC8) handler.
    HOOK_DEFINE_TRAMPOLINE(PoolOfReflectionTouched) {
        static auto Callback(void *a0, void *a1, void *a2, void *a3) -> u64 {
            const u64 ret = Orig(a0, a1, a2, a3);
            if (global_config.rare_cheats.pool_xp_percent > 0 && pools::ShouldCountTouch()) {
                pools::Add(1);
                // Through Numerator(), which does this in 64-bit. The same expression written
                // inline as int overflows at a count of 86 million with the default 25%.
                const unsigned long long uNum = pools::Numerator();
                PRINT("[d3hack-custom] pool touched: count=%d, xp x%llu.%02llu", pools::Count(),
                      uNum / 100ull, uNum % 100ull)
            }
            return ret;
        }
    };

    // d3hack-custom: one-shot dump of the Altar of Rites (internally "DarkAlchemy").
    //
    // 0x19010F0 is a shared globals blob, not an altar struct. Its +0x00 / +0xC0 / +0xD8
    // vectors are a helm list and an achievement-key list -- unrelated, do not dump them.
    // Only these three fields are altar:
    //
    //   +0x090  vector, stride 0x30,  4 rows  -- the 4 Major nodes
    //   +0x0A8  vector, stride 0x30, 26 rows  -- the 26 Minor nodes
    //   +0x128  30 node SNOs, globals 0x63F..0x65C, minors first then majors
    //
    // A 0x30 row is two {ptr,count,cap} vectors:
    //
    //   row +0x00  vecA, element stride 0x08 = {u32 type, u32 quantity}   <- the cost
    //   row +0x18  vecB, element stride 0x48 = the requirement records
    //
    // and a 0x48 requirement record is:
    //
    //   +0x00  vector of item NAMES, element stride 0x28 (std::string w/ SSO:
    //          ptr, size, capacity|0x80000000 heap flag, then a 16-byte inline buffer)
    //   +0x18  u64, observed always 1
    //   +0x20  std::string, the display key e.g. "P75_DarkAlchemyMinorNodeDisplay_002"
    //
    // The previous pass read vecA at stride 0x28 and walked ~0xF0 bytes off the end of a
    // ~0x30-byte allocation, which is what crashed the game. 0x08 is the SMALLEST possible
    // stride, so reading count*8 stays inside the allocation whatever the real size is --
    // that is the safe direction to guess, and the earlier guess had it backwards.
    inline bool AltarPtrOk(const void *pv) {
        // Every heap pointer this game hands out sits in 0x10_0000_0000..0x80_0000_0000.
        // Anything outside that is a count or an id that merely looks pointer-shaped.
        const uintptr_t u = reinterpret_cast<uintptr_t>(pv);
        return u >= 0x1000000000ull && u < 0x8000000000ull;
    }

    inline void AltarAscii(char *szOut, int nCap, const void *pv) {
        int i = 0;
        if (AltarPtrOk(pv)) {
            const char *p = reinterpret_cast<const char *>(pv);
            for (; i < nCap - 1; ++i) {
                const char c = p[i];
                if (c == 0)
                    break;
                szOut[i] = (c >= 0x20 && c < 0x7F) ? c : '.';
            }
        }
        szOut[i] = 0;
    }

    struct AltarVec {
        uint8 *pData;
        int64  nCount;
        int64  nCapacity;
    };

    inline bool AltarVecOk(const AltarVec *ptVec, int nMax) {
        return ptVec != nullptr && AltarPtrOk(ptVec->pData) && ptVec->nCount > 0 &&
               ptVec->nCount <= nMax;
    }

    inline void DumpAltarTables() {
        static bool s_done   = false;
        static int  s_nTries = 0;
        if (s_done || s_nTries >= 6)
            return;
        ++s_nTries;

        const uintptr_t uBase      = GameOffset(0x19010F0);
        const int       arNode[2]  = {0x0A8, 0x090};
        const char     *arWhich[2] = {"minor", "major"};

        for (int v = 0; v < 2; ++v) {
            const auto *ptVec = reinterpret_cast<const AltarVec *>(uBase + arNode[v]);
            if (!AltarVecOk(ptVec, 64)) {
                PRINT("[d3hack-diag] altar %s: not ready (ptr=%p count=%d)", arWhich[v],
                      ptVec->pData, static_cast<int>(ptVec->nCount))
                continue;
            }
            s_done = true;
            PRINT("[d3hack-diag] altar %s nodes: count=%d", arWhich[v],
                  static_cast<int>(ptVec->nCount))

            for (int r = 0; r < static_cast<int>(ptVec->nCount); ++r) {
                const uint8 *pRow = ptVec->pData + (r * 0x30);
                const auto  *ptA  = reinterpret_cast<const AltarVec *>(pRow);
                const auto  *ptB  = reinterpret_cast<const AltarVec *>(pRow + 0x18);
                const int    nA   = AltarVecOk(ptA, 64) ? static_cast<int>(ptA->nCount) : 0;
                const int    nB   = AltarVecOk(ptB, 64) ? static_cast<int>(ptB->nCount) : 0;
                PRINT("[d3hack-diag] %s[%02d] cost=%d req=%d", arWhich[v], r, nA, nB)

                for (int e = 0; e < nA; ++e) {
                    const auto *pw = reinterpret_cast<const uint32 *>(ptA->pData + (e * 8));
                    PRINT("[d3hack-diag]   %s[%02d] cost[%d] type=%u qty=%u", arWhich[v], r, e,
                          pw[0], pw[1])
                }

                for (int e = 0; e < nB; ++e) {
                    const uint8 *p = ptB->pData + (e * 0x48);
                    char         szName[64];
                    AltarAscii(szName, sizeof(szName), *reinterpret_cast<void *const *>(p + 0x20));
                    const auto *ptItems = reinterpret_cast<const AltarVec *>(p);
                    const int   nI      = AltarVecOk(ptItems, 128) ? static_cast<int>(ptItems->nCount) : 0;
                    PRINT("[d3hack-diag]   %s[%02d] req[%d] f=%d items=%d \"%s\"", arWhich[v], r, e,
                          static_cast<int>(*reinterpret_cast<const int64 *>(p + 0x18)), nI, szName)
                    for (int k = 0; k < nI; ++k) {
                        char szItem[64];
                        AltarAscii(szItem, sizeof(szItem),
                                   *reinterpret_cast<void *const *>(ptItems->pData + (k * 0x28)));
                        PRINT("[d3hack-diag]     %s[%02d] req[%d] item[%02d] \"%s\"", arWhich[v], r, e,
                              k, szItem)
                    }
                }
            }
        }

        if (!s_done)
            return;
        const auto *pNodes = reinterpret_cast<const uint32 *>(uBase + 0x128);
        for (int i = 0; i < 30; i += 6)
            PRINT("[d3hack-diag] altar node[%02d] %08X %08X %08X %08X %08X %08X", i, pNodes[i],
                  pNodes[i + 1], pNodes[i + 2], pNodes[i + 3], pNodes[i + 4], pNodes[i + 5])
        PRINT("[d3hack-diag] altar gbids: ChallengeRiftCache=%08X X1_Salvage_Primal=%08X X1_Salvage_Sanctified=%08X",
              static_cast<unsigned>(GlobalSNOGet(0x7DD)), static_cast<unsigned>(GlobalSNOGet(0x7DF)),
              static_cast<unsigned>(GlobalSNOGet(0x7DE)))
    }

    // d3hack-custom: swap the Altar of Rites' Challenge Rift Cache node over to Primordial Ashes.
    //
    // The full table dump identified currency type 21 as Primordial Ashes: the three Major
    // seals cost exactly (21, 55), (21, 110) and (21, 165), which is the real game's ash
    // ladder. Every other type lines up with the same CurrencyType enum DiIiS uses --
    // 1 Blood Shards, 3 Reusable Parts, 4 Arcane Dust, 6 Death's Breath, 7 Forgotten Soul,
    // 8..12 the five bounty mats, 13..16 the demon organs, 20 Greater Rift Keystone.
    //
    // minor[14] is the Challenge Rift Cache node: empty cost vector, one requirement entry
    // naming the item "ChallengeRiftRewardBag". So the swap is to give the row a
    // one-element cost vector {21, N} and empty its requirement vector -- the exact shape
    // the Major seals already use, which is why the tooltip picks the change up for free.
    //
    // The row is located by NAME, never by index, so a data change cannot silently retarget
    // this onto some other node.
    inline bool AltarNameIs(const void *pv, const char *szWant) {
        if (!AltarPtrOk(pv))
            return false;
        const char *p = reinterpret_cast<const char *>(pv);
        for (int i = 0; i < 64; ++i) {
            if (p[i] != szWant[i])
                return false;
            if (szWant[i] == 0)
                return true;
        }
        return false;
    }

    inline void PatchAltarChallengeRiftCost() {
        static bool s_done   = false;
        static int  s_nTries = 0;
        const int   nAshes   = global_config.rare_cheats.altar_crc_ashes;
        if (s_done || nAshes <= 0 || s_nTries >= 6)
            return;
        ++s_nTries;

        const auto *ptVec = reinterpret_cast<const AltarVec *>(GameOffset(0x19010F0) + 0xA8);
        if (!AltarVecOk(ptVec, 64))
            return;

        for (int r = 0; r < static_cast<int>(ptVec->nCount); ++r) {
            uint8 *pRow = ptVec->pData + (r * 0x30);
            auto  *ptA  = reinterpret_cast<AltarVec *>(pRow);
            auto  *ptB  = reinterpret_cast<AltarVec *>(pRow + 0x18);
            if (!AltarVecOk(ptB, 64))
                continue;
            for (int e = 0; e < static_cast<int>(ptB->nCount); ++e) {
                const uint8 *pReq   = ptB->pData + (e * 0x48);
                const auto  *ptItem = reinterpret_cast<const AltarVec *>(pReq);
                if (!AltarVecOk(ptItem, 128))
                    continue;
                for (int k = 0; k < static_cast<int>(ptItem->nCount); ++k) {
                    if (!AltarNameIs(*reinterpret_cast<void *const *>(ptItem->pData + (k * 0x28)),
                                     "ChallengeRiftRewardBag"))
                        continue;
                    // Heap, not static: if this vector is ever destroyed the game will free
                    // the pointer, and it has to be a real allocation when that happens.
                    auto *pCost = new (std::nothrow) uint32[2];
                    if (pCost == nullptr)
                        return;
                    pCost[0] = 21;  // Primordial Ashes
                    pCost[1] = static_cast<uint32>(nAshes);
                    ptA->pData     = reinterpret_cast<uint8 *>(pCost);
                    ptA->nCount    = 1;
                    ptA->nCapacity = 1;
                    ptB->nCount    = 0;
                    s_done         = true;
                    PRINT("[d3hack-custom] altar minor[%02d]: ChallengeRiftRewardBag -> %d Primordial Ashes",
                          r, nAshes)
                    return;
                }
            }
        }
        PRINT("[d3hack-custom] altar: ChallengeRiftRewardBag node not found (try %d)", s_nTries)
    }

    // d3hack-custom: pay the Altar of Rites' item sacrifices in crafting materials instead.
    //
    // The altar's 26 seals are decoded in re/altar-dump-full.txt (DumpAltarTables). Thirteen
    // of them want an ITEM rather than a cost. The dump gives internal names; the published
    // seal list gives what they mean. Both are needed -- guessing from the ids alone got the
    // wrong seal twice, including minor[03], whose 35 accepted helms include Unique_Helm_Set_*
    // entries even though the seal is "any class-specific helm" and not a set piece at all.
    //
    //   minor[01] a Flawless Diamond            minor[14] a Challenge Rift Cache
    //   minor[03] any class-specific helm       minor[17] an Ancient Hellfire Amulet  <- picked
    //   minor[06] Reaper's Wraps        <-pick  minor[18] four SET DUNGEON PAGES      <- picked
    //   minor[09] a Ruby + Ring of Royal Gr.<-  minor[19] an Ancient Puzzle Ring      <- picked
    //   minor[10] a Flawless Royal Emerald      minor[22] a rank 125 Whisper of Atonement
    //   minor[11] a Ramaladni's Gift            minor[23] any augmented weapon        <- picked
    //   minor[13] a Petrified Scream    <-pick  minor[24] a Staff of Herding          <- picked
    //
    // AltarItemCostMode:
    //   0  stock
    //   1  the picked seals only                    <-- default
    //   2  every item sacrifice in the table above
    //
    // Each converted seal pays a RECIPE. Amounts are base x tier x AltarMaterialPercent, with
    // tier = 1 + depth/4 (minors depth 0..25, majors 26+):
    //
    //   the four materials   Forgotten Soul (7) 25   Veiled Crystal (5) 100
    //                        Blood Shards   (1) 200  Reusable Parts (3) 50
    //   blood shards only    Blood Shards   (1) 300
    //
    // Shards-only carries the higher base precisely because it is the whole cost, not a
    // quarter of it. What the picked seals end up charging:
    //
    //   minor[06] Reaper's Wraps        t2   600 shards
    //   minor[09] Ring of Royal Gr.     t3   900 shards
    //   minor[13] Petrified Scream      t4  1200 shards
    //   minor[17] Anc. Hellfire Amulet  t5  1500 shards
    //   minor[18] 4 Set Dungeon Pages   t5   125 soul / 500 crystal / 1000 shard / 250 parts
    //   minor[19] Anc. Puzzle Ring      t5  1500 shards
    //   minor[23] any Augmented Weapon  t6  1800 shards
    //   minor[24] Staff of Herding      t7  2100 shards
    //
    // The altar's own shard nodes run 1100/1300/1400/1500/1600 across the same stretch, so
    // every one of these lands inside the ladder it already charges rather than beside it.
    //
    // ONE REQUIREMENT AT A TIME, not one row at a time. minor[09] asks for a Flawless Royal
    // Ruby AND a Ring of Royal Grandeur as two separate requirement entries; only the matched
    // entry is dropped, so the ruby survives and the seal still wants it. The entries are
    // compacted down and nCount reduced, which leaves the trailing copy outside the live range
    // where nothing will destroy it. Mode 2 drops every entry in the row.
    //
    // Emptying (or shortening) the requirement vector is the same field edit
    // PatchAltarChallengeRiftCost already uses, which is why the tooltip follows without
    // touching the UI. Costs a seal ALREADY has are kept and the recipe appended, so minor[09]
    // keeps its 20 Death's Breath and minor[19] its 50-of-each bounty mats.
    //
    // Runs on every world and is self-healing: a converted requirement is gone, so the next
    // pass skips it. If the altar tables are ever rebuilt the row comes back and is converted
    // again.
    //
    // Two seals are never converted:
    //   - minor[14], the Challenge Rift Cache -- PatchAltarChallengeRiftCost runs first and
    //     turns it into Primordial Ashes. Set AltarChallengeRiftCacheAshes = 0 to free it.
    //   - major[03] -- its requirement entry holds NO items, so it is a code-driven gate (the
    //     final seal waiting on the minor nodes), not an item sacrifice. Emptying it would
    //     hand the seal over for free. That is what the nItems == 0 check is for.
    inline bool AltarNameHas(const void *pv, const char *szNeedle) {
        char sz[64];
        AltarAscii(sz, sizeof(sz), pv);
        for (int i = 0; sz[i] != 0; ++i) {
            int k = 0;
            while (szNeedle[k] != 0 && sz[i + k] == szNeedle[k])
                ++k;
            if (szNeedle[k] == 0)
                return true;
        }
        return false;
    }

    inline void ConvertAltarItemNodes() {
        const int nMode = global_config.rare_cheats.altar_item_cost_mode;
        const int nPct  = global_config.rare_cheats.altar_material_percent;
        if (nMode <= 0)
            return;

        // Currency ids are the CurrencyType enum, pinned by the dump: the three Major seals
        // cost (21, 55/110/165), the real game's Primordial Ash ladder, and every other
        // observed id lines up. 1 Blood Shards, 3 Reusable Parts, 5 Veiled Crystal,
        // 7 Forgotten Soul.
        struct Recipe {
            int    nParts;
            uint32 arType[4];
            uint32 arBase[4];
        };
        static const Recipe tFourMats {4, {7u, 5u, 1u, 3u}, {25u, 100u, 200u, 50u}};
        static const Recipe tShards {1, {1u, 0u, 0u, 0u}, {300u, 0u, 0u, 0u}};

        // Seals singled out by an item-name substring, never by row index, so a data change
        // cannot silently retarget this. Matching is case sensitive, which is what keeps
        // "SwarmRiftKey" (the Petrified Scream, minor[13]) off "swarm_rift_gem_125plus" (the
        // Whisper of Atonement, minor[22]), and "Unique_Ring_004" (Puzzle Ring) off
        // "unique_ring_107_x1" (Ring of Royal Grandeur).
        struct Picked {
            const char   *szToken;
            const char   *szWhat;
            const Recipe *ptRecipe;
        };
        static const Picked arPicked[] = {
            {"class_clues", "SET-DUNGEON-PAGES", &tFourMats},
            {"StaffOfCow", "STAFF-OF-HERDING", &tShards},
            {"Unique_Bracer_103", "REAPERS-WRAPS", &tShards},
            {"unique_ring_107", "RING-OF-ROYAL-GRANDEUR", &tShards},
            {"SwarmRiftKey", "PETRIFIED-SCREAM", &tShards},
            {"x1_Amulet_norm_unique_25", "ANCIENT-HELLFIRE-AMULET", &tShards},
            {"Unique_Ring_004", "ANCIENT-PUZZLE-RING", &tShards},
            {"augmented_item", "AUGMENTED-WEAPON", &tShards},
        };
        constexpr int PICKED_N = static_cast<int>(sizeof(arPicked) / sizeof(arPicked[0]));
        constexpr int MAX_REQS = 16;
        constexpr int REQ_SIZE = 0x48;

        const uintptr_t uBase      = GameOffset(0x19010F0);
        const int       arOff[2]   = {0x0A8, 0x090};
        const char     *arWhich[2] = {"minor", "major"};
        int             nDone      = 0;

        for (int v = 0; v < 2; ++v) {
            auto *ptVec = reinterpret_cast<AltarVec *>(uBase + arOff[v]);
            if (!AltarVecOk(ptVec, 64))
                continue;

            for (int r = 0; r < static_cast<int>(ptVec->nCount); ++r) {
                uint8 *pRow = ptVec->pData + (r * 0x30);
                auto  *ptA  = reinterpret_cast<AltarVec *>(pRow);
                auto  *ptB  = reinterpret_cast<AltarVec *>(pRow + 0x18);
                // d3hack-custom: demon-organ COSTS -> materials.
                //
                // minor[05] ("1 of each Uber Drop") is cost=4 req=0 -- the four Infernal
                // Machine organs are CURRENCY costs (DemonOrganDiablo 13, Ghom 14,
                // SiegeBreaker 15, SkeletonKing 16), not item requirements. The
                // requirement-based conversion below never sees that row, which is also why
                // switching to mode 2 did not fix it and only dropped other seals'
                // requirements as collateral.
                //
                // This runs BEFORE the nReq gate, because a row with no requirements at all
                // is exactly the case being handled. The row already carries one cost entry
                // per organ, so it is an in-place rewrite -- same count, new type and
                // quantity -- with no allocation and nothing to leak.
                if (ptA->nCount > 0 && AltarPtrOk(ptA->pData)) {
                    auto *pCost  = reinterpret_cast<uint32 *>(ptA->pData);
                    int   nOrgan = 0;
                    for (int i = 0; i < static_cast<int>(ptA->nCount); ++i) {
                        const uint32 t = pCost[i * 2];
                        if (t >= 13u && t <= 19u)
                            ++nOrgan;
                    }
                    if (nOrgan > 0) {
                        const int nDepthC = (v == 0) ? r : (26 + r);
                        const int nTierC  = 1 + (nDepthC / 4);
                        int       nPart   = 0;
                        for (int i = 0; i < static_cast<int>(ptA->nCount); ++i) {
                            const uint32 t = pCost[i * 2];
                            if (t < 13u || t > 19u)
                                continue;
                            const int k = nPart % tFourMats.nParts;
                            uint32    q = (tFourMats.arBase[k] * static_cast<uint32>(nTierC) *
                                        static_cast<uint32>(nPct)) /
                                       100u;
                            if (q == 0u)
                                q = 1u;
                            pCost[i * 2 + 0] = tFourMats.arType[k];
                            pCost[i * 2 + 1] = q;
                            ++nPart;
                        }
                        ++nDone;
                        PRINT("[d3hack-custom] altar %s[%02d]: %d demon organ cost(s) -> "
                              "materials (tier %d)",
                              arWhich[v], r, nOrgan, nTierC)
                    }
                }

                const int nReq = static_cast<int>(ptB->nCount);
                if (nReq <= 0 || nReq > MAX_REQS || !AltarPtrOk(ptB->pData))
                    continue;  // no item requirement, already converted, or nonsense count

                // Which requirement entries go, and which recipe pays for them?
                bool arDrop[MAX_REQS] = {};
                char szFirst[64]      = {0};
                int  nItems           = 0;
                int  nDrop            = 0;
                int  nPick            = -1;
                for (int e = 0; e < nReq; ++e) {
                    const auto *ptItems =
                        reinterpret_cast<const AltarVec *>(ptB->pData + (e * REQ_SIZE));
                    if (!AltarVecOk(ptItems, 128))
                        continue;
                    int nHit = -1;
                    for (int k = 0; k < static_cast<int>(ptItems->nCount); ++k) {
                        const void *pName =
                            *reinterpret_cast<void *const *>(ptItems->pData + (k * 0x28));
                        ++nItems;
                        if (szFirst[0] == 0)
                            AltarAscii(szFirst, sizeof(szFirst), pName);
                        for (int p = 0; p < PICKED_N; ++p)
                            if (nHit < 0 && AltarNameHas(pName, arPicked[p].szToken))
                                nHit = p;
                    }
                    if (nHit >= 0 && nPick < 0)
                        nPick = nHit;
                    if (nHit >= 0 || nMode == 2) {
                        arDrop[e] = true;
                        ++nDrop;
                    }
                }
                if (nItems == 0)
                    continue;  // code-driven gate, not an item sacrifice -- see major[03] above
                if (nDrop == 0)
                    continue;  // mode 1 and nothing picked in this row

                const Recipe &tR = (nPick >= 0) ? *arPicked[nPick].ptRecipe : tFourMats;

                const int nDepth = (v == 0) ? r : (26 + r);
                const int nTier  = 1 + (nDepth / 4);

                const int nOld =
                    (ptA->nCount > 0 && AltarPtrOk(ptA->pData)) ? static_cast<int>(ptA->nCount) : 0;
                // Heap, not static: if this vector is ever destroyed the game frees pData, so
                // it has to be a real allocation when that happens. Never freed by us -- the
                // altar globals live for the process.
                auto *pNew = new (std::nothrow) uint32[(nOld + tR.nParts) * 2];
                if (pNew == nullptr)
                    return;
                const auto *pOld = reinterpret_cast<const uint32 *>(ptA->pData);
                for (int i = 0; i < nOld * 2; ++i)
                    pNew[i] = pOld[i];
                for (int i = 0; i < tR.nParts; ++i) {
                    uint32 q =
                        (tR.arBase[i] * static_cast<uint32>(nTier) * static_cast<uint32>(nPct)) / 100u;
                    if (q == 0u)
                        q = 1u;
                    pNew[(nOld + i) * 2 + 0] = tR.arType[i];
                    pNew[(nOld + i) * 2 + 1] = q;
                }
                ptA->pData     = reinterpret_cast<uint8 *>(pNew);
                ptA->nCount    = nOld + tR.nParts;
                ptA->nCapacity = nOld + tR.nParts;

                // Compact the survivors down. The dropped entries' strings leak; the trailing
                // duplicate sits past nCount so nothing will ever destroy it twice.
                int nKeep = 0;
                for (int e = 0; e < nReq; ++e) {
                    if (arDrop[e])
                        continue;
                    if (nKeep != e) {
                        uint8       *pDst = ptB->pData + (nKeep * REQ_SIZE);
                        const uint8 *pSrc = ptB->pData + (e * REQ_SIZE);
                        for (int b = 0; b < REQ_SIZE; ++b)
                            pDst[b] = pSrc[b];
                    }
                    ++nKeep;
                }
                ptB->nCount = nKeep;
                ++nDone;

                const char *szWhat = (nPick >= 0) ? arPicked[nPick].szWhat : "ITEM";
                if (tR.nParts == 1)
                    PRINT("[d3hack-custom] altar %s[%02d] %s \"%s\" -> %u blood shards (%d of %d "
                          "requirements dropped)",
                          arWhich[v], r, szWhat, szFirst, pNew[nOld * 2 + 1], nDrop, nReq)
                else
                    PRINT("[d3hack-custom] altar %s[%02d] %s \"%s\" -> %u soul / %u crystal / "
                          "%u shard / %u parts (%d of %d requirements dropped)",
                          arWhich[v], r, szWhat, szFirst, pNew[(nOld + 0) * 2 + 1],
                          pNew[(nOld + 1) * 2 + 1], pNew[(nOld + 2) * 2 + 1],
                          pNew[(nOld + 3) * 2 + 1], nDrop, nReq)
            }
        }

        if (nDone > 0)
            PRINT("[d3hack-custom] altar: %d seals converted (mode %d, %d%%)", nDone, nMode, nPct)
        else
            PRINT("[d3hack-custom] altar: nothing to convert (mode %d) -- already done, or the "
                  "tables are not up yet",
                  nMode)
    }

    // d3hack-custom: name the powers worth watching in the formula probe.
    //
    // Called from sInitializeWorld, NOT from InstallHooks. GlobalSNOGet resolves to a real
    // function pointer at boot but the SNO system behind it is not built yet, and calling it
    // there faults instantly at 0x4E5E50 -- the identical trap main.cpp already documents for
    // AllGBIDsOfType and the GB handle pool. A non-null function pointer is not readiness.
    inline void ReportPowerSnos() {
        static bool s_done = false;
        if (s_done || !global_config.rare_cheats.power_formula_probe)
            return;
        if (GlobalSNOGet == nullptr)
            return;
        s_done = true;
        PRINT("[d3hack-power] Community_Buff_NestingPortalSpawn (Vision of Enmity) sno = %d",
              static_cast<int>(GlobalSNOGet(static_cast<SNO>(0x61B))))
    }

    // d3hack-custom: one line per rift so the level the game thinks you are on is
    // always in the log next to whatever the drops and gem chance did.
    inline void ReportRiftLevel() {
        PRINT("[d3hack-custom] rift level = %d (ancient>=%d primal>=%d x%d)", TrueGRLevel(),
              global_config.loot_modifiers.AncientMinGRLevel,
              global_config.loot_modifiers.PrimalMinGRLevel,
              global_config.loot_modifiers.PrimalGuaranteedCount)
    }

    // d3hack-custom: will this specifier actually produce a legendary?
    //
    // AugmentSpecifier runs for EVERY loot roll, whites and rares included, so the
    // guaranteed-primal budget has to be spent only on rolls that can really become
    // one -- otherwise the first few commons eat the whole allowance and a single
    // legendary comes out primal.
    //
    // Stock LootRollForAncientLegendary asks the same question at 0x88DE40 with
    //     ldr w0, [x19, #8] ; bl 0x4F47E0
    // so read the GBID at that same raw byte offset rather than through the
    // LootSpecifier field names -- the header's layout does not line up with what the
    // 2.7.6 code actually indexes (its +0x1C and +0x28 accesses do not match the
    // declared fields either), and eAncientRank is the only offset in it proven correct.
    inline auto SpecifierIsLegendary(const LootSpecifier *tSpecifier) -> bool {
        if (tSpecifier == nullptr)
            return false;
        const auto gbidItem = *reinterpret_cast<const GBID *>(reinterpret_cast<const uint8 *>(tSpecifier) + 8);
        return LootItemIsLegendary(gbidItem) != 0;
    }

    // d3hack-custom: read an int attribute with param -1 off an ACD.
    inline auto ReadAttrInt(ActorCommonData *ptACD, int32 eAttrib) -> int {
        if (ptACD == nullptr)
            return -1;
        FastAttribKey tKey;
        tKey.nValue = eAttrib | (-1 << 12);
        return static_cast<int>(ACD_AttributesGetInt(ptACD, tKey));
    }

    // d3hack-custom: the greater-rift tier the looter is currently in, as a displayed
    // GR number. The game global is the only dependable source here: call site 0x88D334
    // passes idACDLooter = -1 outright when its actor pointer is null, and
    // LootSpecifier::tLooteeParams.nTieredLootRunLevel is -1 on the ancient-roll path,
    // so both of the obvious per-actor sources read as "not in a rift".
    inline auto LooterGRLevel(LootSpecifier *tSpecifier, ActorCommonData *ptACDLooter) -> int {
        const int nTrue = TrueGRLevel();
        if (nTrue > 0)
            return nTrue;
        const int nFromSpecifier = tSpecifier->tLooteeParams.nTieredLootRunLevel;
        if (nFromSpecifier > 0)
            return nFromSpecifier;
        return ReadAttrInt(ptACDLooter, IN_TIERED_LOOT_RUN_LEVEL);
    }

    // d3hack-custom: the game's own ancient/primal roll, handed in by the trampoline
    // hook so AugmentSpecifier can let every drop take its natural rank first.
    using StockAncientRollFn = void (*)(LootSpecifier *, ACDID);

    inline void AugmentSpecifier(LootSpecifier *tSpecifier, ActorCommonData *ptACDLooter,
                                 const ACDID idACDLooter, StockAncientRollFn pfnStockRoll) {
        // d3hack-custom: capture the real rift level BEFORE any forced override below.
        const int nRealGR = LooterGRLevel(tSpecifier, ptACDLooter);
        if (global_config.loot_modifiers.ForcedILevel > 0) {
            tSpecifier->tLooteeParams.nForcedILevel = global_config.loot_modifiers.ForcedILevel;
        }
        if (global_config.loot_modifiers.TieredLootRunLevel > 0) {
            tSpecifier->tLooteeParams.nTieredLootRunLevel = global_config.loot_modifiers.TieredLootRunLevel;
        }
        tSpecifier->tLooteeParams.bDisableAncientDrops       = static_cast<BOOL>(global_config.loot_modifiers.DisableAncientDrops);
        tSpecifier->tLooteeParams.bDisablePrimalAncientDrops = static_cast<BOOL>(global_config.loot_modifiers.DisablePrimalAncientDrops);
        tSpecifier->tLooteeParams.bDisableTormentDrops       = static_cast<BOOL>(global_config.loot_modifiers.DisableTormentDrops);
        tSpecifier->tLooteeParams.bDisableTormentCheck       = static_cast<BOOL>(global_config.loot_modifiers.DisableTormentCheck);
        tSpecifier->tLooteeParams.bSuppressGiftGeneration    = static_cast<BOOL>(global_config.loot_modifiers.SuppressGiftGeneration);

        ++g_nAncientRollsThisWorld;

        // d3hack-custom: RUN THE GAME'S OWN ANCIENT/PRIMAL ROLL FIRST, ALWAYS.
        //
        // This hook used to be a REPLACE, which deleted the stock roll outright: every
        // drop in the game then took its rank from AncientRankValue alone. With the
        // shipped AncientRank = "Normal" that is a hard 0, so ancients and primals
        // stopped appearing anywhere the GR escalation below did not fire -- normal
        // Nephalem rifts (no rift level at all) and every GR under AncientMinGRLevel.
        //
        // Everything after this point may only RAISE eAncientRank. The stock result is
        // the floor, never something we overwrite, so the untouched loot paths keep
        // exactly their vanilla drop rates.
        if (pfnStockRoll != nullptr)
            pfnStockRoll(tSpecifier, idACDLooter);

        const int nStockRank = tSpecifier->eAncientRank;

        // d3hack-custom: an explicit AncientRank setting is a floor on every drop.
        // "Normal" (0) means "no override" -- use DisableAncientDrops to suppress
        // ancients outright, which is the knob that actually gates the stock roll.
        if (global_config.loot_modifiers.AncientRankValue > tSpecifier->eAncientRank)
            tSpecifier->eAncientRank = global_config.loot_modifiers.AncientRankValue;

        // d3hack-custom: escalate drop rank with greater-rift tier.
        // AncientRank is 0=Normal 1=Ancient 2=Primal. Only ever raise it, so the stock
        // roll and an explicit AncientRank both still act as floors.
        const int nAncientAt = global_config.loot_modifiers.AncientMinGRLevel;
        const int nPrimalAt  = global_config.loot_modifiers.PrimalMinGRLevel;
        if (nRealGR > 0 && SpecifierIsLegendary(tSpecifier)) {
            if (nAncientAt > 0 && nRealGR >= nAncientAt && tSpecifier->eAncientRank < 1)
                tSpecifier->eAncientRank = 1;
            // Only the first N drops of a rift are forced primal. The rest keep the
            // Ancient floor above, so salvage material income is unaffected.
            // A primal the stock roll produced on its own does not spend the budget.
            if (nPrimalAt > 0 && nRealGR >= nPrimalAt &&
                g_nPrimalsThisWorld < global_config.loot_modifiers.PrimalGuaranteedCount &&
                tSpecifier->eAncientRank < 2) {
                tSpecifier->eAncientRank = 2;
                ++g_nPrimalsThisWorld;
                PRINT("[d3hack-custom] primal %d/%d granted at GR %d", g_nPrimalsThisWorld,
                      global_config.loot_modifiers.PrimalGuaranteedCount, nRealGR)
            }
        }

        if (tSpecifier->eAncientRank != nStockRank)
            ++g_nAncientRaisedThisWorld;
        else
            ++g_nAncientStockThisWorld;
    }

    // d3hack-custom: report the loot-roll tally for the world just finished, then clear
    // it along with the per-rift primal budget. Called from sInitializeWorld, so it fires
    // on every world change rather than only when something rare drops.
    //
    // "stock-untouched" going to 0 in a normal rift is the signal that this hook has
    // started overriding loot paths it was supposed to leave alone.
    // d3hack-custom: set once the first world is live. Boot-time noise is the reason
    // three probes in a row reported a starved budget as a negative result.
    inline bool g_bWorldEntered = false;

    // d3hack-custom: deferred camera apply -- see the note in EliteEventProbe.
    // d3hack-custom: the player's live world position, taken from the ACD (+0x60/64/68).
    // Declared ahead of the scanner because the scanner's height filter needs the z.
    inline float s_flLastPx = 0.0f;
    inline float s_flLastPy = 0.0f;
    inline float s_flLastPz = 0.0f;

    // d3hack-custom: the live camera, once the scanner has identified it.
    //   eye    = vec4 at s_uCamEye       (x, y, z, w=1)
    //   target = vec4 at s_uCamEye-0x20  (the look-at, w~1)
    // eye - target measured (+48.0, +48.0, +50.1): equal x/y and similar z, a 45 degree
    // isometric camera ~84 units back. Scaling that vector IS the zoom.
    inline uintptr_t s_uCamEye = 0ull;
    // The offset AS IT WAS AT LOCK TIME. Every write recomputes from this, never from the
    // current eye -- reading the live value meant multiplying an already-multiplied offset
    // (84379 -> 421898 -> 2109494 in the logs), which runs away toward infinity and is the
    // prime suspect for the InvalidAccessHandler crash.
    inline float s_flOrigOx = 0.0f;
    inline float s_flOrigOy = 0.0f;
    inline float s_flOrigOz = 0.0f;

    // d3hack-custom: HEAP SCANNER for the camera.
    //
    // Established with a working control: the camera is NOT in the Player struct (8 KB
    // scanned, scan proven to run). It is a renderer-side object on the heap, so the only
    // way left is the one the user suggested at the start -- a value scan. Done in-process
    // rather than from Cheat Engine, because in here the player's live position is already
    // in hand to use as the search key.
    //
    // svcQueryMemory walks the address space and reports each region's state and
    // permissions, so mapped, readable heap can be scanned without ever touching an
    // unmapped page. mem_layout.cpp already uses it the same way.
    //
    // Two stages:
    //   1. SWEEP  -- find addresses where two consecutive floats sit within 250 units of
    //                the player's (x, y) but are NOT the player's own position. The camera
    //                trails the player, so it lives in exactly that band. Bounded to 512
    //                candidates and a slice of memory per pass so the game keeps running.
    //   2. VERIFY -- once the player has moved, re-read only those candidates and keep the
    //                ones that moved in the same direction. Lag-tolerant: the camera eases,
    //                so direction is required but magnitude only has to be in the ballpark.
    struct CamCand {
        uintptr_t uAddr;
        float     flX;
        float     flY;
    };

    inline CamCand  s_arCand[2048] {};
    inline int      s_nCand      = 0;
    inline bool     s_bSweepDone = false;
    // Start at 0 and let svcQueryMemory walk. The first version started the cursor at
    // 0x8000000000 (~549 GB) while every real heap pointer seen tonight is ~0x10E5B13220
    // (~72 GB) -- so it swept empty space ABOVE the heap, then hit a query failure and
    // stopped dead without printing anything. Unmapped space costs one query per region,
    // so walking from zero is cheap.
    inline uintptr_t s_uScanCur  = 0ull;
    inline float    s_flSweepPx  = 0.0f;
    inline float    s_flSweepPy  = 0.0f;

    // NaN/Inf test that -ffast-math cannot optimise away: exponent all ones.
    inline auto FloatIsSane(const void *p) -> bool {
        u32 u;
        __builtin_memcpy(&u, p, 4);
        return (u & 0x7F800000u) != 0x7F800000u;
    }

    // d3hack-custom: OBSERVER ASSET SCAN -- where the camera offset actually comes from.
    //
    // Every value-scanning approach failed to find the SOURCE of the eye offset: it is
    // provably not in .text, .rodata, .data, not within +/-64 KB of the camera in either
    // cartesian or polar form, and not reachable from any .data pointer. So it is loaded
    // data -- and the format was recovered from the PC build rather than guessed at.
    //
    // The DiIiS PC server emulator in the sibling tree ships a parser for D3 .obs
    // (Observer) assets, and the game archives sit next to it. Decoded against 105 real
    // assets, an Observer is 108 bytes:
    //
    //     +0x00 0xDEADBEEF magic     +0x04 57 = SNO type tag (Observer)
    //     +0x10 SNO id
    //     +0x24 Angle0 = FOV (rad)
    //     +0x34 Angle1 = azimuth/yaw (rad)   +0x38 Angle2 = elevation (rad)
    //     +0x3C F2     = CAMERA DISTANCE     +0x40 V0     = eye offset (x, y, z)
    //
    // |V0| == F2 holds exactly on 104 of the 105 (the exception has F2 = 0). V0 is the
    // spherical-to-cartesian expansion of (distance, yaw, elevation), stored precomputed.
    // The PC gameplay camera is dist 98 / yaw 45 / elev 42.2, and Blizzard own pulled-back
    // variant `observerRetro` is the SAME angles at dist 350. Their zoom is a scalar on
    // these fields, which is exactly what is wanted here.
    //
    // This session Switch camera measured eye - target = (48.005, 48.005, 50.108),
    // magnitude 84.379, azimuth 45 degrees. That IS a V0/F2 pair in this layout, and it
    // explains the behaviour that blocked every previous attempt: writing the eye worked,
    // but the game re-expanded the offset from the asset on the very next frame. Patch the
    // asset and the game computes the pulled-back camera itself -- no per-frame race, no
    // runaway multiplication, nothing to maintain.
    //
    // TWO INDEPENDENT FINDERS, because it is not yet known whether the Switch keeps the
    // raw file image or a repacked runtime struct:
    //   1. MAGIC -- the exact u32 pair (0xDEADBEEF, 57). Two integer compares, no false
    //      positives worth worrying about.
    //   2. SHAPE -- four consecutive floats where the first equals the magnitude of the
    //      next three. Header-independent, so it still finds the data if the loader strips
    //      or rewrites the header. Random memory does not satisfy it.
    // If magic reports zero and shape reports hits, the loader repacks. If both report
    // zero, the Switch stores the camera some other way and this line of attack is dead --
    // which is itself an answer, rather than the silence earlier probes produced.
    struct ObsHit {
        uintptr_t uAddr;
        u32       uSnoId;
        float     flDist;
        float     flX;
        float     flY;
        float     flZ;
        bool      bMagic;
        bool      bWritable;
        bool      bValid;
        bool      bPatched;
    };

    inline ObsHit    s_arObs[256] {};
    inline int       s_nObs         = 0;
    inline int       s_nObsMagic    = 0;
    inline int       s_nObsShape    = 0;
    inline int       s_nObsPrint    = 0;
    inline bool      s_bObsScanDone = false;
    inline uintptr_t s_uObsCur      = 0ull;
    inline u64       s_uObsTick     = 0ull;
    // A LIVE heap pointer to anchor the sweep on. The first version walked up from 0 and
    // reached ~170 MB in a whole session while the actors sit at ~0x10E6146C80 (72 GB) --
    // it was never going to arrive. Anchoring is not an optimisation here, it is the
    // difference between scanning the right arena and scanning nothing that matters.
    inline uintptr_t s_uObsAnchor   = 0ull;
    inline uintptr_t s_uObsLo       = 0ull;
    inline uintptr_t s_uObsHi       = 0ull;
    inline int       s_nObsPass     = 0;
    inline int       s_nObsRegion   = 0;
    // Ingredient-hunt state. Declared with the rest so ObsApplyZoom, which is defined
    // above the helpers, can report the counter.
    inline u32   s_arIngrLo[4] {};
    inline u32   s_arIngrHi[4] {};
    inline int   s_nObsIngr = 0;
    inline int   s_nObsIngrPatched = 0;
    // d3hack-custom: the live projection-matrix arena, and the STOCK values to recognise.
    // Re-applying is keyed on "currently equals stock" so a matrix can never be divided
    // twice -- compounding is what sent the old camera-eye write to infinity.
    inline uintptr_t s_uProjLo    = 0ull;
    inline uintptr_t s_uProjHi    = 0ull;
    inline float     s_flProjM00  = 0.0f;
    inline float     s_flProjM11  = 0.0f;
    inline u64       s_uProjTick  = 0ull;
    inline int       s_nProjHits  = 0;
    inline bool      s_bProjSaid  = false;
    // The exact matrix addresses. Re-applying by ADDRESS instead of rescanning a window
    // is what lets this run on every hook call rather than at a throttled 250 Hz -- and the
    // throttle is why it flickers: the game rebuilt the matrix in the gaps between passes.
    inline uintptr_t s_arProjSlot[16] {};
    inline int       s_nProjSlot = 0;
    inline int       s_nFovHits  = 0;
    inline bool  s_bIngrReady = false;


    // This file has no %f, so floats print as <whole>.<thousandths>.
    inline auto ObsWhole(float v) -> int { return static_cast<int>(v); }
    inline auto ObsMilli(float v) -> int {
        const float a = (v < 0.0f) ? -v : v;
        return static_cast<int>(a * 1000.0f) % 1000;
    }

    inline auto ObsSeen(uintptr_t uBase) -> bool {
        for (int i = 0; i < s_nObs; ++i)
            if (s_arObs[i].uAddr == uBase)
                return true;
        return false;
    }

    // Report one Observer, and scale it if it is the gameplay camera and a zoom is set.
    // Patching is recorded per address so a later pass can never scale the same asset
    // twice -- compounding is what sent the old per-frame write toward infinity.
    inline void ObsHandle(uintptr_t uBase, bool bMagic, bool bWritable) {
        if (s_nObs >= 256 || ObsSeen(uBase))
            return;

        auto       *pf = reinterpret_cast<float *>(uBase);
        const auto *pu = reinterpret_cast<const u32 *>(uBase);
        for (int i = 9; i <= 18; ++i)
            if (!FloatIsSane(&pf[i]))
                return;

        const u32   uId    = bMagic ? pu[4] : 0u;
        const float flFov  = pf[9] * 57.29578f;
        const float flYaw  = pf[13] * 57.29578f;
        const float flElev = pf[14] * 57.29578f;
        const float flDist = pf[15];
        const float x = pf[16], y = pf[17], z = pf[18];

        // The invariant that proves the block really is an Observer.
        const float flMag2 = x * x + y * y + z * z;
        const float flTol  = (flDist > 1.0f) ? (flDist * 0.01f) : 0.01f;
        const bool  bValid = flDist > 1.0f && flDist < 100000.0f &&
                             flMag2 > (flDist - flTol) * (flDist - flTol) &&
                             flMag2 < (flDist + flTol) * (flDist + flTol);

        const int nSlot           = s_nObs++;
        s_arObs[nSlot].uAddr      = uBase;
        s_arObs[nSlot].uSnoId     = uId;
        s_arObs[nSlot].flDist     = flDist;
        s_arObs[nSlot].flX        = x;
        s_arObs[nSlot].flY        = y;
        s_arObs[nSlot].flZ        = z;
        s_arObs[nSlot].bMagic     = bMagic;
        s_arObs[nSlot].bWritable  = bWritable;
        s_arObs[nSlot].bValid     = bValid;
        s_arObs[nSlot].bPatched   = false;

        if (s_nObsPrint < 120) {
            ++s_nObsPrint;
            PRINT("[d3hack-obs] %s %p sno=%u fov=%d.%03d yaw=%d.%03d elev=%d.%03d "
                  "dist=%d.%03d V0=(%d.%03d, %d.%03d, %d.%03d) %s%s",
                  bMagic ? "asset" : "shape", reinterpret_cast<void *>(uBase), uId,
                  ObsWhole(flFov), ObsMilli(flFov), ObsWhole(flYaw), ObsMilli(flYaw),
                  ObsWhole(flElev), ObsMilli(flElev), ObsWhole(flDist), ObsMilli(flDist),
                  ObsWhole(x), ObsMilli(x), ObsWhole(y), ObsMilli(y), ObsWhole(z), ObsMilli(z),
                  bValid ? "ok" : "INVALID", bWritable ? "" : " READ-ONLY")
        }

    }

    // d3hack-custom: apply the zoom ONCE, after the scan has finished.
    //
    // Deliberately not done as candidates are found. The shape finder is header-less by
    // design, so it can in principle match a transform that merely looks like an Observer;
    // patching mid-scan would write into it before there is any way to know that a real
    // magic-matched asset exists elsewhere. With the whole table in hand the choice is
    // easy: if ANY asset carried the 0xDEADBEEF header, trust only those and ignore every
    // shape guess. Shape hits are used only when the header search came up empty, which is
    // the case where the loader must be repacking the struct.
    inline void ObsApplyZoom() {
        const float k = global_config.rare_cheats.camera_zoom;
        if (k == 0.0f || k == 1.0f)
            return;

        const bool bHaveMagic = (s_nObsMagic > 0);
        const int  nWantId    = global_config.rare_cheats.camera_observer_id;
        int        nDone      = 0;

        for (int i = 0; i < s_nObs; ++i) {
            ObsHit &t = s_arObs[i];
            if (t.bPatched || !t.bValid)
                continue;
            if (bHaveMagic && !t.bMagic)
                continue;

            // Targeting. An explicit id from the log wins. Otherwise take the isometric
            // gameplay family: equal x and y (a 45 degree azimuth) at a gameplay distance.
            // Menu, portrait, pet and store observers all sit outside that band -- in the
            // PC dump they run 15..155 with lopsided x/y.
            // Fingerprint hits are already filtered to the gameplay-camera shape, so the
            // distance-band rule only applies to full .obs images.
            if (t.bMagic) {
                const float flDxy = (t.flX > t.flY) ? (t.flX - t.flY) : (t.flY - t.flX);
                const bool  bTarget =
                    (nWantId != 0)
                        ? (t.uSnoId == static_cast<u32>(nWantId))
                        : (flDxy < 0.05f && t.flX > 1.0f && t.flDist > 40.0f && t.flDist < 250.0f);
                if (!bTarget)
                    continue;
            } else if (nDone >= 4) {
                // Bound the blind writes. If eight fingerprint matches all miss, the answer
                // is in the log, not in scribbling over more of the heap.
                continue;
            }
            if (!t.bWritable) {
                PRINT("[d3hack-obs] sno=%u at %p is a target but the page is READ-ONLY%s",
                      t.uSnoId, reinterpret_cast<void *>(t.uAddr), "")
                continue;
            }

            auto *pf = reinterpret_cast<float *>(t.uAddr);
            if (t.bMagic) {
                // A real .obs image: distance at +0x3C, offset vector at +0x40.
                pf[15] = t.flDist * k;
                pf[16] = t.flX * k;
                pf[17] = t.flY * k;
                pf[18] = t.flZ * k;
            } else {
                // A bare offset triple found by fingerprint. uAddr points AT the vector;
                // the magnitude, when there is one, sits immediately in front of it.
                pf[0] = t.flX * k;
                pf[1] = t.flY * k;
                pf[2] = t.flZ * k;
                if (t.flDist > 0.0f)
                    pf[-1] = t.flDist * k;
            }
            t.bPatched = true;
            ++nDone;
            PRINT("[d3hack-obs] PATCHED sno=%u dist %d.%03d -> %d.%03d, V0.z %d.%03d -> %d.%03d",
                  t.uSnoId, ObsWhole(t.flDist), ObsMilli(t.flDist), ObsWhole(pf[15]),
                  ObsMilli(pf[15]), ObsWhole(t.flZ), ObsMilli(t.flZ), ObsWhole(pf[18]),
                  ObsMilli(pf[18]))
        }

        PRINT("[d3hack-obs] zoom x%d.%03d applied to %d target(s); source = %s",
              ObsWhole(k), ObsMilli(k), nDone,
              bHaveMagic ? "header-matched .obs assets" : "offset fingerprints")
        if (nDone == 0) {
            PRINT("[d3hack-obs] the camera offset (%d.%03d, %d.%03d, %d.%03d) is stored NOWHERE "
                  "in the swept band -- it is computed, not loaded",
                  ObsWhole(s_flOrigOx), ObsMilli(s_flOrigOx), ObsWhole(s_flOrigOy),
                  ObsMilli(s_flOrigOy), ObsWhole(s_flOrigOz), ObsMilli(s_flOrigOz))
            if (s_nFovHits == 0)
                PRINT("[d3hack-fov] band complete: the 35deg FOV is NOT stored anywhere as a "
                      "float -- it is computed, like the camera offset before it%s", "")
            PRINT("[d3hack-proj] game matrices found: %d, arena %p..%p (%uKB), stock m11=%d.%03d",
                  s_nObsShape, reinterpret_cast<void *>(s_uProjLo),
                  reinterpret_cast<void *>(s_uProjHi),
                  static_cast<u32>((s_uProjHi - s_uProjLo) >> 10), ObsWhole(s_flProjM11),
                  ObsMilli(s_flProjM11))
        }
    }

    // d3hack-custom: the values the camera offset is COMPUTED FROM.
    //
    // Built once, from the locked camera, so nothing here is a guess carried over from a
    // previous session. For (48.005, 48.005, 50.108) these come out as:
    //     distance   84.379   horizontal 67.889
    //     direction  0.568918 (x and y)  0.593840 (z)
    // A relative window of 2e-5 maps to a contiguous u32 range because IEEE bit patterns
    // are monotonic for positive floats -- so the test stays two integer compares.
    inline void ObsBuildIngredients() {
        if (s_bIngrReady || s_uCamEye == 0ull)
            return;
        const float ox = s_flOrigOx, oy = s_flOrigOy, oz = s_flOrigOz;
        const float mag = __builtin_sqrtf(ox * ox + oy * oy + oz * oz);
        const float hor = __builtin_sqrtf(ox * ox + oy * oy);
        if (!(mag > 1.0f))
            return;
        // Only the two magnitudes. The normalised direction (0.5689 / 0.5938) produced
        // 101187 hits in one sweep -- 37 of the first 40 printed were dir.z, sitting in
        // smooth ramps and in sin/cos tables where 0.593^2 + 0.803^2 = 1. Values around
        // 0.5 are simply everywhere in a heap. The distance is not: 84.379 hit ONCE.
        const float arV[2] = { mag, hor };
        for (int i = 0; i < 2; ++i) {
            const float lo = arV[i] * 0.99998f;
            const float hi = arV[i] * 1.00002f;
            __builtin_memcpy(&s_arIngrLo[i], &lo, 4);
            __builtin_memcpy(&s_arIngrHi[i], &hi, 4);
        }
        s_bIngrReady = true;
        PRINT("[d3hack-obs] hunting: camera distance %d.%03d, horizontal reach %d.%03d",
              ObsWhole(arV[0]), ObsMilli(arV[0]), ObsWhole(arV[1]), ObsMilli(arV[1]))
    }

    // -1 = no match, 0 = camera distance, 1 = horizontal reach.
    inline auto ObsIngredientWhich(u32 u) -> int {
        if (!s_bIngrReady)
            return -1;
        for (int i = 0; i < 2; ++i)
            if (u >= s_arIngrLo[i] && u <= s_arIngrHi[i])
                return i;
        return -1;
    }

    // d3hack-custom: THE WRITE TRAP -- stop hunting the value, catch the code.
    //
    // Everything that could be searched for has been, and all of it came back empty:
    //   - the offset triple (48.005, 48.005, 50.108), matched BITWISE across 4 GB: 0 hits
    //   - its distance 84.379 and reach 67.890: 5 hits, none of them structurally a camera
    //     (no angles, no FOV, and +-FLT_MAX sentinels beside them -- bounding boxes, and
    //     scaling them changed nothing on screen, so they are outputs, not inputs)
    //   - the 0xDEADBEEF .obs header the PC build uses: 0 hits, so no asset images either
    // The camera offset is computed every frame and never stored. There is nothing left to
    // find by looking at data.
    //
    // So look at the instruction instead. The camera object IS located (locked by
    // fingerprint in under a second) and the game rewrites its eye every frame. Make that
    // page read-only and the write faults, and the fault carries the one fact that has been
    // missing all along: the address of the code doing it.
    //
    // This kills the game on purpose. That is fine -- exception_handler.cpp already dumps
    // pc, lr and far, resolves pc to module+offset, writes
    // sd:/config/d3hack-nx/user_exception.txt AND prints to the log. A crash that names the
    // function is a complete result, so no fault recovery is needed.
    //
    // Read `far` first. A 4 KB page holds more than the camera, so if far is not the eye
    // (or within its vec4) the trap caught an innocent neighbour and it is worth one more
    // run -- do not read the pc as the camera writer until far confirms it.
    // d3hack-custom: WHO OWNS THE CAMERA.
    //
    // The write trap is dead: Ryujinx does not enforce guest page permissions. Proved with
    // a control -- the page was set read-only, the game played on without faulting, and
    // then the mod wrote to the page itself and that survived too. Nothing will fault here,
    // so no amount of protecting will ever name the writer.
    //
    // What the same run DID establish is that the object is genuinely the live camera:
    //     sample 3: target (349, 327, 0) eye (397, 375, 50) player (350, 329, 0)
    //     sample 5: target (290, 272, 0) eye (338, 320, 50) player (294, 279, 0)
    // The target sits on the player and follows as you walk; the eye is always target +
    // (48, 48, 50). So the address was never wrong -- writes land, and the game recomputes
    // the value before the frame is drawn. Most likely it builds the VIEW MATRIX during the
    // camera update, which would mean writing the eye afterwards can never matter however
    // late it is done. That fits every observation so far: the number changes, the picture
    // never does.
    //
    // So stop poking the value and identify the code. Two things, neither of which has been
    // tried, and both cheap:
    //   1. DUMP the object. Every session so far searched memory AROUND the camera for
    //      values without ever simply reading the struct it lives in. A vtable pointer near
    //      the start resolves to a module+offset and hands over the class outright.
    //   2. Find POINTERS to it. Anything holding this address is its owner; if one of them
    //      lives in the main module's static data, that global can be xref'd offline in the
    //      binary and the camera code falls out of it.
    inline int  s_nOwnPhase  = 0;
    inline u64  s_uOwnTick   = 0ull;
    inline int  s_nOwnHits   = 0;
    inline uintptr_t s_uOwnBase = 0ull;
    inline int  s_nOwnRefDump = 0;

    // Print a pointer, and say where it points if that is somewhere known.
    inline void OwnDescribe(const char *tag, int nOff, u64 uVal) {
        const auto *pMod = exl::util::TryGetModule(static_cast<uintptr_t>(uVal));
        if (pMod != nullptr) {
            const auto svName = pMod->GetModuleName();
            PRINT("[d3hack-own] %s %+4d: %016llx  -> %.*s + 0x%llX  <<< CODE/DATA POINTER", tag,
                  nOff, static_cast<unsigned long long>(uVal), static_cast<int>(svName.size()),
                  svName.data(),
                  static_cast<unsigned long long>(static_cast<uintptr_t>(uVal) -
                                                  pMod->m_Total.m_Start))
            return;
        }
        PRINT("[d3hack-own] %s %+4d: %016llx", tag, nOff, static_cast<unsigned long long>(uVal))
    }

    // Dump `nRadius` bytes either side of an address as 64-bit words. A pointer that
    // resolves inside a loaded module is called out -- that is the one worth chasing,
    // because a static address can be xref'd in the binary offline.
    inline void OwnDumpAround(uintptr_t uCenter, const char *tag, int nRadius) {
        for (int off = -nRadius; off < nRadius; off += 8) {
            const auto uAt = static_cast<uintptr_t>(uCenter + off);
            if (uAt < 0x10000ull)
                continue;
            u64 v;
            __builtin_memcpy(&v, reinterpret_cast<const void *>(uAt), 8);
            if (v == 0ull)
                continue;
            // Anything in the guest pointer range gets the module lookup; the rest is far
            // more likely to be two floats, so show it that way instead.
            if (v > 0x0000001000000000ull && v < 0x0000200000000000ull) {
                OwnDescribe(tag, off, v);
            } else {
                float f0, f1;
                __builtin_memcpy(&f0, reinterpret_cast<const void *>(uAt), 4);
                __builtin_memcpy(&f1, reinterpret_cast<const void *>(uAt + 4), 4);
                if (FloatIsSane(&f0) && FloatIsSane(&f1))
                    PRINT("[d3hack-own] %s %+4d: %016llx   = %d.%03d, %d.%03d", tag, off,
                          static_cast<unsigned long long>(v), ObsWhole(f0), ObsMilli(f0),
                          ObsWhole(f1), ObsMilli(f1))
                else
                    PRINT("[d3hack-own] %s %+4d: %016llx", tag, off,
                          static_cast<unsigned long long>(v))
            }
        }
    }

    inline void CameraOwnerProbe() {
        if (s_uCamEye == 0ull || s_nOwnPhase >= 2)
            return;
        const u64 uNow = svcGetSystemTick();
        if (s_uOwnTick == 0ull) {
            s_uOwnTick = uNow;
            return;
        }
        if ((uNow - s_uOwnTick) < (3ull * d3::pools::kTicksPerSecond))
            return;

        if (s_nOwnPhase == 0) {
            s_nOwnPhase = 1;
            s_uOwnBase  = s_uCamEye - 0x20;      // the target vec4 starts the pair
            PRINT("[d3hack-own] camera eye %p, target %p -- dumping the struct",
                  reinterpret_cast<void *>(s_uCamEye), reinterpret_cast<void *>(s_uOwnBase))

            OwnDumpAround(s_uOwnBase, "obj", 0x100);
            PRINT("[d3hack-own] struct dumped; sweeping for pointers to it%s", "")
            return;
        }
    }

    // d3hack-custom: KEEP the widened FOV, every frame.
    //
    // Widening the projection matrix once produced a single visibly-changed frame and then
    // reverted -- so unlike the camera eye, a write here DOES reach the screen; the matrix
    // is simply rebuilt each frame. That makes it a race we can win, because this hook fires
    // far more often than 60 Hz.
    //
    // Idempotent by construction: a matrix is only rewritten while it still holds the STOCK
    // value, so a slot can never be divided twice however often this runs. That is the same
    // discipline the loot floors use, and the reason the old eye write ran away to infinity
    // (84379 -> 421898 -> 2109494) was precisely that it recomputed from the current value.
    inline void ProjectionReapply() {
        const float k = global_config.rare_cheats.camera_zoom;
        if (k <= 1.0f || s_nProjSlot == 0 || s_flProjM11 <= 0.0f)
            return;

        const float flWant00 = s_flProjM00 / k;
        const float flWant11 = s_flProjM11 / k;

        for (int i = 0; i < s_nProjSlot; ++i) {
            const auto  a  = s_arProjSlot[i];
            const auto *pu = reinterpret_cast<const u32 *>(a);
            // Re-verify the fingerprint every time: if this buffer has been recycled for
            // something else, writing into it would corrupt whatever now lives there.
            if (pu[1] || pu[2] || pu[3] || pu[4] || pu[6] || pu[7] || pu[8] || pu[9] ||
                pu[12] || pu[13] || pu[15])
                continue;
            if (pu[11] != 0xBF800000u && pu[11] != 0x3F800000u)
                continue;
            auto *pm = reinterpret_cast<float *>(a);
            if (!FloatIsSane(&pm[0]) || !FloatIsSane(&pm[5]))
                continue;
            // Only ever rewrite a slot that still holds the STOCK value. That makes this
            // idempotent however often it runs, so no amount of repetition can compound --
            // the mistake the old camera-eye write made when it recomputed from the current
            // value and ran 84379 -> 421898 -> 2109494.
            const float d0 = pm[0] - s_flProjM00;
            const float d1 = pm[5] - s_flProjM11;
            if (d0 > 0.004f || d0 < -0.004f || d1 > 0.004f || d1 < -0.004f)
                continue;
            pm[0] = flWant00;
            pm[5] = flWant11;
            ++s_nProjHits;
            if (!s_bProjSaid) {
                s_bProjSaid = true;
                PRINT("[d3hack-proj] holding widened FOV on %d slot(s): m11 %d.%03d -> %d.%03d (x%d.%03d)",
                      s_nProjSlot, ObsWhole(s_flProjM11), ObsMilli(s_flProjM11),
                      ObsWhole(flWant11), ObsMilli(flWant11), ObsWhole(k), ObsMilli(k))
            }
        }
    }

    // d3hack-custom: THE SCAN RUNS ON ITS OWN THREAD.
    //
    // Locating anything in a multi-gigabyte heap costs real time, and spending it on the
    // game thread is what took town to ~0.3 fps. Lowering the slice did not help: it still
    // stole frames and pushed the time-to-find out to minutes, which is unshippable -- a
    // feature that takes that long to engage is indistinguishable from a broken one.
    //
    // A worker thread fixes both at once. The OS schedules it around the game, the frame
    // cost disappears, and the slice can go back up so the arena is found in seconds.
    // Everything it touches is already safe for this: svcQueryMemory validates each region
    // before it is read, and the writes are idempotent (a slot is only rewritten while it
    // still holds the stock value), so racing the game thread cannot compound anything.
    alignas(0x1000) inline u8 g_scanStack[0x8000] {};
    // Opaque storage rather than a real nn::os::ThreadType. Including os_thread_type.hpp
    // pulls in the condition-variable headers, which do not compile standalone in this SDK
    // reimplementation, and the api header only forward-declares the type. Over-sized and
    // aligned is all that is actually required of us.
    alignas(16) inline u8     g_scanThreadStorage[0x400] {};
    inline bool               g_bScanThreadUp = false;

    inline auto ScanThreadPtr() -> nn::os::ThreadType * {
        return reinterpret_cast<nn::os::ThreadType *>(g_scanThreadStorage);
    }

    // d3hack-custom: THE VIEW MATRIX BUILDER -- 0x29B6xx.
    //
    // Found by static analysis rather than scanning, from the exact matrix the game was
    // observed producing. Its tail is unmistakable:
    //
    //     fneg s3 ; str s3, [x19, #0x30]    -dot(right,   eye)
    //     fneg s0 ; str s0, [x19, #0x34]    -dot(up,      eye)
    //               str s0, [x19, #0x38]     dot(forward, eye)
    //     mov w8, #0x3f800000 ; str w8, [x19, #0x3c]      1.0
    //
    // That is the translation row of a view matrix: the camera position expressed along its
    // own basis. +0x38 is therefore the distance along the VIEW AXIS, and adding to it
    // dollies the camera straight back -- a real pull-back, not the wider lens the
    // projection edit gives.
    //
    // Why this finally escapes the race that beat every previous attempt: the camera eye,
    // the offset and the projection matrices all live in per-frame buffers, so writing them
    // is a fight with whatever rebuilds them (the arena address even moved mid-session,
    // 0x10EB12C3A0 -> 0x10EAF0D0C0). Here the value is changed at the moment it is COMPUTED,
    // before anything downstream reads it. Nothing to out-run.
    //
    // Hooked one instruction AFTER the store, where x19 still holds the matrix, so the
    // general-register context is enough and no float-register plumbing is needed.
    // d3hack-custom: THE CAMERA SOURCE -- SetCameraEyeTarget, 0x29B4D0.
    //
    //     adrp x9, #0x1150000 ; ldr x9,[x9,#0x470] ; ldr x9,[x9]
    //     ldr w8,[x0]   ; str w8,[x9,#0x630]      eye    <- arg0 (vec3)
    //     ldr w8,[x0,#4]; str w8,[x9,#0x634]
    //     ldr w8,[x0,#8]; str w8,[x9,#0x638]
    //     ldr w8,[x1]   ; str w8,[x9,#0x63c]      target <- arg1 (vec3)
    //     ...
    // with the neighbouring accessors confirming the layout:
    //     0x29B510 GetCameraEye()    -> global+0x630
    //     0x29B530 GetCameraTarget() -> global+0x63C
    //
    // This is upstream of EVERYTHING. The renderer, the culling frustum and input
    // unprojection all read the camera through those getters, so scaling the eye here keeps
    // all three in agreement. That is exactly what the view-matrix dolly at 0x29B754 could
    // not do: it moved the picture only, leaving culling to drop geometry at the screen
    // edges and input to unproject through the old camera (which is why walking south
    // fought the stick -- toward the camera is where that ray error inverts).
    //
    // The caller's vec3 is left untouched; x0 is redirected to our own scaled copy instead.
    inline float s_arScaledEye[4] {};
    inline int   s_nEyeLog = 0;
    struct EyeSig {
        u32 uCaller;
        int nMag, nX, nY, nZ;
        u32 uCount;
    };
    inline EyeSig s_arEyeSig[16] {};
    inline int    s_nEyeSig    = 0;
    inline u32    s_nEyeScaled = 0;
    inline u64    s_uEyeTick   = 0ull;

    HOOK_DEFINE_INLINE(CameraEyeSet) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            const auto uEye = static_cast<uintptr_t>(ctx->X[0]);
            const auto uTgt = static_cast<uintptr_t>(ctx->X[1]);
            if (uEye < 0x10000ull || uTgt < 0x10000ull)
                return;
            const auto *pE = reinterpret_cast<const float *>(uEye);
            const auto *pT = reinterpret_cast<const float *>(uTgt);
            for (int i = 0; i < 3; ++i)
                if (!FloatIsSane(&pE[i]) || !FloatIsSane(&pT[i]))
                    return;

            const float ox = pE[0] - pT[0];
            const float oy = pE[1] - pT[1];
            const float oz = pE[2] - pT[2];

            // CENSUS KEYED ON THE CALLER.
            //
            // 0x29B4D0 is NOT the camera setter. The linker has FOLDED it: identical machine
            // code shared by every "store two vec3s into an object" function in the binary.
            // The census proved it -- the values coming through are colours:
            //     (255,255,249)  (63,63,63)  (254,255,255)  (1,1,1)
            // and 389865 of them got "scaled", which is the weird lighting. The neighbouring
            // GetCameraEye/GetCameraTarget accessors made it look camera-specific; that was
            // reading intent into an accessor pattern.
            //
            // Identical code cannot be told apart by what it does, only by WHO CALLS IT. The
            // hook sits on the function's first instruction, before anything is pushed, so
            // x30 still holds the caller's return address. Bucket on that and the camera's
            // call site separates itself from the colour setters immediately.
            if (global_config.rare_cheats.camera_dist_log) {
                const uintptr_t uBase = exl::util::modules::GetTargetStart();
                const uintptr_t uLr   = static_cast<uintptr_t>(ctx->X[30]);
                const u32 uCaller = (uLr > uBase) ? static_cast<u32>(uLr - uBase) : 0u;
                const int nMag = static_cast<int>(__builtin_sqrtf(ox * ox + oy * oy + oz * oz));
                bool bF = false;
                for (int i = 0; i < s_nEyeSig; ++i) {
                    if (s_arEyeSig[i].uCaller == uCaller) {
                        ++s_arEyeSig[i].uCount;
                        s_arEyeSig[i].nMag = nMag;
                        s_arEyeSig[i].nX = static_cast<int>(ox);
                        s_arEyeSig[i].nY = static_cast<int>(oy);
                        s_arEyeSig[i].nZ = static_cast<int>(oz);
                        bF = true;
                        break;
                    }
                }
                if (!bF && s_nEyeSig < 16) {
                    s_arEyeSig[s_nEyeSig].uCaller = uCaller;
                    s_arEyeSig[s_nEyeSig].nMag    = nMag;
                    s_arEyeSig[s_nEyeSig].nX      = static_cast<int>(ox);
                    s_arEyeSig[s_nEyeSig].nY      = static_cast<int>(oy);
                    s_arEyeSig[s_nEyeSig].nZ      = static_cast<int>(oz);
                    s_arEyeSig[s_nEyeSig].uCount  = 1;
                    ++s_nEyeSig;
                }
                const u64 uNow = svcGetSystemTick();
                if (s_uEyeTick == 0ull ||
                    (uNow - s_uEyeTick) > (5ull * d3::pools::kTicksPerSecond)) {
                    s_uEyeTick = uNow;
                    PRINT("[d3hack-eye] --- callers (%d sites) ---", s_nEyeSig)
                    for (int i = 0; i < s_nEyeSig; ++i)
                        PRINT("[d3hack-eye]   caller 0x%06X  last(%d, %d, %d) |off|=%-5d seen %u",
                              s_arEyeSig[i].uCaller, s_arEyeSig[i].nX, s_arEyeSig[i].nY,
                              s_arEyeSig[i].nZ, s_arEyeSig[i].nMag, s_arEyeSig[i].uCount)
                    s_nEyeSig = 0;
                }
            }

            const float k = global_config.rare_cheats.camera_dist_scale;
            if (k <= 0.0f || k == 1.0f)
                return;
            // Only the configured call site. A value-shaped filter cannot work on folded
            // code -- a colour of (63, 63, 63) is "isometric" by any x==y test.
            const u32 uWant = static_cast<u32>(global_config.rare_cheats.camera_dist_caller);
            if (uWant == 0u)
                return;
            const uintptr_t uBase2 = exl::util::modules::GetTargetStart();
            const uintptr_t uLr2   = static_cast<uintptr_t>(ctx->X[30]);
            if (uLr2 <= uBase2 || static_cast<u32>(uLr2 - uBase2) != uWant)
                return;
            ++s_nEyeScaled;

            s_arScaledEye[0] = pT[0] + ox * k;
            s_arScaledEye[1] = pT[1] + oy * k;
            s_arScaledEye[2] = pT[2] + oz * k;
            s_arScaledEye[3] = pE[3];
            ctx->X[0] = reinterpret_cast<u64>(&s_arScaledEye[0]);
        }
    };

    // d3hack-custom: THE CAMERA UPDATE -- 0x2651F0, immediately before the view build.
    //
    // Chain, established by following code rather than scanning memory:
    //     0x29B590  the view-matrix builder (LookAt). ONE caller.
    //     0x2651F4  bl to it, from the camera update -- all the fadd/fsub above it is the
    //               camera smoothing.
    // The caller stages two vec3s on the stack and then, AFTER the call, copies the one at
    // sp+0x10 into a persistent object:
    //     ldp w8,w9,[sp,#0x10] ; str w8,[x19,#0x28c] / str w9,[x19,#0x290] / [x19,#0x294]
    //
    // Because that copy RE-READS the stack after the call, changing sp+0x10 before the bl
    // reaches both the view matrix and the persistent camera in one write. That is the
    // property every earlier attempt lacked: the view-matrix dolly at 0x29B754 moved only
    // the picture, so culling dropped edge geometry and input unprojected through the old
    // camera (walking south fought the stick). Here there is one value and one writer.
    //
    // Note 0x29B4D0 was NOT the camera setter despite its accessor-shaped neighbours -- it
    // is linker-folded code shared with colour/light setters, and its callers all turned out
    // to be particle lighting at 18000 calls/sec.
    // d3hack-custom: CENSUS AT THE VIEW BUILDER ENTRY -- 0x29B590.
    //
    // Argument map, from the prologue:
    //     x0 -> x19   the output matrix
    //     x1 -> x20   the EYE (the vec3 the body dots against the basis)
    //     x2          direction, 3 floats, negated when w4 == 0
    //
    // callers.py finds exactly ONE bl to this function (0x2651F4, the menu/character-screen
    // camera -- confirmed: the vec3s it stages are normalised basis vectors like
    // (0.707, 0.707, 0), not positions). Yet dollying inside this builder DID move the
    // gameplay camera. Both can only be true if the gameplay path reaches it INDIRECTLY,
    // through a function pointer or vtable, which leaves no bl for static analysis to find.
    //
    // So identify the caller at runtime: the hook sits on the first instruction, before
    // anything is pushed, so x30 still holds the return address. Bucket by it.
    struct ViewCallerSig {
        u32 uCaller;
        int nX, nY, nZ;
        u32 uCount;
    };
    // d3hack-custom: the world-space delta the render camera was moved by, so the shadow
    // and light passes can be moved by exactly the same vector. Moving them along their OWN
    // axes would be wrong -- a cascade centred on the view has to translate with the view,
    // not slide along the light direction.
    inline float s_flDollyDelta[3] {};
    inline u64   s_uDollySeenTick = 0ull;

    // Direction census, bucketed by the view direction rather than the caller. One caller
    // serves every pass, so the direction is what tells them apart.
    struct ViewDirSig {
        int nDx, nDy, nDz;
        u32 uCount;
    };
    inline ViewDirSig s_arViewDir[16] {};
    inline int        s_nViewDir  = 0;
    inline u64        s_uViewDirTick = 0ull;

    inline ViewCallerSig s_arViewCaller[16] {};
    inline int  s_nViewCaller = 0;
    inline u64  s_uViewTick   = 0ull;

    // d3hack-custom: what does the camera object actually hold?
    //
    // The seam that shows up once the dolly pulls back looks like geometry crossing a far
    // clip plane. Nothing in rodata names a draw distance -- the only fog-ish symbols are
    // shader uniforms (scene_fogPlanes, fogParams, InteractiveFogRT), and fog planes are
    // authored per level, which would put them out of reach of a global knob. A clip plane
    // would be one float on the camera, so look before theorising further.
    //
    // 0x2651F4 calls the view builder with x19 = the camera/view object and then writes
    // results into it at +0x28C/+0x290/+0x294, reading floats from +0x18, +0x58, +0xEC and
    // +0xFC. An inline hook at the builder's first instruction has not disturbed x19 yet, so
    // it is the caller's pointer. Dump once, read it, then decide.
    inline void CameraFieldDump(uintptr_t uCam) {
        static bool s_bDone = false;
        if (s_bDone || uCam < 0x10000ull)
            return;
        s_bDone = true;
        const auto *pF = reinterpret_cast<const float *>(uCam);
        PRINT("[d3hack-cam] camera object at %p, floats +0x000..+0x2A0:", (void *) uCam)
        for (int nRow = 0; nRow < 0x2A0 / 4; nRow += 4) {
            // Four per line keeps each PRINT short enough to survive the log, and prints
            // the offset so a candidate can be named directly rather than counted out.
            PRINT("[d3hack-cam]   +0x%03X  %d.%03d  %d.%03d  %d.%03d  %d.%03d",
                  nRow * 4,
                  ObsWhole(pF[nRow + 0]), ObsMilli(pF[nRow + 0]),
                  ObsWhole(pF[nRow + 1]), ObsMilli(pF[nRow + 1]),
                  ObsWhole(pF[nRow + 2]), ObsMilli(pF[nRow + 2]),
                  ObsWhole(pF[nRow + 3]), ObsMilli(pF[nRow + 3]))
        }
    }

    HOOK_DEFINE_INLINE(ViewBuildCensus) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (global_config.rare_cheats.camera_field_dump)
                CameraFieldDump(static_cast<uintptr_t>(ctx->X[19]));

            const auto uEye = static_cast<uintptr_t>(ctx->X[1]);
            if (uEye < 0x10000ull)
                return;
            const auto *pE = reinterpret_cast<const float *>(uEye);
            for (int i = 0; i < 3; ++i)
                if (!FloatIsSane(&pE[i]))
                    return;

            // THE DOLLY. x1 is the eye and x2 the view direction, so pulling the camera
            // back needs no target at all: eye += dir * d.
            //
            // Gated on a WORLD-SPACE eye. The census shows this one caller serving two very
            // different views -- eye(436, 626, 74) is the gameplay camera in world
            // coordinates, eye(80, 0, 0) is a canned/menu view -- so require both x and y to
            // be large. That is exactly what the earlier attempt got wrong: it scaled the
            // stack vec3s, which are the normalised BASIS vectors (0.707, 0.707, 0) and not
            // a position, so it distorted the menus and never moved a camera.
            // IDENTIFY THE CAMERA BY ITS DIRECTION, NOT ITS POSITION.
            //
            // The first gate was `|eye.x| > 150 && |eye.y| > 150`, taken from town where the
            // eye read (436, 626, 74). Interiors are separate worlds with small LOCAL
            // coordinates, so inside the Slaughtered Calf Inn the eye failed that test and
            // the zoom silently reverted to stock. Position tells us nothing portable.
            //
            // The direction does. D3's camera is fixed isometric and cannot be rotated, so
            // eye - target is always proportional to (0.5689, 0.5689, 0.5938) whatever the
            // scene: |dx| == |dy|, and |dz| a little larger. Menu and canned views do not
            // look like that.
            //
            // Signs are taken absolute deliberately. The builder negates x2 when w4 == 0, so
            // the raw vector arrives either way round -- but the camera always sits at +x,
            // +y, +z from what it looks at, so moving along (|dx|, |dy|, |dz|) is always
            // AWAY. Positive ViewDolly = further out, in every scene, with no sign to guess.
            const float d    = global_config.rare_cheats.view_dolly;
            const auto  uDir = static_cast<uintptr_t>(ctx->X[2]);
            if (d != 0.0f && uDir >= 0x10000ull) {
                const auto *pD = reinterpret_cast<const float *>(uDir);
                if (FloatIsSane(&pD[0]) && FloatIsSane(&pD[1]) && FloatIsSane(&pD[2])) {
                    const float adx = (pD[0] < 0.0f) ? -pD[0] : pD[0];
                    const float ady = (pD[1] < 0.0f) ? -pD[1] : pD[1];
                    const float adz = (pD[2] < 0.0f) ? -pD[2] : pD[2];
                    const float dxy = (adx > ady) ? (adx - ady) : (ady - adx);
                    const bool  bIso = (dxy < 0.03f) && (adx > 0.45f && adx < 0.70f) &&
                                       (adz > 0.45f && adz < 0.75f);

                    // Only read the clock when something actually needs it: this hook runs
                    // for every view matrix the engine builds, several times a frame.
                    const bool bNeedTick =
                        global_config.rare_cheats.view_dolly_shadows ||
                        global_config.rare_cheats.camera_dist_log;
                    const u64 uNow = bNeedTick ? svcGetSystemTick() : 0ull;
                    if (bIso) {
                        // The render camera. Move it, and remember BY HOW MUCH so the other
                        // passes can be moved by the same world vector.
                        s_flDollyDelta[0] = adx * d;
                        s_flDollyDelta[1] = ady * d;
                        s_flDollyDelta[2] = adz * d;
                        s_uDollySeenTick  = uNow;
                        auto *pW          = reinterpret_cast<float *>(uEye);
                        pW[0] += s_flDollyDelta[0];
                        pW[1] += s_flDollyDelta[1];
                        pW[2] += s_flDollyDelta[2];
                    } else if (global_config.rare_cheats.view_dolly_shadows &&
                               s_uDollySeenTick != 0ull &&
                               (uNow - s_uDollySeenTick) < (d3::pools::kTicksPerSecond / 5ull)) {
                        // A shadow, light or reflection pass built in the same frame as a
                        // live render camera. Translate it by the SAME delta so its volume
                        // follows the view -- that seam across the screen is where the
                        // un-moved coverage ended.
                        //
                        // The 200 ms window is the whole safety gate: menus never build an
                        // isometric camera, so s_uDollySeenTick goes stale and nothing here
                        // fires. That is what the earlier "dolly every view" attempt lacked,
                        // and why it distorted the menu screens.
                        auto *pW = reinterpret_cast<float *>(uEye);
                        pW[0] += s_flDollyDelta[0];
                        pW[1] += s_flDollyDelta[1];
                        pW[2] += s_flDollyDelta[2];
                    }

                    // Census by DIRECTION: one caller serves every pass, so the direction is
                    // the only thing that distinguishes render from shadow from light.
                    if (global_config.rare_cheats.camera_dist_log) {
                        const int nDx = static_cast<int>(pD[0] * 100.0f);
                        const int nDy = static_cast<int>(pD[1] * 100.0f);
                        const int nDz = static_cast<int>(pD[2] * 100.0f);
                        bool      bF  = false;
                        for (int i = 0; i < s_nViewDir; ++i) {
                            if (s_arViewDir[i].nDx == nDx && s_arViewDir[i].nDy == nDy &&
                                s_arViewDir[i].nDz == nDz) {
                                ++s_arViewDir[i].uCount;
                                bF = true;
                                break;
                            }
                        }
                        if (!bF && s_nViewDir < 16) {
                            s_arViewDir[s_nViewDir].nDx    = nDx;
                            s_arViewDir[s_nViewDir].nDy    = nDy;
                            s_arViewDir[s_nViewDir].nDz    = nDz;
                            s_arViewDir[s_nViewDir].uCount = 1;
                            ++s_nViewDir;
                        }
                        if (s_uViewDirTick == 0ull ||
                            (uNow - s_uViewDirTick) > (5ull * d3::pools::kTicksPerSecond)) {
                            s_uViewDirTick = uNow;
                            PRINT("[d3hack-vdir] --- view directions (%d) ---", s_nViewDir)
                            for (int i = 0; i < s_nViewDir; ++i)
                                PRINT("[d3hack-vdir]   dir(%d, %d, %d)/100  seen %u",
                                      s_arViewDir[i].nDx, s_arViewDir[i].nDy,
                                      s_arViewDir[i].nDz, s_arViewDir[i].uCount)
                            s_nViewDir = 0;
                        }
                    }
                }
            }

            if (!global_config.rare_cheats.camera_dist_log)
                return;
            const uintptr_t uBase = exl::util::modules::GetTargetStart();
            const uintptr_t uLr   = static_cast<uintptr_t>(ctx->X[30]);
            const u32 uCaller = (uLr > uBase) ? static_cast<u32>(uLr - uBase) : 0u;

            bool bF = false;
            for (int i = 0; i < s_nViewCaller; ++i) {
                if (s_arViewCaller[i].uCaller == uCaller) {
                    ++s_arViewCaller[i].uCount;
                    s_arViewCaller[i].nX = static_cast<int>(pE[0]);
                    s_arViewCaller[i].nY = static_cast<int>(pE[1]);
                    s_arViewCaller[i].nZ = static_cast<int>(pE[2]);
                    bF = true;
                    break;
                }
            }
            if (!bF && s_nViewCaller < 16) {
                s_arViewCaller[s_nViewCaller].uCaller = uCaller;
                s_arViewCaller[s_nViewCaller].nX      = static_cast<int>(pE[0]);
                s_arViewCaller[s_nViewCaller].nY      = static_cast<int>(pE[1]);
                s_arViewCaller[s_nViewCaller].nZ      = static_cast<int>(pE[2]);
                s_arViewCaller[s_nViewCaller].uCount  = 1;
                ++s_nViewCaller;
            }

            const u64 uNow = svcGetSystemTick();
            if (s_uViewTick == 0ull ||
                (uNow - s_uViewTick) > (5ull * d3::pools::kTicksPerSecond)) {
                s_uViewTick = uNow;
                PRINT("[d3hack-vcall] --- view-builder callers (%d) ---", s_nViewCaller)
                for (int i = 0; i < s_nViewCaller; ++i)
                    PRINT("[d3hack-vcall]   caller 0x%06X  eye(%d, %d, %d)  seen %u",
                          s_arViewCaller[i].uCaller, s_arViewCaller[i].nX,
                          s_arViewCaller[i].nY, s_arViewCaller[i].nZ,
                          s_arViewCaller[i].uCount)
                s_nViewCaller = 0;
            }
        }
    };

    inline int s_nCamUpdLog = 0;

    HOOK_DEFINE_INLINE(CameraUpdateScale) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            // sp+0x00 and sp+0x10 are the two vec3s staged for the builder. The one at
            // sp+0x10 is what lands in the persistent camera object, so it is the eye; the
            // other is the look-at target.
            float *pT = &ctx->SP.at<float>(0x00);
            float *pE = &ctx->SP.at<float>(0x10);
            for (int i = 0; i < 3; ++i)
                if (!FloatIsSane(&pE[i]) || !FloatIsSane(&pT[i]))
                    return;

            const float ox = pE[0] - pT[0];
            const float oy = pE[1] - pT[1];
            const float oz = pE[2] - pT[2];
            const float m2 = ox * ox + oy * oy + oz * oz;

            if (global_config.rare_cheats.camera_dist_log && s_nCamUpdLog < 10) {
                ++s_nCamUpdLog;
                PRINT("[d3hack-camupd] target(%d.%03d, %d.%03d, %d.%03d) eye-target(%d.%03d, "
                      "%d.%03d, %d.%03d) |off|=%d",
                      ObsWhole(pT[0]), ObsMilli(pT[0]), ObsWhole(pT[1]), ObsMilli(pT[1]),
                      ObsWhole(pT[2]), ObsMilli(pT[2]), ObsWhole(ox), ObsMilli(ox),
                      ObsWhole(oy), ObsMilli(oy), ObsWhole(oz), ObsMilli(oz),
                      static_cast<int>(__builtin_sqrtf(m2)))
            }

            const float k = global_config.rare_cheats.camera_dist_scale;
            if (k <= 0.0f || k == 1.0f)
                return;
            // Only a gameplay-sized offset, so a cutscene or menu camera passing through
            // this same update is left alone.
            if (m2 < (20.0f * 20.0f) || m2 > (400.0f * 400.0f))
                return;

            pE[0] = pT[0] + ox * k;
            pE[1] = pT[1] + oy * k;
            pE[2] = pT[2] + oz * k;
        }
    };

#include "program/d3/hooks/affix_names.inc"

    // d3hack-custom: asset name -> something a person can read.
    //
    // Rules, derived from the names actually seen in play:
    //   - drop a leading rarity/act/expansion tag (champion_, rare_, minion_, x1_, a5_ ...)
    //   - drop TRAILING variant tags only -- "_A", "_a5", "_01" -- never interior ones, so
    //     "x1_LR_Boss_Terror_A" keeps LR and loses only the A
    //   - split camelCase, so QuillDemon becomes Quill Demon
    //   - title-case each word
    // Variant descriptors like FastAttack and Ranged are KEPT: they distinguish packs, which
    // is the whole point of the line.
    inline auto PrettyTokEqCI(const char *t, int n, const char *lit) -> bool {
        int i = 0;
        for (; i < n && lit[i] != 0; ++i) {
            char a = t[i];
            char b = lit[i];
            if (a >= 'A' && a <= 'Z')
                a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = static_cast<char>(b - 'A' + 'a');
            if (a != b)
                return false;
        }
        return i == n && lit[i] == 0;
    }

    inline auto PrettyIsVariantTag(const char *t, int n) -> bool {
        if (n <= 0 || n > 3)
            return false;
        bool bDigits = true;
        for (int i = 0; i < n; ++i)
            if (t[i] < '0' || t[i] > '9') {
                bDigits = false;
                break;
            }
        if (bDigits)
            return true;                        // "01", "5"
        const char c = t[0];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
            return false;
        for (int i = 1; i < n; ++i)
            if (t[i] < '0' || t[i] > '9')
                return false;
        return true;                            // "A", "a5", "b12"
    }

    inline void PrettyMonsterName(char *pDst, size_t nDst, const char *pSrc) {
        if (pDst == nullptr || nDst == 0)
            return;
        pDst[0] = 0;
        if (pSrc == nullptr)
            return;

        constexpr int kMaxTok = 12;
        const char   *arTok[kMaxTok] {};
        int           arLen[kMaxTok] {};
        int           nTok = 0;
        const char   *p    = pSrc;
        while (*p != 0 && nTok < kMaxTok) {
            while (*p == '_' || *p == ' ')
                ++p;
            if (*p == 0)
                break;
            const char *pStart = p;
            while (*p != 0 && *p != '_' && *p != ' ')
                ++p;
            arTok[nTok] = pStart;
            arLen[nTok] = static_cast<int>(p - pStart);
            ++nTok;
        }
        if (nTok == 0)
            return;

        // "x1_LR_Boss_DarkAngel_A" needs THREE leading tags removed, not one -- dropping a
        // single token left "Lr Boss Dark Angel" in the log. Loop instead, and add the
        // rift/boss tags that showed up in play.
        static const char *const kDrop[] = {"champion", "rare",  "unique", "minion", "boss",
                                            "lr",       "uber",  "x1",     "p1",     "p2",
                                            "p3",       "p4",    "a1",     "a2",     "a3",
                                            "a4",       "a5",    "px",     "tr",     "cain"};
        // Drop leading tags by SHAPE as well as by name. The list had p1..p4 but the game
        // also ships p73_*, and one unlisted tag stopped the loop dead -- which is why
        // "P73 Lr Boss Despair" kept both Lr and Boss even though they are in the list.
        // PrettyIsVariantTag already recognises letter-then-digits (p73, x1, a5), so reuse
        // it here instead of trying to enumerate every patch prefix Blizzard has used.
        int nFirst = 0;
        while (nFirst < nTok - 1) {
            bool bDropped = false;
            for (const auto *d : kDrop)
                if (PrettyTokEqCI(arTok[nFirst], arLen[nFirst], d)) {
                    ++nFirst;
                    bDropped = true;
                    break;
                }
            if (!bDropped && PrettyIsVariantTag(arTok[nFirst], arLen[nFirst])) {
                ++nFirst;
                bDropped = true;
            }
            if (!bDropped)
                break;
        }

        int nLast = nTok;
        while (nLast - 1 > nFirst && PrettyIsVariantTag(arTok[nLast - 1], arLen[nLast - 1]))
            --nLast;

        size_t o = 0;
        for (int i = nFirst; i < nLast; ++i) {
            for (int j = 0; j < arLen[i]; ++j) {
                const char c          = arTok[i][j];
                const char prev       = (j > 0) ? arTok[i][j - 1] : static_cast<char>(0);
                const bool bUp        = (c >= 'A' && c <= 'Z');
                const bool bPrevLower = (prev >= 'a' && prev <= 'z') ||
                                        (prev >= '0' && prev <= '9');
                const bool bWordStart = (j == 0) || (bUp && bPrevLower);
                if (bWordStart && o != 0 && o + 1 < nDst)
                    pDst[o++] = ' ';
                if (o + 1 >= nDst)
                    break;
                char out = c;
                if (bWordStart) {
                    if (out >= 'a' && out <= 'z')
                        out = static_cast<char>(out - 'a' + 'A');
                } else {
                    if (out >= 'A' && out <= 'Z')
                        out = static_cast<char>(out - 'A' + 'a');
                }
                pDst[o++] = out;
            }
        }
        pDst[(o < nDst) ? o : (nDst - 1)] = 0;

        // Never return nothing: a name made entirely of tags falls back to the raw string.
        if (pDst[0] == 0) {
            size_t n = 0;
            while (pSrc[n] != 0 && n + 1 < nDst) {
                pDst[n] = pSrc[n];
                ++n;
            }
            pDst[n] = 0;
        }
    }

    // d3hack-custom: the elite's affixes, straight off the ACD.
    //
    // Found by exact-value search rather than guesswork: the 51 affix GBIDs from
    // GameBalance/MonsterAffixes.gam were scanned for inside a live elite's ACD, and they
    // sit as five consecutive u32s at +0x178 (mirrored at +0x4D8 and +0x838):
    //
    //     +0x178  00402EEA  Rare          <- rarity marker, not an affix
    //     +0x17C  F8BAAC2C  Desecrator
    //     +0x180  D4BC2B17  Shielding
    //     +0x184  065B6A15  Mortar
    //     +0x188  FCFC91CF  Thunderstorm
    //
    // which is exactly what the game printed on that monster's health bar.
    inline auto AffixIsRarityMarker(const char *szName) -> bool {
        return PrettyTokEqCI(szName, static_cast<int>(__builtin_strlen(szName)), "ChampionBase") ||
               PrettyTokEqCI(szName, static_cast<int>(__builtin_strlen(szName)), "Rare") ||
               PrettyTokEqCI(szName, static_cast<int>(__builtin_strlen(szName)), "Minion") ||
               PrettyTokEqCI(szName, static_cast<int>(__builtin_strlen(szName)), "Unique");
    }

    // d3hack-custom: colour affixes by what they DO to you, so the line is scannable
    // without reading it. Grouping beats a colour-per-affix: 51 arbitrary colours carry no
    // information, four meaningful ones do.
    //   red    - ground damage you have to move out of
    //   cyan   - crowd control, the ones that stop you moving
    //   grey   - defensive/tanky, slower kill rather than dangerous
    //   violet - mobility and swarm, the ones that waste your time
    inline auto AffixColorHex(const char *szName) -> const char * {
        struct Grp { const char *szHex; const char *arName[14]; };
        static const Grp kGrp[] = {
            {"FF6A4A", {"Desecrator", "Molten", "Plagued", "ArcaneEnchanted", "Thunderstorm",
                        "Mortar", "Orbiter", "FrozenPulse", "FireChains", "Electrified",
                        "PoisonEnchanted", "Ballista", "Multishot", nullptr}},
            {"6ED2FF", {"Jailer", "Frozen", "Vortex", "Knockback", "Waller", "Nightmarish",
                        nullptr}},
            {"C8C8C8", {"Shielding", "Extra Health", "Health Link", "Missile Dampening",
                        "Juggernaut", "ReflectsDamage", "FireImmune", "ColdImmune",
                        "LightningImmune", "PoisonImmune", nullptr}},
            {"C89AFF", {"Teleporter", "Illusionist", "Wormhole", "Fast", "Avenger", "Horde",
                        "Vampiric", "Frenzy", nullptr}},
        };
        for (const auto &g : kGrp)
            for (const auto *n : g.arName) {
                if (n == nullptr)
                    break;
                if (PrettyTokEqCI(szName, static_cast<int>(__builtin_strlen(szName)), n))
                    return g.szHex;
            }
        return "D8D8D8";
    }

    // Only the NAME carries the rarity colour now; the "Engaged Rare" prefix stays neutral
    // so the eye lands on what was actually pulled. Killed lines use dimmer variants of the
    // same hues so the two read apart without reading the words.
    inline auto RarityColorHex(int nRarity, bool bKilled) -> const char * {
        if (nRarity == 7)
            return bKilled ? "B32999" : "FF59D9";   // rift guardian
        if (nRarity == 1)
            return bKilled ? "5980CC" : "73A6FF";   // champion, blue
        if (nRarity == 2)
            return bKilled ? "BFA640" : "FFD94D";   // rare, yellow
        return bKilled ? "A0A0A0" : "E0E0E0";
    }

    inline void BuildAffixList(char *pDst, size_t nDst, const void *pACD) {
        if (pDst == nullptr || nDst == 0)
            return;
        pDst[0] = 0;
        if (pACD == nullptr)
            return;
        const auto *pW = reinterpret_cast<const u32 *>(pACD);
        size_t      o  = 0;
        for (int i = 0; i < 8; ++i) {
            const u32 uV = pW[(0x178 / 4) + i];
            if (uV == 0u || uV == 0xFFFFFFFFu)
                continue;
            const char *szName = nullptr;
            for (const auto &t : kAffixNames)
                if (t.uGbid == uV) {
                    szName = t.szName;
                    break;
                }
            if (szName == nullptr || AffixIsRarityMarker(szName))
                continue;
            // "ChampionGhostly" -> "Ghostly", "ArcaneEnchanted" -> "Arcane Enchanted":
            // the same prettifier the monster names use, so one set of rules covers both.
            char szP[48] {};
            PrettyMonsterName(szP, sizeof(szP), szName);
            if (szP[0] == 0)
                continue;
            // Separator in the neutral colour, then the affix in its group's colour.
            const char *pSep = (o == 0) ? "\x01""8C8C8C - " : "\x01""8C8C8C, ";
            for (const char *q = pSep; *q != 0 && o + 1 < nDst; ++q)
                pDst[o++] = *q;
            pDst[o] = 0;
            if (o + 8 < nDst) {
                pDst[o++] = 0x01;
                for (const char *q = AffixColorHex(szName); *q != 0 && o + 1 < nDst; ++q)
                    pDst[o++] = *q;
            }
            for (const char *q = szP; *q != 0 && o + 1 < nDst; ++q)
                pDst[o++] = *q;
        }
        pDst[(o < nDst) ? o : (nDst - 1)] = 0;
    }

    // d3hack-custom: THE KILL LINE, by watching what we announced.
    //
    // Both routes that name the victim directly are dead, measured not assumed:
    //   - the kill handler at 0x842DF0: w0/w1 are not ACDIDs (ACDTryToGet null on both)
    //   - LAST_ACD_KILLED_TIME (0x472): param is -1 on EVERY write, so the key carries no
    //     actor at all -- it is purely a timestamp
    //
    // So do not ask the game what died. We already know which elites we announced, and an
    // id we chose ourselves cannot be misidentified: hold onto it and watch it stop
    // resolving, or its health reach zero. Whichever happens first is the kill.
    struct AnnouncedElite {
        ACDID id;
        int   nRarity;
        char  szName[64];
        char  szAffix[128];
        u64   uTick;
        bool  bUsed;
    };
    inline AnnouncedElite s_arAnnounced[16] {};
    inline u64            s_uKillPollTick = 0ull;

    inline void RememberAnnounced(ACDID id, int nRarity, const char *szName,
                                  const char *szAffix) {
        int nSlot = -1;
        for (int i = 0; i < 16; ++i) {
            if (s_arAnnounced[i].bUsed && s_arAnnounced[i].id == id)
                return;                       // already tracking this exact actor
            if (nSlot < 0 && !s_arAnnounced[i].bUsed)
                nSlot = i;
        }
        if (nSlot < 0)
            nSlot = 0;                        // ring over the oldest rather than drop it
        auto &t   = s_arAnnounced[nSlot];
        t.id      = id;
        t.nRarity = nRarity;
        t.uTick   = svcGetSystemTick();
        t.bUsed   = true;
        size_t n  = 0;
        for (; szName != nullptr && szName[n] != 0 && n + 1 < sizeof(t.szName); ++n)
            t.szName[n] = szName[n];
        t.szName[n] = 0;
        n           = 0;
        for (; szAffix != nullptr && szAffix[n] != 0 && n + 1 < sizeof(t.szAffix); ++n)
            t.szAffix[n] = szAffix[n];
        t.szAffix[n] = 0;
    }

    inline void PollAnnouncedDeaths() {
        if (!global_config.rare_cheats.combat_log || ACDTryToGet == nullptr)
            return;

        // !! HOT PATH !!  This runs on EVERY ACD attribute write -- thousands per second --
        // and svcGetSystemTick is a SYSCALL, which is expensive under emulation. Reading it
        // just to check a throttle cost more than the work it was throttling, and showed up
        // as ~5 fps even in an empty rift.
        //
        // Gate on a plain counter first: 511 of every 512 calls now cost one increment and a
        // test. Also bail immediately when nothing is being tracked, which is the common
        // case outside a pack fight.
        static u32 s_nTick = 0;
        if ((++s_nTick & 0x1FFu) != 0u)
            return;
        bool bAny = false;
        for (const auto &t : s_arAnnounced)
            if (t.bUsed) {
                bAny = true;
                break;
            }
        if (!bAny)
            return;

        const u64 uNow = svcGetSystemTick();
        if (s_uKillPollTick != 0ull &&
            (uNow - s_uKillPollTick) < (d3::pools::kTicksPerSecond / 4ull))
            return;
        s_uKillPollTick = uNow;

        for (auto &t : s_arAnnounced) {
            if (!t.bUsed)
                continue;
            // Give up on anything we have held for five minutes: the actor may have been
            // unloaded with the world rather than killed, and a stale entry would announce
            // a kill that never happened.
            if ((uNow - t.uTick) > (300ull * d3::pools::kTicksPerSecond)) {
                t.bUsed = false;
                continue;
            }
            ActorCommonData *pA   = ACDTryToGet(t.id);
            bool             bDead = (pA == nullptr);
            if (!bDead)
                bDead = (pools::GetAttrInt(pA, 0xFFFFF073u) <= 0);   // HITPOINTS_CUR
            if (!bDead)
                continue;

            t.bUsed = false;
            if (t.nRarity == 7) {
                d3::imgui_overlay::PostCombatLog(
                    0.62f, 0.62f, 0.62f, "Killed " "\x01" "%s%s%s",
                    RarityColorHex(t.nRarity, true), t.szName, t.szAffix);
                continue;
            }
            const char *szWhat = (t.nRarity == 1) ? "Champion" : "Rare";
            d3::imgui_overlay::PostCombatLog(
                0.62f, 0.62f, 0.62f, "Killed %s " "\x01" "%s%s%s", szWhat,
                RarityColorHex(t.nRarity, true), t.szName, t.szAffix);
        }
    }


    // d3hack-custom: WHO ADDS BLOOD SHARDS?
    //
    // Goal: partial pickup, PC-style -- take what fits under the cap instead of refusing the
    // whole pile. ACD_ModifyCurrencyAmount (0x47FF40) is only an overflow guard; it has no
    // cap check at all, so the refusal is in whoever calls it. There are 37 call sites, far
    // too many to read, and the failing path may not call it at all.
    //
    // So identify it at runtime. An inline hook on the first instruction still has the
    // caller's return address in x30, which is the same trick that separated the gameplay
    // camera from the menu camera and the folded colour setter from a real camera setter.
    // Currency type 1 is BloodShards (CurrencyType enum, confirmed by minor[08] costing
    // "type=1 qty=1100" = 1100 Blood Shards in the altar dump).
    // d3hack-custom: the pickup's own cap arithmetic, at 0x48F304.
    //
    // 0x48F2B0 ALREADY implements partial pickup: overflow = current + pile - cap, and when
    // current < cap it takes (pile - overflow), i.e. exactly the room left. The only refusal
    // is `cmp current, cap / b.ge` -- being completely full. So before writing any clamp,
    // find out which of those three numbers disagrees with the screen.
    //   x0 = current (just returned by GetCurrencyAmount)   x21 = pile   x22 = cap
    // d3hack-custom: PC-style partial pickup -- split the pile instead of eating it.
    //
    // Two hooks bracketing one call of the pickup handler (0x48F870). The whole feature is
    // three facts about that function:
    //
    //   0x48F9E0  the gate      -- current + pile vs cap, "whole pile or nothing"
    //   0x48F2B0  the clamp     -- already takes exactly (cap - current) when let through
    //   0x48F9B0  the destroy   -- bl 0x86B210(actorId), UNCONDITIONAL once the take succeeds
    //
    // Relaxing the gate alone is a trap, and it cost a live test to find out: the clamp takes
    // the room correctly, and then the destroy removes the ground actor whole, so everything
    // that did not fit is deleted. That is strictly worse than the stock refusal -- the stock
    // bug wasted your time, this one wasted your shards.
    //
    // So the remainder has to survive. 0x86B210 early-exits on id == -1 (`cmn w19, #1`), and
    // the id it destroys is loaded one instruction earlier by `ldr w0, [x8, #0x14]`. Pointing
    // x8 at our own scratch record whose +0x14 reads -1 turns the destroy into a no-op without
    // patching a single byte of the shared code path -- grid items still get destroyed
    // normally, because the override only arms when the gate saw a genuine partial.
    //
    // The pile's own count then has to come down by what was taken, or the same pile can be
    // farmed forever. Quantity is a 64-bit value split across two attributes, per 0x485260:
    // 0x190 is the HIGH word, 0x191 the LOW word, both with param -1. The attribute key is
    // just id | (param << 12) -- 0x69AEC0 is a one-instruction orr w0, w0, w1, lsl #12 -- so
    // param -1 gives 0xFFFFF190 / 0xFFFFF191 and no call is needed to build it.
    inline void     *s_pPartialACD  = nullptr;
    inline u32       s_uPartialId   = 0;
    inline long long s_nPartialKeep = 0;
    inline long long s_nPartialTook = 0;

    inline void ShardSetQuantity(void *pACD, long long nValue) {
        using Fn = void (*)(void *, u64, s32);
        auto *pFn = reinterpret_cast<Fn>(GameOffset(0x46FB90));   // ACD_AttributesSetInt
        if (pFn == nullptr)
            return;
        pFn(pACD, 0xFFFFF190ull, static_cast<s32>(static_cast<u64>(nValue) >> 32));
        pFn(pACD, 0xFFFFF191ull, static_cast<s32>(static_cast<u64>(nValue) & 0xFFFFFFFFull));
    }

    inline auto ShardGetQuantity(void *pACD) -> long long {
        using Fn = s32 (*)(void *, u64);
        auto *pFn = reinterpret_cast<Fn>(GameOffset(0x46FAB0));   // ACD_AttributesGetInt
        if (pFn == nullptr)
            return -1;
        const u64 uHi = static_cast<u32>(pFn(pACD, 0xFFFFF190ull));
        const u64 uLo = static_cast<u32>(pFn(pACD, 0xFFFFF191ull));
        return static_cast<long long>((uHi << 32) | uLo);
    }

    // x0 = current amount, x19 = the item ACD, x22 = pile, x24 = cap, w21 = currency type.
    HOOK_DEFINE_INLINE(ShardPileSplit) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            const long long nCur  = static_cast<long long>(ctx->X[0]);
            const long long nPile = static_cast<long long>(ctx->X[22]);
            const long long nCap  = static_cast<long long>(ctx->X[24]);
            const bool      bFits = (nCap < 1) || (nCur + nPile <= nCap);
            const bool      bRoom = (nCap >= 1) && (nCur < nCap);

            // Cleared on EVERY call, fitting or not: an early-out between here and the
            // destroy site must not leave a stale record armed for the next pickup.
            s_pPartialACD = nullptr;

            if (!bFits && bRoom && global_config.rare_cheats.partial_currency_pickup) {
                auto *pACD     = reinterpret_cast<void *>(static_cast<uintptr_t>(ctx->X[19]));
                s_pPartialACD  = pACD;
                s_uPartialId   = *reinterpret_cast<u32 *>(pACD);   // id lives at ACD+0
                s_nPartialTook = nCap - nCur;
                s_nPartialKeep = nPile - s_nPartialTook;

                // Shrink the pile to exactly the room left, right here, and the stock gate
                // waves it through on its own: the next three instructions are
                // add x8, x0, x22 / cmp x8, x24 / b.le, so current + room == cap passes.
                // x22 is also what gets handed to the clamp as the amount (0x48FAFC and
                // 0x48FBC0 both do mov x4, x22), so one write both authorises the take and
                // sizes it, with no overflow left for the clamp to discard.
                //
                // Deliberately NOT an instruction patch on the gate. A patched gate stays
                // relaxed even if this hook never runs, and a relaxed gate without the
                // remainder write deletes shards -- the failure mode has to be stock
                // refusal, not silent loss.
                ctx->X[22] = static_cast<u64>(s_nPartialTook);
            }

            if (!global_config.rare_cheats.shard_probe)
                return;
            const u32 uType = static_cast<u32>(ctx->W[21]);
            if (uType == 0u)
                return;                       // gold, constant traffic
            // Only the oversized case is worth a line. A whole session of ordinary pickups is
            // 40 shards at a time against 1700 of room, and logging those burned the entire
            // budget before the bar ever got near the cap -- the one event under test was the
            // one event that could never be recorded. Two freebies at the top so an empty log
            // still distinguishes "never fired" from "fired, nothing interesting".
            static int s_nFit = 0;
            if (bFits && s_nFit >= 2)
                return;
            if (bFits)
                ++s_nFit;
            static int s_n = 0;
            if (s_n >= 16)
                return;
            ++s_n;
            PRINT("[d3hack-shard] gate: type=%u current=%lld pile=%lld cap=%lld room=%lld -> %s",
                  uType, nCur, nPile, nCap, nCap - nCur,
                  bFits                        ? "fits, take all"
                  : (s_pPartialACD != nullptr) ? "SPLIT (take the room, leave the rest)"
                                               : "refused, at cap")
        }
    };

    // 0x48F9AC: ldr w0, [x8, #0x14], feeding the destroy at 0x48F9B0. x19 is the actor id by
    // this point (0x48F92C reloaded it from the ACD), which is what identifies the record as
    // belonging to the pickup the gate armed rather than to some later one.
    HOOK_DEFINE_INLINE(ShardPileKeep) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            void *pACD = s_pPartialACD;
            if (pACD == nullptr)
                return;
            s_pPartialACD = nullptr;
            if (static_cast<u32>(ctx->W[19]) != s_uPartialId)
                return;                       // a different actor is being retired; leave it

            ShardSetQuantity(pACD, s_nPartialKeep);

            // x8 is dead after the load (0x48F9B4 is mov w19, #1 and 0x48F9B8 reloads x8 from
            // the stack), so redirecting it costs nothing downstream.
            static u32 s_arScratch[8] {};
            s_arScratch[5] = 0xFFFFFFFFu;     // +0x14 -> -1 -> 0x86B210 early-exits
            ctx->X[8] = reinterpret_cast<u64>(&s_arScratch[0]);

            if (!global_config.rare_cheats.shard_probe)
                return;
            static int s_n = 0;
            if (s_n >= 8)
                return;
            ++s_n;
            PRINT("[d3hack-shard] split: took %lld, left %lld on the ground (reads back %lld)",
                  s_nPartialTook, s_nPartialKeep, ShardGetQuantity(pACD))
        }
    };

    HOOK_DEFINE_INLINE(ShardCapProbe) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (!global_config.rare_cheats.shard_probe)
                return;
            if (static_cast<u32>(ctx->W[25]) != 1u)
                return;                       // blood shards only
            const long long nCur  = static_cast<long long>(ctx->X[0]);
            const long long nPile = static_cast<long long>(ctx->X[21]);
            const long long nCap  = static_cast<long long>(ctx->X[22]);
            // As above: a pile that fits proves nothing about the partial path.
            static int s_nFit = 0;
            const bool bFits = (nCap < 1) || (nCur + nPile <= nCap);
            if (bFits && s_nFit >= 2)
                return;
            if (bFits)
                ++s_nFit;
            static int s_n = 0;
            if (s_n >= 12)
                return;
            ++s_n;
            PRINT("[d3hack-shard] clamp: current=%lld pile=%lld cap=%lld room=%lld -> take %lld",
                  nCur, nPile, nCap, nCap - nCur,
                  (nCap < 1 || nCur + nPile <= nCap) ? nPile
                  : (nCur >= nCap)                   ? 0LL
                                                     : (nCap - nCur))
        }
    };

    HOOK_DEFINE_INLINE(CurrencyModifyProbe) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (!global_config.rare_cheats.shard_probe)
                return;
            if (static_cast<u32>(ctx->W[2]) != 1u)
                return;                       // blood shards only
            static u32 s_arSite[12] {};
            static int s_nSite = 0;
            const uintptr_t uBase = exl::util::modules::GetTargetStart();
            const uintptr_t uLr   = static_cast<uintptr_t>(ctx->X[30]);
            const u32 uCaller = (uLr > uBase) ? static_cast<u32>(uLr - uBase) : 0u;
            for (int i = 0; i < s_nSite; ++i)
                if (s_arSite[i] == uCaller)
                    return;                   // one line per call site, not per pickup
            if (s_nSite < 12)
                s_arSite[s_nSite++] = uCaller;
            PRINT("[d3hack-shard] modify(type=1) amount=%lld from caller 0x%06X",
                  static_cast<long long>(ctx->X[1]), uCaller)
        }
    };

    inline int s_nDollyLog = 0;
    // d3hack-custom: a CENSUS of the view matrices, not a snapshot.
    //
    // The first 16 lines were all captured during load, and banding on them matched nothing
    // once the world was up -- the zoom silently did nothing. Sampling the first N of
    // anything has now produced a wrong answer twice today (the probe budgets, and this),
    // so count distinct translations continuously and report them periodically instead.
    struct DollySig {
        int nX, nY, nZ;
        u32 uCount;
    };
    inline DollySig s_arDollySig[24] {};
    inline int      s_nDollySig     = 0;
    inline u32      s_nDollyApplied = 0;
    inline u64      s_uDollyTick    = 0ull;

    HOOK_DEFINE_INLINE(ViewMatrixDolly) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            auto *m = reinterpret_cast<float *>(ctx->X[19]);
            if (reinterpret_cast<uintptr_t>(m) < 0x10000ull)
                return;
            if (!FloatIsSane(&m[12]) || !FloatIsSane(&m[13]) || !FloatIsSane(&m[14]))
                return;

            // This builder is generic -- shadow, reflection and UI passes go through it too.
            // Log a handful so the main camera can be told apart from the rest before
            // anything is applied to all of them indiscriminately.
            if (global_config.rare_cheats.view_dolly_log) {
                // Bucket by rounded translation so each camera shows up once with a count.
                const int nX = static_cast<int>(m[12]);
                const int nY = static_cast<int>(m[13]);
                const int nZ = static_cast<int>(m[14]);
                bool      bF = false;
                for (int i = 0; i < s_nDollySig; ++i) {
                    // Group anything within a few units: the game camera drifts as you walk.
                    const int dz = s_arDollySig[i].nZ - nZ;
                    if (dz < 6 && dz > -6) {
                        ++s_arDollySig[i].uCount;
                        s_arDollySig[i].nX = nX;
                        s_arDollySig[i].nY = nY;
                        s_arDollySig[i].nZ = nZ;
                        bF = true;
                        break;
                    }
                }
                if (!bF && s_nDollySig < 24) {
                    s_arDollySig[s_nDollySig].nX     = nX;
                    s_arDollySig[s_nDollySig].nY     = nY;
                    s_arDollySig[s_nDollySig].nZ     = nZ;
                    s_arDollySig[s_nDollySig].uCount = 1;
                    ++s_nDollySig;
                }

                const u64 uNow = svcGetSystemTick();
                if (s_uDollyTick == 0ull ||
                    (uNow - s_uDollyTick) > (5ull * d3::pools::kTicksPerSecond)) {
                    s_uDollyTick = uNow;
                    PRINT("[d3hack-dolly] --- census (%d cameras, %u dollied) ---", s_nDollySig,
                          s_nDollyApplied)
                    for (int i = 0; i < s_nDollySig; ++i)
                        PRINT("[d3hack-dolly]   z=%-6d x=%-6d y=%-6d  seen %u", s_arDollySig[i].nZ,
                              s_arDollySig[i].nX, s_arDollySig[i].nY, s_arDollySig[i].uCount)
                    s_nDollySig = 0;
                }
            }

            // ONLY THE GAME CAMERA. This builder serves every view in the engine, and
            // applying to all of them blanked the screen. The passes are easy to tell apart
            // by their translation row:
            //
            //     (0.000,   -2.518,  -79.965)   the game camera   <-- this one
            //     (64.720,  -2.781, -156.053)   second main-camera pass
            //     (-121.176, 65.089, -999.566)  light / shadow camera
            //     (0.000,  -14.142, -500.000)   shadow cascade
            //     (0.000,    0.000,  -50.000)   UI / portrait, called constantly
            //
            // z is -(camera distance), and the game camera sits at ~-80, matching the ~84
            // measured from the eye/target pair all along. Band it tightly and leave the
            // shadow, light and UI views alone.
            const float d = global_config.rare_cheats.view_dolly;
            if (d == 0.0f)
                return;
            // PICK THE CAMERA THAT MOVES.
            //
            // Four views come through this builder every frame, and three were dollied
            // hundreds of times a second with no visible effect at all:
            //
            //     z=-19   x=0    y=0     frozen      tested, nothing
            //     z=-79   x=0    y=-4    frozen      tested, nothing
            //     z=-100  x=0    y=0     frozen      tested, nothing
            //     z=-648  x=-134 y=386   -> -635 / -104 / 375 as the player walked
            //
            // Only the last one changes, so only the last one is looking at the world; the
            // others are canned views (UI portrait and fixed-offset passes). Its z drifts
            // with the player, which is why a fixed z band could never hold it -- but its
            // translation is in WORLD space, so a large |x| identifies it regardless of
            // where the player is standing. The canned views all sit at x = 0.
            // MOVE EVERY WORLD CAMERA TOGETHER, or the passes disagree.
            //
            // Dollying only the moving camera DID zoom the view -- and steadily, with no
            // flicker, because this changes the value where it is computed instead of
            // racing a per-frame buffer. But it left two artefacts, and both are the same
            // bug: the other passes were still using the old, closer camera.
            //   - models missing at the screen edges -> CULLING still had the old frustum,
            //     so newly-visible geometry was never submitted
            //   - movement fighting the stick        -> INPUT still unprojected through the
            //     old camera, so stick direction and screen direction disagreed
            //
            // So the frozen views are the culling/input/shadow passes and they have to move
            // by the same amount. Excluded: z > -30, which is the UI portrait camera (the
            // follower's head) -- pulling that back would shrink the portrait.
            const float z  = m[14];
            const float ax = (m[12] < 0.0f) ? -m[12] : m[12];
            // ONLY the moving render camera.
            //
            // Extending the dolly to the frozen views was wrong twice over: it broke the
            // menu screens (they are among them) and it did NOT fix the edge popin. So
            // culling does not derive from this matrix -- the frustum is built upstream from
            // the camera parameters, before this function ever runs. Changing the view
            // matrix here moves the picture without moving anything that decides what gets
            // submitted, and no amount of gating in this hook can reach that.
            if (ax < 10.0f)
                return;
            if (!(z < global_config.rare_cheats.view_dolly_zmax &&
                  z > global_config.rare_cheats.view_dolly_zmin))
                return;
            // The |x| < 1 test came from load-time samples where x was exactly 0. Dropped:
            // once the world is up the camera tracks the player and x is whatever it is.

            // SUBTRACT: z is negative and is minus the distance, so pushing it further
            // negative moves the camera back. Adding +84 to -79.965 put the camera at +4 --
            // essentially on top of the player, looking out from inside the world, which is
            // exactly the fog-coloured screen that produced.
            m[14] = z - d;
            ++s_nDollyApplied;
        }
    };

    inline void ObserverAssetScan();      // forward decl for the worker

    inline void ScanWorker(void *) {
        while (true) {
            if (global_config.rare_cheats.active &&
                global_config.rare_cheats.camera_observer_scan && !s_bObsScanDone &&
                s_uObsAnchor != 0ull) {
                ObserverAssetScan();
            }
            // Yield generously: this is background work, and being polite here is the whole
            // point of moving it off the game thread. Raw syscall to stay clear of the
            // nn::TimeSpan headers.
            // 32 MB slices with a 2 ms nap still took the game to ~2 fps: the worker thread
            // moved the cost off the frame but did not reduce it, and it competes with the
            // emulator for the same cores. Small slice, longer nap -- the window is only
            // 44 MB now, so this still finishes in a couple of seconds.
            svcSleepThread(8000000LL);   // 8 ms
        }
    }

    inline void StartScanWorker() {
        if (g_bScanThreadUp)
            return;
        g_bScanThreadUp = true;
        // !! nn::os priorities are 0..31 (0 = most urgent, default 16), NOT libnx's 0..63.
        // Passing 50 tripped an internal R_ABORT_UNLESS inside CreateThread and killed the
        // game with svcBreak -- and because it aborts rather than returning an error, the
        // fallback below never got a chance to run. Use the named constants.
        const s32 nPrio = nn::os::DefaultThreadPriority + 8;   // 24: clearly below the game
        const auto r = nn::os::CreateThread(ScanThreadPtr(), ScanWorker, nullptr, g_scanStack,
                                            sizeof(g_scanStack), nPrio);
        if (R_FAILED(r)) {
            PRINT("[d3hack-obs] worker thread failed (0x%X) at prio %d -- falling back to the "
                  "game thread", static_cast<u32>(r), nPrio)
            g_bScanThreadUp = false;
            return;
        }
        nn::os::StartThread(ScanThreadPtr());
        PRINT("[d3hack-obs] scan worker started at prio %d -- the game thread no longer pays for it", nPrio)
    }

    inline void ObserverAssetScan() {
        if (s_bObsScanDone || s_uObsAnchor == 0ull)
            return;

        // Wait for the camera lock. Everything below matches the camera's OWN offset bits,
        // so starting before the lock would be back to guessing -- which is exactly what
        // produced three rounds of degenerate matches: axis-aligned pairs, z-dominant
        // vectors, and finally 23151 uniform (n, n, n) triples out of the UI tables.
        if (s_uCamEye == 0ull) {
            // Its own counter: incrementing s_nObsPass here spent the re-arm budget on
            // ticks that never swept anything, and mislabelled the first real sweep
            // "pass 10".
            static int s_nObsWait = 0;
            if ((s_nObsWait & 7) == 0)
                PRINT("[d3hack-obs] waiting for the camera lock before sweeping%s", "")
            ++s_nObsWait;
            return;
        }
        ObsBuildIngredients();

        // STOP EARLY. Once a game projection matrix has been found there is nothing left to
        // look for -- the remaining gigabytes cost frames and buy nothing. Carry on only far
        // enough past the first hit to collect the rest of the cluster (they sit within a few
        // KB of each other), then shut the sweep down for good.
        // Stop only when there is nothing left to look for.
        //
        // The early stop was added when the sweep ran on the game thread and cost frames.
        // It also silently truncated the FOV hunt: the sweep quit 1 MB after the matrices,
        // so "0 FOV hits" meant the search never ran rather than the value not existing --
        // a negative result that was really no result. On the worker thread the sweep is
        // free, so let it finish the band unless BOTH targets are in hand.
        if (false) {
            s_bObsScanDone = true;
            s_nObsPass     = 99;                 // and never re-arm on a world change
            const uintptr_t uPad = 0x1000ull;
            s_uProjLo = (s_uProjLo > uPad) ? (s_uProjLo - uPad) : s_uProjLo;
            s_uProjHi = s_uProjHi + uPad;
            PRINT("[d3hack-proj] arena found, sweep STOPPED. holding %p..%p (%uKB), stock "
                  "m00=%d.%03d m11=%d.%03d",
                  reinterpret_cast<void *>(s_uProjLo), reinterpret_cast<void *>(s_uProjHi),
                  static_cast<u32>((s_uProjHi - s_uProjLo) >> 10), ObsWhole(s_flProjM00),
                  ObsMilli(s_flProjM00), ObsWhole(s_flProjM11), ObsMilli(s_flProjM11))
            return;
        }

        // Band the sweep around the live anchor instead of walking the whole address
        // space. +/-2 GB covers the arena the actors and their assets live in, and takes
        // a handful of ticks rather than the ~19 minutes an unbounded walk needed.
        if (s_uObsLo == 0ull) {
            // THE ARENA SITS JUST ABOVE THE ANCHOR. Measured across runs:
            //     anchor 0x10E9782240 -> arena 0x10EADC0F80   = +23.3 MB
            //     anchor 0x10E9212A20 -> arena 0x10EAF0D0C0   = +30.4 MB
            //     anchor 0x10E94594E0 -> arena 0x10EB12C3A0   = +30.2 MB
            // Always 23-31 MB above the live ACD pointer. Sweeping +-2 GB to find something
            // that is reliably inside a 44 MB window was ~100x more work than the job needs,
            // and scanning is expensive enough under emulation (~24 MB/s saturates a core)
            // that the difference is the whole performance problem. Try the narrow window
            // first and only fall back to the full band if it comes up empty.
            s_uObsLo = s_uObsAnchor + (12ull << 20);
            s_uObsHi = s_uObsAnchor + (56ull << 20);
            s_uObsCur = s_uObsLo;
            PRINT("[d3hack-obs] pass %d sweeping %p .. %p (anchor %p)", s_nObsPass + 1,
                  reinterpret_cast<void *>(s_uObsLo), reinterpret_cast<void *>(s_uObsHi),
                  reinterpret_cast<void *>(s_uObsAnchor))
        }
        if (s_uObsCur < s_uObsLo || s_uObsCur > s_uObsHi)
            s_uObsCur = s_uObsLo;

        constexpr u64 kChunk        = 1ull << 20;
        // 512 MB per half-second tick ran 2119 MB in one go and visibly stalled the
        // game -- this runs synchronously on the game thread. 48 MB/s is invisible and
        // still crosses the 4 GB band in about a minute and a half.
        // A tick that ran 5.7 s preceded a Vulkan ErrorDeviceLost. An exact 32-bit
        // compare is far cheaper per word than the old float arithmetic, so a smaller
        // slice still covers the band quickly while keeping each tick short.
        // 24 MB/s took the game to ~0.3 fps standing in town. The sweep is a means to an
        // end, not a feature: it only needs to survive long enough to find the matrix
        // arena, and it now stops as soon as it has.
        // Running on a worker thread now, so this is a slice between yields rather than a
        // bite out of a frame. 6 MB/tick on the game thread was the worst of both: it still
        // cost frames and needed ~6 minutes to reach the arena at 0x10EA.
        constexpr u64 kBytesPerPass = 8ull * 1024ull * 1024ull;
        u64           uScanned      = 0;
        const uintptr_t uStarted    = s_uObsCur;

        while (uScanned < kBytesPerPass && s_uObsCur < s_uObsHi) {
            MemoryInfo tInfo {};
            u32        uPage = 0;
            if (R_FAILED(svcQueryMemory(&tInfo, &uPage, s_uObsCur)) || tInfo.size == 0 ||
                tInfo.addr + tInfo.size <= s_uObsCur) {
                break;
            }

            const uintptr_t uRegionEnd = tInfo.addr + tInfo.size;
            const bool      bReadable  = (tInfo.perm & Perm_R) != 0;
            const bool      bWritable  = (tInfo.perm & Perm_W) != 0;
            const bool      bData      = (tInfo.type == MemType_Heap) ||
                                    (tInfo.type == MemType_MappedMemory) ||
                                    (tInfo.type == MemType_CodeMutable) ||
                                    (tInfo.type == MemType_CodeWritable);
            if (!bReadable || !bData || tInfo.size > (2048ull * 1024ull * 1024ull)) {
                s_uObsCur = uRegionEnd;
                continue;
            }
            // Log the map as it goes. Knowing which arenas exist is worth more than one
            // more silent pass -- and a probe that prints nothing cannot be told apart
            // from one that never ran.
            if (s_uObsCur == tInfo.addr && s_nObsRegion < 12) {
                ++s_nObsRegion;
                PRINT("[d3hack-obs] region %p size %uMB type %u perm %u",
                      reinterpret_cast<void *>(tInfo.addr),
                      static_cast<u32>(tInfo.size >> 20), static_cast<u32>(tInfo.type),
                      static_cast<u32>(tInfo.perm))
            }

            const uintptr_t uEnd =
                (uRegionEnd < s_uObsCur + kChunk) ? uRegionEnd : (s_uObsCur + kChunk);
            const auto *pu    = reinterpret_cast<const u32 *>(s_uObsCur);
            const u64   nGo   = (uEnd > s_uObsCur) ? ((uEnd - s_uObsCur) / 4) : 0;
            const u64   nSafe = (uRegionEnd - s_uObsCur) / 4;

            for (u64 i = 0; i < nGo; ++i) {
                if (i + 27 >= nSafe)
                    break;
                const uintptr_t uHere = reinterpret_cast<uintptr_t>(&pu[i]);

                // 1. MAGIC -- an Observer file image, exactly.
                if (pu[i] == 0xDEADBEEFu && pu[i + 1] == 57u) {
                    ++s_nObsMagic;
                    ObsHandle(uHere, true, bWritable);
                    continue;
                }

                // 2. THE PROJECTION MATRIX.
                //
                // Why this and not another value hunt: everything tried so far matched on a
                // numeric SHAPE, and this heap defeats every shape -- axis-aligned pairs,
                // z-dominant vectors, uniform triples, and in .text an unrolled
                // dst[i]=a[i]*t+b[i] blend that reads 0x34/0x38/0x3C purely because they are
                // consecutive. Pointers to the camera turned out to be our own log buffer.
                //
                // A perspective projection matrix cannot be imitated by a sequential sweep.
                // It is TEN elements that are exactly zero in fixed positions, plus a
                // hard +-1.0, plus two positive scales:
                //
                //     m00   0    0    0          m00 = 1 / (aspect * tan(fovy/2))
                //      0   m11   0    0          m11 = 1 / tan(fovy/2)
                //      0    0   m22  -1
                //      0    0   m23   0
                //
                // Ten exact zeros is a far harder constraint than any float relationship,
                // and the four leading zero checks are integer compares, so the scan stays
                // cheap.
                //
                // And it should not race. The VIEW matrix is rebuilt every frame because the
                // camera moves; the PROJECTION matrix depends only on FOV, aspect and the
                // clip planes, so it is typically built once and reused. Shrinking m00/m11
                // widens the FOV, which zooms out -- and if it is not rebuilt, the write
                // simply sticks. That is the one thing writing the eye could never do.
                if ((i & 3) != 0)
                    continue;                       // matrices are 16-byte aligned
                if (i + 16 >= nSafe)
                    continue;

                // Zeros first: cheapest possible rejection, and it removes almost everything.
                auto IsZero = [](u32 u) { return u == 0x00000000u || u == 0x80000000u; };
                if (!IsZero(pu[i + 1]) || !IsZero(pu[i + 2]) || !IsZero(pu[i + 3]) ||
                    !IsZero(pu[i + 4]))
                    continue;
                if (!IsZero(pu[i + 6]) || !IsZero(pu[i + 7]) || !IsZero(pu[i + 8]) ||
                    !IsZero(pu[i + 9]) || !IsZero(pu[i + 12]) || !IsZero(pu[i + 13]))
                    continue;

                // The -1 (or +1) in the w row, in either matrix order.
                const bool bMinusOne11 = (pu[i + 11] == 0xBF800000u || pu[i + 11] == 0x3F800000u);
                const bool bMinusOne14 = (pu[i + 14] == 0xBF800000u || pu[i + 14] == 0x3F800000u);
                if (!bMinusOne11 && !bMinusOne14)
                    continue;
                if (!IsZero(pu[i + 15]))
                    continue;

                const auto *pm = reinterpret_cast<const float *>(&pu[i]);
                if (!FloatIsSane(&pm[0]) || !FloatIsSane(&pm[5]))
                    continue;
                const float m00 = pm[0], m11 = pm[5];
                if (!(m00 > 0.05f && m00 < 20.0f) || !(m11 > 0.05f && m11 < 20.0f))
                    continue;
                if (ObsSeen(uHere))
                    continue;

                ++s_nObsShape;
                if (s_nObsShape > 12)
                    continue;

                // fovy = 2*atan(1/m11); print the tangent instead of calling atan so this
                // stays free of libm, and the aspect falls out of m11/m00.
                PRINT("[d3hack-proj] %p m00=%d.%03d m11=%d.%03d m22=%d.%03d m23=%d.%03d "
                      "aspect=%d.%03d tan(fovy/2)=%d.%03d",
                      reinterpret_cast<void *>(uHere), ObsWhole(m00), ObsMilli(m00),
                      ObsWhole(m11), ObsMilli(m11), ObsWhole(pm[10]), ObsMilli(pm[10]),
                      ObsWhole(bMinusOne11 ? pm[14] : pm[11]),
                      ObsMilli(bMinusOne11 ? pm[14] : pm[11]), ObsWhole(m11 / m00),
                      ObsMilli(m11 / m00), ObsWhole(1.0f / m11), ObsMilli(1.0f / m11))

                // 2b. THE STORED FOV -- the value the matrices are BUILT from.
                //
                // The matrix buffers turned out to be transient: their addresses differ every
                // run and even changed inside one session (0x10EB12C3A0 -> 0x10EAF0D0C0), in
                // groups of six. That is a per-frame uniform ring allocator, which is why
                // rewriting fixed addresses flickers -- the GPU has already moved to a buffer
                // we are not holding. No write rate fixes that.
                //
                // But every one of those rebuilt matrices carries the SAME m11 = 3.171, i.e.
                // tan(fovy/2) = 0.3153, i.e. a 35 degree field of view. That is an authored
                // camera parameter -- it is Angle0 at +0x24 in the .obs format decoded from
                // the PC archives -- so unlike the camera offset it should genuinely be
                // stored. Patch the source and every rebuild comes out wide by itself: no
                // race, no per-frame writes, and nothing to keep scanning.
                //
                // 35 degrees in radians, and its half-angle tangent. Both specific values,
                // not shapes.
                {
                    const u32 ufov = pu[i];
                    float     fv;
                    __builtin_memcpy(&fv, &ufov, 4);
                    const bool bFov = (fv > 0.60781f && fv < 0.61392f);   // 0.6108652 +-0.5%
                    const bool bTan = (fv > 0.31372f && fv < 0.31688f);   // 0.3152988 +-0.5%
                    if ((bFov || bTan) && s_nFovHits < 16 && i >= 8 && (i + 16) < nSafe) {
                        ++s_nFovHits;
                        PRINT("[d3hack-fov] %p = %d.%06d (%s)", reinterpret_cast<void *>(uHere),
                              ObsWhole(fv), static_cast<int>(fv * 1000000.0f) % 1000000,
                              bFov ? "35deg FOV in radians" : "tan(fovy/2)")
                        const float *pc = reinterpret_cast<const float *>(&pu[i]) - 8;
                        for (int q = 0; q < 24; q += 4) {
                            if (!FloatIsSane(&pc[q]) || !FloatIsSane(&pc[q + 1]) ||
                                !FloatIsSane(&pc[q + 2]) || !FloatIsSane(&pc[q + 3]))
                                continue;
                            PRINT("[d3hack-fov]   %+4d: %d.%03d %d.%03d %d.%03d %d.%03d",
                                  (q - 8) * 4, ObsWhole(pc[q]), ObsMilli(pc[q]),
                                  ObsWhole(pc[q + 1]), ObsMilli(pc[q + 1]),
                                  ObsWhole(pc[q + 2]), ObsMilli(pc[q + 2]),
                                  ObsWhole(pc[q + 3]), ObsMilli(pc[q + 3]))
                        }
                        // Widen it. tan(new/2) = tan(old/2) * k, so for the radian form use
                        // the small-angle-free identity via the tangent we already know.
                        const float kf = global_config.rare_cheats.camera_zoom;
                        if (kf > 1.0f && bWritable) {
                            auto *pw = const_cast<float *>(reinterpret_cast<const float *>(&pu[i]));
                            // 35 deg -> 64.5 deg at k=2 (tan 17.5 * 2 = 0.6306, atan -> 32.24)
                            const float flNew = bTan ? (fv * kf)
                                                     : (fv * (1.0f + (kf - 1.0f) * 0.84f));
                            *pw = flNew;
                            PRINT("[d3hack-fov] WIDENED %p: %d.%06d -> %d.%06d",
                                  reinterpret_cast<void *>(uHere), ObsWhole(fv),
                                  static_cast<int>(fv * 1000000.0f) % 1000000, ObsWhole(flNew),
                                  static_cast<int>(flNew * 1000000.0f) % 1000000)
                        }
                    }
                }

                // Only the GAME's matrices, never the UI's.
                //
                // The first run patched two identity matrices (m00 = m11 = 1.0, aspect 1.0,
                // m22 = +1.0) along with the real ones. Those are ortho/UI transforms and
                // widening them would distort the interface. The real one is unmistakable:
                // aspect 1.777 (16:9) and m22 negative -- and tan(fovy/2) = 0.315 puts the
                // field of view at 35.0 degrees, exactly the FOV in the PC `observer` asset
                // decoded from the archives this morning. Same camera, different platform.
                const float flAspect = m11 / m00;
                const bool  bGameCam = (flAspect > 1.3f && flAspect < 2.4f) && (pm[10] < 0.0f);
                if (!bGameCam || !bWritable)
                    continue;

                // Remember the arena. These sit in a small cluster of uniform buffers, and
                // the game rotates through them, so re-applying has to cover the span rather
                // than a fixed address.
                if (s_nProjSlot < 16) {
                    bool bHave = false;
                    for (int q = 0; q < s_nProjSlot; ++q)
                        if (s_arProjSlot[q] == uHere)
                            bHave = true;
                    if (!bHave)
                        s_arProjSlot[s_nProjSlot++] = uHere;
                }
                if (s_uProjLo == 0ull || uHere < s_uProjLo)
                    s_uProjLo = uHere;
                if (uHere + 0x40 > s_uProjHi)
                    s_uProjHi = uHere + 0x40;
                s_flProjM00 = m00;
                s_flProjM11 = m11;
            }

            uScanned += (uEnd - s_uObsCur);
            s_uObsCur = uEnd;
        }

        // Progress every tick, so this can never go quiet mid-sweep again.
        if (s_uProjLo == 0ull)
            PRINT("[d3hack-obs] pass %d at %p (+%uMB): %d matrices so far", s_nObsPass + 1,
                  reinterpret_cast<void *>(s_uObsCur),
                  static_cast<u32>((s_uObsCur - uStarted) >> 20), s_nObsShape)

        if (s_uObsCur >= s_uObsHi || s_uObsCur == uStarted) {
            // Nothing in the narrow window? Then the assumption about where the arena lives
            // is wrong for this session -- widen once rather than silently reporting a
            // negative that is really "did not look".
            if (s_uProjLo == 0ull && s_uObsHi < s_uObsAnchor + (1ull << 30)) {
                PRINT("[d3hack-obs] narrow window empty, widening to the full band%s", "")
                s_uObsLo  = (s_uObsAnchor > (2ull << 30)) ? (s_uObsAnchor - (2ull << 30)) : 0ull;
                s_uObsHi  = s_uObsAnchor + (2ull << 30);
                s_uObsCur = s_uObsLo;
                return;
            }
            s_bObsScanDone = true;
            ++s_nObsPass;
            PRINT("[d3hack-obs] pass %d finished: %d magic, %d exact offset, %d ingredient, "
                  "%d recorded", s_nObsPass, s_nObsMagic, s_nObsShape, s_nObsIngr, s_nObs)
            ObsApplyZoom();
        }
    }

    inline void CameraHeapScan(float flPx, float flPy) {
        if (s_bSweepDone)
            return;

        // d3hack-custom: BOUNDED, RE-QUERIED sweep.
        //
        // Two crashes came from walking the whole address space: svcQueryMemory tells you a
        // region was mapped WHEN ASKED, not that it stays mapped, and the signature filter
        // is slow enough per region to widen that window until another thread unmaps
        // something under us. Both faults were `InvalidAccessHandler at 0x0` on a thread
        // other than the scanner, before any output.
        //
        // Fix both halves:
        //   1. Anchor the sweep to a KNOWN-GOOD live pointer -- the player's ACD -- and only
        //      look +/-48 MB around it. Every camera found so far (0x10e492f930,
        //      0x10e499aad0, 0x10e536c610, 0x10e79a5bd0, 0x10e8c6b3a4) sits in that band,
        //      so this costs nothing in coverage and removes terabytes of exposure.
        //   2. Re-query immediately before each 1 MB chunk, so the gap between "is it
        //      mapped?" and "read it" is as short as it can be.
        void *pAnchorAcd = nullptr;
        if (GetPrimaryPlayer != nullptr && ACDTryToGet != nullptr)
            pAnchorAcd = ACDTryToGet(GetPrimaryPlayer());
        if (reinterpret_cast<uintptr_t>(pAnchorAcd) < 0x10000ull)
            return;
        const uintptr_t uAnchor = reinterpret_cast<uintptr_t>(pAnchorAcd);
        // +/-48 MB was too tight: it swept ~0x10e5d0.. to 0x10e8d0.. and found ONE
        // candidate, while cameras have been seen at 0x10e79a5bd0 and 0x10e8c6b3a4 --
        // outside the band in some sessions. The signature filter is selective enough
        // (1 hit per 96 MB, versus 2048 for the old generic one) that a much wider band
        // is nearly free.
        const uintptr_t uHi     = uAnchor + (320ull << 20);
        // Sweep the FULL band, both directions. Starting at the anchor and going up was
        // an inference from a single session's addresses, and this session's camera was
        // evidently below it -- 320 MB swept, zero candidates. With the fast budget the
        // whole band is affordable, so do not get clever about direction.
        const uintptr_t uLo = (uAnchor > (320ull << 20)) ? (uAnchor - (320ull << 20)) : 0ull;
        if (s_uScanCur < uLo || s_uScanCur > uHi)
            s_uScanCur = uLo;

        constexpr u64 kChunk        = 1ull << 20;   // 1 MB at a time
        // 12 MB per 3s tick means ~2.5 minutes to cross a 640 MB band. The per-READ
        // exposure is what caused the crashes and that is fixed by the 1 MB re-query, not
        // by the per-pass budget -- so raising this covers ground faster without widening
        // the dangerous window at all.
        constexpr u64 kBytesPerPass = 64ull * 1024ull * 1024ull;
        u64           uScanned      = 0;

        while (uScanned < kBytesPerPass && s_nCand < 2048 && s_uScanCur < uHi) {
            MemoryInfo tInfo {};
            u32        uPage = 0;
            if (R_FAILED(svcQueryMemory(&tInfo, &uPage, s_uScanCur)) ||
                tInfo.addr + tInfo.size <= s_uScanCur || tInfo.size == 0) {
                // End of the address space, or a query we cannot follow. Finish the sweep
                // instead of silently stopping -- a probe that goes quiet is indistinguish-
                // able from one that never ran, which has cost several runs tonight.
                s_bSweepDone = true;
                s_flSweepPx  = flPx;
                s_flSweepPy  = flPy;
                PRINT("[d3hack-scan] sweep ended at %p: %d candidates",
                      reinterpret_cast<void *>(s_uScanCur), s_nCand)
                return;
            }

            const bool bReadable = (tInfo.perm & Perm_R) != 0;
            // Clamp the slice to one chunk and to the anchored band, so a single read never
            // covers more than 1 MB of memory that was queried a moment ago.
            const uintptr_t uEnd = (tInfo.addr + tInfo.size < s_uScanCur + kChunk)
                                       ? (tInfo.addr + tInfo.size)
                                       : (s_uScanCur + kChunk);
            const bool bData     = (tInfo.type == MemType_Heap) || (tInfo.type == MemType_MappedMemory) ||
                                   (tInfo.type == MemType_CodeMutable) || (tInfo.type == MemType_CodeWritable);
            if (bReadable && bData && tInfo.size <= (256ull * 1024ull * 1024ull)) {
                const auto *pf = reinterpret_cast<const float *>(s_uScanCur);
                const u64   n  = (uEnd > s_uScanCur) ? ((uEnd - s_uScanCur) / 4) : 0;
                // d3hack-custom: search for the CAMERA SIGNATURE, not "floats near the
                // player".
                //
                // The loose filter matched every monster, prop and item nearby and filled
                // 2048 slots before the sweep even reached the camera's region -- "pool
                // FULL at 0x10e5465000" while the camera sat elsewhere. Generic filters
                // cannot work here; there is simply too much world geometry near a player.
                //
                // But the camera's shape is now MEASURED, so match it directly:
                //     target vec4 at A        -- sits ON the player
                //     eye    vec4 at A + 0x20 -- 30..200 units away, 15..150 units ABOVE
                // Every one of those must hold at once. Scenery satisfies none of them.
                for (u64 i = 0; i + 12 < n && s_nCand < 2048; ++i) {
                    const float *t = &pf[i];
                    const float *e = &pf[i + 8];   // +0x20
                    if (!FloatIsSane(&t[0]) || !FloatIsSane(&t[1]) || !FloatIsSane(&t[2]))
                        continue;
                    if (!FloatIsSane(&e[0]) || !FloatIsSane(&e[1]) || !FloatIsSane(&e[2]))
                        continue;

                    const float tx = t[0] - flPx;
                    const float ty = t[1] - flPy;
                    // Tolerances widened: every bound here came from ONE measured camera
                    // (offset 48/48/50, target exactly on the player). The look-at may lead
                    // the player slightly, and other cameras may sit closer or steeper.
                    if (!((tx * tx + ty * ty) < (45.0f * 45.0f)))
                        continue;                      // look-at roughly on the player

                    const float ox = e[0] - t[0];
                    const float oy = e[1] - t[1];
                    const float oz = e[2] - t[2];
                    const float m2 = ox * ox + oy * oy + oz * oz;
                    if (!(m2 > (20.0f * 20.0f) && m2 < (400.0f * 400.0f)))
                        continue;                      // camera-like distance
                    if (!(oz > 6.0f && oz < 300.0f))
                        continue;                      // and above, not beside

                    // d3hack-custom: EXACT-SIGNATURE FAST PATH.
                    //
                    // The camera's offset has been byte-identical in every session it has
                    // ever been found in: (48.005, 48.005, 50.108). That is a far tighter
                    // fingerprint than "target on player, eye above it", and it needs no
                    // movement-based verify at all -- which is the flaky stage (it depends
                    // on how far the player happens to have walked between sweep and check,
                    // and has been throwing away good candidates all night).
                    //
                    // If the offset matches that, it IS the camera. Lock immediately.
                    if (s_uCamEye == 0ull &&
                        ox > 47.5f && ox < 48.5f &&
                        oy > 47.5f && oy < 48.5f &&
                        oz > 49.6f && oz < 50.6f) {
                        s_uCamEye  = reinterpret_cast<uintptr_t>(e);
                        s_flOrigOx = ox;
                        s_flOrigOy = oy;
                        s_flOrigOz = oz;
                        PRINT("[d3hack-cam!] EXACT MATCH, locked immediately: eye=%p off(%dm,%dm,%dm)",
                              reinterpret_cast<void *>(s_uCamEye),
                              static_cast<int>(ox * 1000.0f), static_cast<int>(oy * 1000.0f),
                              static_cast<int>(oz * 1000.0f))
                        const uintptr_t uB = exl::util::modules::GetTargetStart();
                        const auto     *pD = reinterpret_cast<const uintptr_t *>(uB + 0x1098000ull);
                        int             nF = 0;
                        for (u64 d = 0; d < (0xC4A14ull / 8ull) && nF < 12; ++d) {
                            const uintptr_t v = pD[d];
                            if (v > s_uCamEye + 0x1000ull || v + 0x100000ull < s_uCamEye)
                                continue;
                            ++nF;
                            PRINT("[d3hack-ptr] .data+0x%X -> %p (eye +0x%X)",
                                  static_cast<u32>(0x1098000ull + d * 8ull),
                                  reinterpret_cast<void *>(v),
                                  static_cast<u32>(s_uCamEye - v))
                        }
                        PRINT("[d3hack-ptr] %d static pointers reach it", nF)

                        // d3hack-custom: find the stored offset NEXT TO the camera.
                        //
                        // Sweeping 640 MB of live heap for the triple keeps crashing, and it
                        // always will: svcQueryMemory cannot promise a region stays mapped,
                        // and every extra byte scanned is more exposure. But the value the
                        // game restores each frame is a camera PARAMETER -- it almost
                        // certainly lives in or beside the camera object we have just
                        // locked, which is memory we know is valid.
                        //
                        // +/-64 KB around a known-good pointer: instant, safe, and far more
                        // likely than trawling the heap.
                        {
                            const uintptr_t uFrom = (s_uCamEye > 0x10000ull) ? (s_uCamEye - 0x10000ull) : 0ull;
                            const auto     *pw    = reinterpret_cast<const float *>(uFrom);
                            const u64       nw    = 0x20000ull / 4ull;
                            int             nT    = 0;
                            for (u64 q = 0; q + 3 < nw && nT < 12; ++q) {
                                if (!FloatIsSane(&pw[q]) || !FloatIsSane(&pw[q + 1]) ||
                                    !FloatIsSane(&pw[q + 2]))
                                    continue;
                                const float a = pw[q], b = pw[q + 1], c = pw[q + 2];
                                if (!(a > 47.9f && a < 48.1f && b > 47.9f && b < 48.1f &&
                                      c > 50.0f && c < 50.2f))
                                    continue;
                                ++nT;
                                PRINT("[d3hack-triple] %p = (%dm,%dm,%dm)  eye%+d",
                                      reinterpret_cast<const void *>(&pw[q]),
                                      static_cast<int>(a * 1000.0f),
                                      static_cast<int>(b * 1000.0f),
                                      static_cast<int>(c * 1000.0f),
                                      static_cast<int>(reinterpret_cast<uintptr_t>(&pw[q]) - s_uCamEye))
                            }
                            PRINT("[d3hack-triple] %d offset triples within 64KB of the camera", nT)

                            // No vec3 near the camera, and no static pointer to it. So the
                            // offset is COMPUTED, not stored -- and the numbers say from
                            // what:
                            //     distance = sqrt(48.005^2 + 48.005^2 + 50.108^2) = 84.379
                            //     yaw      = 45 deg   (x == y exactly: a perfect diagonal)
                            //     pitch    = atan2(50.108, 67.89) = 36.4 deg = 0.6355 rad
                            // A yaw/pitch/distance camera yields that vector precisely, and
                            // none of those three would ever appear as a (48,48,50) triple.
                            // Look for each encoding instead, in the same safe window.
                            int nP = 0;
                            for (u64 q = 0; q < nw && nP < 16; ++q) {
                                if (!FloatIsSane(&pw[q]))
                                    continue;
                                const float v = pw[q];
                                const char *what = nullptr;
                                if (v > 84.30f && v < 84.46f)       what = "distance 84.379";
                                else if (v > 0.630f && v < 0.641f)  what = "pitch rad 0.6355";
                                else if (v > 36.30f && v < 36.50f)  what = "pitch deg 36.4";
                                else if (v > 0.780f && v < 0.791f)  what = "yaw rad 0.7854";
                                else if (v > 44.95f && v < 45.05f)  what = "yaw deg 45";
                                else if (v > 67.80f && v < 67.98f)  what = "ground dist 67.89";
                                if (what == nullptr)
                                    continue;
                                ++nP;
                                PRINT("[d3hack-polar] %p = %dm  <- %s  eye%+d",
                                      reinterpret_cast<const void *>(&pw[q]),
                                      static_cast<int>(v * 1000.0f), what,
                                      static_cast<int>(reinterpret_cast<uintptr_t>(&pw[q]) - s_uCamEye))
                            }
                            PRINT("[d3hack-polar] %d polar-form candidates near the camera", nP)
                        }
                    }

                    s_arCand[s_nCand].uAddr = reinterpret_cast<uintptr_t>(e);
                    s_arCand[s_nCand].flX   = e[0];
                    s_arCand[s_nCand].flY   = e[1];
                    ++s_nCand;
                }

                uScanned += (uEnd - s_uScanCur);
            }
            s_uScanCur = (uEnd > s_uScanCur) ? uEnd : (s_uScanCur + kChunk);
        }

        // Heartbeat: silence must never be the only signal.
        static int s_nSweepTick = 0;
        if ((++s_nSweepTick % 4) == 0)
            PRINT("[d3hack-scan] sweeping... at %p, %d candidates so far",
                  reinterpret_cast<void *>(s_uScanCur), s_nCand)

        if (s_nCand >= 2048 || s_uScanCur >= uHi) {
            if (s_nCand >= 2048)
                PRINT("[d3hack-scan] candidate pool FULL at %p -- filter too loose%s",
                      reinterpret_cast<void *>(s_uScanCur), "")
            s_bSweepDone = true;

            // RE-BASELINE. Candidates are collected across the whole sweep -- ten seconds
            // of walking -- each storing its value at the moment it was found. The player
            // reference, though, is taken here at sweep completion. Comparing a camera's
            // movement over one window against the player's over a shorter one can never
            // agree in magnitude, which is exactly why 25 candidates all failed verify.
            //
            // Re-read every candidate NOW so both sides start from the same instant.
            for (int c = 0; c < s_nCand; ++c) {
                const auto *pc = reinterpret_cast<const float *>(s_arCand[c].uAddr);
                if (FloatIsSane(&pc[0]) && FloatIsSane(&pc[1])) {
                    s_arCand[c].flX = pc[0];
                    s_arCand[c].flY = pc[1];
                }
            }
            s_flSweepPx = flPx;
            s_flSweepPy = flPy;
            PRINT("[d3hack-scan] sweep done: %d candidates, re-baselined at player (%d, %d)",
                  s_nCand, static_cast<int>(flPx), static_cast<int>(flPy))
        }
    }

    inline void CameraHeapVerify(float flPx, float flPy) {
        if (!s_bSweepDone || s_nCand == 0)
            return;
        const float mdx = flPx - s_flSweepPx;
        const float mdy = flPy - s_flSweepPy;
        const float amx = (mdx < 0.0f) ? -mdx : mdx;
        if (!(amx > 20.0f))
            return;  // wait until the player has actually gone somewhere

        int nKept = 0;
        for (int i = 0; i < s_nCand && nKept < 4; ++i) {
            const auto *pf = reinterpret_cast<const float *>(s_arCand[i].uAddr);
            if (!FloatIsSane(&pf[0]) || !FloatIsSane(&pf[1]))
                continue;
            // The camera TRACKS the player, so it must still be beside him after the
            // walk -- not merely have moved in the right direction. The first verify
            // pass reported 16 "hits" that were huge garbage floats in one freed buffer
            // (printing as INT_MIN): their deltas were enormous and happened to carry the
            // right sign, which the direction test alone could not reject.
            //
            // Proximity AFTER the move is the constraint that garbage cannot fake.
            const float nx = pf[0] - flPx;
            const float ny = pf[1] - flPy;
            const float anx = (nx < 0.0f) ? -nx : nx;
            const float any = (ny < 0.0f) ? -ny : ny;
            if (!(anx < 400.0f && any < 400.0f))
                continue;

            const float cdx = pf[0] - s_arCand[i].flX;
            const float cdy = pf[1] - s_arCand[i].flY;
            // And it must have moved a BELIEVABLE amount: an eased follower covers most of
            // the player's displacement, never fifty times it.
            const float acdx = (cdx < 0.0f) ? -cdx : cdx;
            if (!(acdx < amx * 3.0f))
                continue;
            // Per-axis sign tests were too brittle: walking mostly along one axis leaves
            // the other axis' delta near zero, and a near-zero delta fails a
            // "must exceed 30% of the player's" test on that axis, killing real hits. One
            // run found 8 candidates, the next found 0 with the same code.
            //
            // Compare 2D DISPLACEMENT MAGNITUDE instead, which does not care which way the
            // walk went, plus a dot-product sign so it must move WITH the player and not
            // against him.
            const float camMag = __builtin_sqrtf(cdx * cdx + cdy * cdy);
            const float plMag  = __builtin_sqrtf(mdx * mdx + mdy * mdy);
            if (!(camMag > plMag * 0.25f && camMag < plMag * 2.5f))
                continue;
            if (!((cdx * mdx + cdy * mdy) > 0.0f))
                continue;
            ++nKept;
            // d3hack-custom: dump the surrounding struct for the survivors.
            //
            // A candidate at a FIXED diagonal offset, high above the player, is an
            // isometric camera -- 0x10e539f8f0 showed off(+49,+50) while the off(-1,-1)
            // cluster is just the player's own position duplicated. What we actually need
            // is not the position (it is recomputed every frame, so writing it would be
            // overwritten) but the DISTANCE/FOV field that generates the offset. That
            // lives in the same struct, so print its neighbourhood: an FOV shows up as a
            // small float around 0.6-1.2 (radians), a distance as tens of units.
            // Only dump structs for candidates at a REAL offset. The off(-1,-1) cluster is
            // the player's own render transform duplicated -- it crowds the log and is not
            // the camera. A genuine camera sits tens of units away, like the off(103,110)
            // and off(49,50) hits.
            const float offMag = __builtin_sqrtf(nx * nx + ny * ny);
            if (offMag < 20.0f)
                continue;
            {
                const auto *pn = reinterpret_cast<const float *>(s_arCand[i].uAddr - 0x40);
                for (int r = 0; r < 16; ++r) {
                    const int k = r * 4;
                    u32 b0, b1, b2, b3;
                    __builtin_memcpy(&b0, &pn[k + 0], 4);
                    __builtin_memcpy(&b1, &pn[k + 1], 4);
                    __builtin_memcpy(&b2, &pn[k + 2], 4);
                    __builtin_memcpy(&b3, &pn[k + 3], 4);
                    PRINT("[d3hack-cs] %p+%02X: %08X %08X %08X %08X (%dm %dm %dm %dm)",
                          reinterpret_cast<void *>(s_arCand[i].uAddr - 0x40), k * 4,
                          b0, b1, b2, b3,
                          static_cast<int>(pn[k + 0] * 1000.0f), static_cast<int>(pn[k + 1] * 1000.0f),
                          static_cast<int>(pn[k + 2] * 1000.0f), static_cast<int>(pn[k + 3] * 1000.0f))
                }
            }
            // Lock on to a candidate that really looks like a view transform: a target
            // vec4 sits 0x20 before the eye vec4, both with w ~ 1.0, and the eye is well
            // above the target. Coincidental float pairs do not satisfy all three.
            if (s_uCamEye == 0ull) {
                const auto *pe = reinterpret_cast<const float *>(s_arCand[i].uAddr);
                const auto *pt = reinterpret_cast<const float *>(s_arCand[i].uAddr - 0x20);
                // Print the lock inputs unconditionally. Last run produced a textbook
                // candidate -- off(50,48) -- and no lock, and guessing which clause
                // rejected it would just burn another walk.
                u32 bw_e, bw_t;
                __builtin_memcpy(&bw_e, &pe[3], 4);
                __builtin_memcpy(&bw_t, &pt[3], 4);
                PRINT("[d3hack-lock?] eye=%p ew=%08X tw=%08X dz=%dm tgt(%d,%d) player(%d,%d)",
                      reinterpret_cast<void *>(s_arCand[i].uAddr), bw_e, bw_t,
                      static_cast<int>((pe[2] - pt[2]) * 1000.0f),
                      static_cast<int>(pt[0]), static_cast<int>(pt[1]),
                      static_cast<int>(flPx), static_cast<int>(flPy))

                // Relaxed: the decisive signal is that the LOOK-AT sits on the player and
                // the eye is above it. The w components were a guess from one sample and
                // are not worth failing a real camera over.
                const float tdx = pt[0] - flPx;
                const float tdy = pt[1] - flPy;
                const float atdx = (tdx < 0.0f) ? -tdx : tdx;
                const float atdy = (tdy < 0.0f) ? -tdy : tdy;
                if (FloatIsSane(&pe[2]) && FloatIsSane(&pt[2]) &&
                    atdx < 60.0f && atdy < 60.0f && (pe[2] - pt[2]) > 8.0f) {
                    s_uCamEye = s_arCand[i].uAddr;
                    // d3hack-custom: POINTER SCAN -- find a stable path to this object.
                    //
                    // The camera is heap-allocated so its address differs every launch,
                    // which is why we re-scan each session. But the game finds it somehow,
                    // so a pointer to it must live at a FIXED module offset. Sweep the
                    // module's .data (offset 0x1098000, size 0xC4A14) for any 8-byte value
                    // landing inside this object, and report it module-relative. Once one
                    // of those offsets proves stable across launches, the scanner becomes
                    // unnecessary: read the global, add the delta, done.
                    {
                        const uintptr_t uBase = exl::util::modules::GetTargetStart();
                        const auto     *pD    = reinterpret_cast<const uintptr_t *>(uBase + 0x1098000ull);
                        const u64       nD    = 0xC4A14ull / 8ull;
                        int             nFound = 0;
                        for (u64 d = 0; d < nD && nFound < 12; ++d) {
                            const uintptr_t v = pD[d];
                            // 8 KB was too narrow -- it assumed the eye sits near the start
                            // of its object and found nothing. If the camera is a field deep
                            // inside a renderer or scene manager, the base pointer is far
                            // further back. Accept anything landing within 1 MB before the
                            // eye (and a little after), and report the delta so a stable
                            // one can be recognised across launches.
                            if (v > s_uCamEye + 0x1000ull || v + 0x100000ull < s_uCamEye)
                                continue;
                            ++nFound;
                            PRINT("[d3hack-ptr] .data+0x%X -> %p (eye is +0x%X into it)",
                                  static_cast<u32>(0x1098000ull + d * 8ull),
                                  reinterpret_cast<void *>(v),
                                  static_cast<u32>(s_uCamEye - v))
                        }
                        PRINT("[d3hack-ptr] %d static pointers reach the camera object", nFound)
                    }
                    s_flOrigOx = pe[0] - pt[0];
                    s_flOrigOy = pe[1] - pt[1];
                    s_flOrigOz = pe[2] - pt[2];
                    PRINT("[d3hack-cam!] LOCKED eye=%p offset(%dm,%dm,%dm)",
                          reinterpret_cast<void *>(s_uCamEye),
                          static_cast<int>((pe[0] - pt[0]) * 1000.0f),
                          static_cast<int>((pe[1] - pt[1]) * 1000.0f),
                          static_cast<int>((pe[2] - pt[2]) * 1000.0f))
                }
            }
            PRINT("[d3hack-scan] CAMERA? %p cam(%d,%d) player(%d,%d) off(%d,%d)",
                  reinterpret_cast<void *>(s_arCand[i].uAddr),
                  static_cast<int>(pf[0]), static_cast<int>(pf[1]),
                  static_cast<int>(flPx), static_cast<int>(flPy),
                  static_cast<int>(pf[0] - flPx), static_cast<int>(pf[1] - flPy))
        }
        PRINT("[d3hack-scan] %d of %d candidates track the player", nKept, s_nCand)
        if (nKept == 0) {
            // Sweep again rather than stopping. Candidates differ every session and every
            // walk, so a single failed pass says nothing -- retrying costs the player
            // nothing but time already being spent walking.
            s_nCand      = 0;
            s_bSweepDone = false;
            s_uScanCur   = 0ull;
            PRINT("[d3hack-scan] retrying sweep%s", "")
            return;
        }
        // One report, then idle. The previous version reset the cursor to the OLD end
        // marker (1 TB) and immediately "swept" empty space again, logging a bogus
        // "0 candidates" that looked like a real result.
        s_nCand = 0;
    }

    inline bool  s_bCamWalkArmed = false;   // d3hack-custom: camera-by-walking probe
    inline bool  s_bCamWalkHave  = false;
    inline u64   s_uCamWalkTick  = 0ull;
    inline float s_arCamWalk[2048] {};

    inline int   s_nCamWalkPass = 0;

    inline bool s_bCameraPending  = false;
    inline u64  s_uCameraArmTick  = 0ull;

    inline void ReportAndResetLootTally() {
        g_bWorldEntered = true;

        // d3hack-custom: main-view zoom.
        //
        // The observer route was a dead end -- GlobalSNOGet never resolves an Observer
        // (0x407..0x422) in-world, proven with a working control. This is the other end of
        // the same system: 0x93BE80, reached from the CameraSetZoom Lua binding, and it
        // clamps its argument to [-1.0, 1.0]:
        //
        //     fmov s0,#1.0 / fmov s1,#-1.0 / fmin s0,s9,s0 / fcsel s0,s1,s0,mi
        //
        // So zoom is NORMALISED against the game's own range, not a free multiplier. That
        // caps how far out we can go -- whatever the engine already allows -- but it also
        // means no clamp has to be defeated and nothing can be driven out of bounds.
        //
        // Applied per world because a zoom set this way is camera state, not config, and
        // anything that rebuilds the camera (loading a rift, a cutscene) would drop it.
        // d3hack-custom: arm the camera-by-walking probe for this world.
        // The camera lock is what supplies the exact offset bits the sweep searches for, so
        // the scan must arm it as well -- CameraObserverDump was off, which is why the log
        // had zero d3hack-scan lines and the sweep was left guessing at values.
        if (global_config.rare_cheats.active && (global_config.rare_cheats.camera_observer_dump ||
                                                 global_config.rare_cheats.camera_observer_scan ||
                                                 global_config.rare_cheats.camera_trap)) {
            s_bCamWalkArmed = true;
            s_bCamWalkHave  = false;
            s_uCamWalkTick  = 0ull;
            s_nCamWalkPass  = 0;
        }

        // d3hack-custom: re-arm the Observer asset scan for this world. The recorded hit
        // table is deliberately NOT cleared -- assets survive a world change, and clearing
        // it would let the same asset be scaled a second time.
        // Re-arm at most three times. Assets load with a world, so a later world may bring
        // the gameplay observer in after an earlier sweep finished -- but the first version
        // re-armed on EVERY world change, which reset the cursor to 0 twice in one minute
        // and guaranteed the sweep never completed.
        if (global_config.rare_cheats.active && global_config.rare_cheats.camera_observer_scan &&
            s_nObsPass < 3 && s_uProjLo == 0ull) {
            s_bObsScanDone = false;
            s_uObsLo       = 0ull;
            s_uObsHi       = 0ull;
            s_uObsCur      = 0ull;
            s_uObsTick     = 0ull;
            s_nObsShape    = 0;
        }

        // CameraSetZoom (0x93BE80) is NOT the camera. Its body ends
        //     str s0,[sp,#8] / mov w1,#0x150 / bl 0x71DA90
        // i.e. it packs the value into a message and enqueues it -- the scripted-sequence
        // path used by cutscenes. It ran cleanly three times per world with zoom=1.0 and
        // changed nothing on screen, which is exactly what a queued cutscene command does
        // during normal play. Kept only because it costs nothing and may be useful for a
        // scripted effect later; it is off by default.
        if (CameraSetZoomValue != nullptr && global_config.rare_cheats.active &&
            global_config.rare_cheats.camera_zoom != 0.0f) {
            CameraSetZoomValue(0, global_config.rare_cheats.camera_zoom, 0.0f);
            s_bCameraPending = true;   // and again later, once a connection exists
            s_uCameraArmTick = svcGetSystemTick();
        }

        // d3hack-custom: the NATIVE camera. 0x93C010 resolves an Observer asset by name
        // (SNO group 0x1A) and applies it to a view -- no message queue, which is the
        // difference that matters. The game ships alternates: CONSOLE_WIDE, CONSOLE_ZOOMED,
        // WIDE, ZOOMED, each with a retro twin.
        //
        // The observer GlobalSNOs are never resolved by the game in-world, but nothing
        // stops US resolving them: GlobalSNOGet works fine when called directly, exactly as
        // ReportPowerSnos already does for a power.
        if (global_config.rare_cheats.active && GlobalSNOGet != nullptr &&
            SNOToString != nullptr) {
            if (global_config.rare_cheats.camera_observer_dump) {
                static bool s_bDumped = false;
                if (!s_bDumped) {
                    s_bDumped = true;

                    // d3hack-custom: read the Observer asset table directly.
                    //
                    // The previous attempt called 0x7498B0 for the pointer. That function
                    // returns a BOOL -- it computes the pointer and then does
                    // `cmp x8,#0 / cset w8,ne`. It handed back 0x1, the null check passed
                    // because 1 != 0, and dereferencing address 1 crashed the game. I had
                    // read the `ldr x8,[x8,w1,uxtw #3]` in its middle and stopped before
                    // the return.
                    //
                    // So no unknown-signature call this time. Both 0x7498B0 and 0x749990
                    // open with the same two instructions:
                    //     adrp x10,#0x1A21000 / ldr x10,[x10,#0x6F0]
                    // and index [mgr+0x11E0][sno], bounded by [mgr+0x11E8]. That is four
                    // reads I can bounds-check myself, and the bound is the game's own.
                    //
                    // Every dereference below is guarded, and a pointer must look like a
                    // pointer (>= 0x10000) before it is touched -- which is exactly the
                    // check whose absence turned a bad read into a crash.
                    auto **ppMgr = reinterpret_cast<void **>(GameOffsetFromTable("sno_asset_mgr_ptr"));
                    void  *pMgr  = (ppMgr != nullptr) ? *ppMgr : nullptr;
                    if (reinterpret_cast<uintptr_t>(pMgr) < 0x10000ull) {
                        PRINT("[d3hack-obs] sno asset manager not ready (%p)", pMgr)
                    } else {
                        auto *pu       = reinterpret_cast<u8 *>(pMgr);
                        const u32 uCnt = *reinterpret_cast<const u32 *>(pu + 0x11E8);
                        auto **ppArr   = *reinterpret_cast<void ***>(pu + 0x11E0);
                        PRINT("[d3hack-obs] mgr=%p count=%u arr=%p", pMgr, uCnt,
                              static_cast<void *>(ppArr))

                        const int arIds[2] = {0x409, 0x40F};
                        for (int i = 0; i < 2; ++i) {
                            const int nSno = static_cast<int>(GlobalSNOGet(static_cast<SNO>(arIds[i])));
                            if (ppArr == nullptr || nSno < 0 || static_cast<u32>(nSno) >= uCnt) {
                                // Expected outcome if these SNOs live past the array: the
                                // real lookup falls back to a hash path for exactly that
                                // case. Reporting it beats guessing at the hash.
                                PRINT("[d3hack-obs] id=0x%X sno=%d OUT OF ARRAY (count=%u)",
                                      arIds[i], nSno, uCnt)
                                continue;
                            }
                            auto *p = reinterpret_cast<u8 *>(ppArr[nSno]);
                            if (reinterpret_cast<uintptr_t>(p) < 0x10000ull) {
                                PRINT("[d3hack-obs] id=0x%X sno=%d not resident (%p)",
                                      arIds[i], nSno, static_cast<void *>(p))
                                continue;
                            }
                            PRINT("[d3hack-obs] id=0x%X sno=%d asset=%p", arIds[i], nSno,
                                  static_cast<void *>(p))

                            // d3hack-custom: look for the camera constants INSIDE the
                            // Observer asset.
                            //
                            // 48.005 / 50.108 / 84.379 / 0.6355 appear NOWHERE in the
                            // binary -- not .text, not .rodata, not .data (searched offline
                            // in the dumps). They are not in the camera object, no static
                            // pointer reaches it, and they are not stored in polar form
                            // beside it either. So they are LOADED DATA, and the Observer
                            // asset is the camera definition -- which is exactly where this
                            // hunt started before its container format defeated me.
                            //
                            // The difference now is that we know the precise values to look
                            // for, so the container layout does not have to be understood at
                            // all. Scan a safe window of the asset for them.
                            {
                                const auto *pa = reinterpret_cast<const float *>(p);
                                int         nH = 0;
                                for (u64 q = 0; q < (0x8000ull / 4ull) && nH < 12; ++q) {
                                    if (!FloatIsSane(&pa[q]))
                                        continue;
                                    const float v = pa[q];
                                    const char *w = nullptr;
                                    if (v > 47.9f && v < 48.1f)        w = "48.005 (x/y offset)";
                                    else if (v > 50.0f && v < 50.2f)   w = "50.108 (z offset)";
                                    else if (v > 84.30f && v < 84.46f) w = "84.379 (distance)";
                                    else if (v > 0.630f && v < 0.641f) w = "0.6355 (pitch rad)";
                                    if (w == nullptr)
                                        continue;
                                    ++nH;
                                    PRINT("[d3hack-obsc] id=0x%X +0x%X = %dm  <- %s",
                                          arIds[i], static_cast<u32>(q * 4),
                                          static_cast<int>(v * 1000.0f), w)
                                }
                                PRINT("[d3hack-obsc] id=0x%X: %d camera constants in the asset",
                                      arIds[i], nH)
                            }
                            // arr[sno] is a CONTAINER, not the payload: it is a run of
                            // 0x18-byte records {u64 key, void *ptr, u64 0x10} whose ptr
                            // field steps by 8 each record. The camera parameters are one
                            // level down, behind those pointers. Follow the first few --
                            // guarded the same way, since the lesson from the 0x1 crash is
                            // that a value is not a pointer until it looks like one.
                            const auto *w = reinterpret_cast<const u32 *>(p);
                            for (int row = 0; row < 4; ++row) {
                                const int k = row * 4;
                                PRINT("[d3hack-obs]  +%02X: %08X %08X %08X %08X", k * 4,
                                      w[k + 0], w[k + 1], w[k + 2], w[k + 3])
                            }
                            for (int rec = 0; rec < 3; ++rec) {
                                auto *pSub = *reinterpret_cast<u8 **>(p + rec * 0x18 + 8);
                                if (reinterpret_cast<uintptr_t>(pSub) < 0x10000ull) {
                                    PRINT("[d3hack-obs]   rec%d sub=%p (skipped)", rec,
                                          static_cast<void *>(pSub))
                                    continue;
                                }
                                const auto *sw = reinterpret_cast<const u32 *>(pSub);
                                PRINT("[d3hack-obs]   rec%d sub=%p: %08X %08X %08X %08X %08X %08X %08X %08X",
                                      rec, static_cast<void *>(pSub), sw[0], sw[1], sw[2],
                                      sw[3], sw[4], sw[5], sw[6], sw[7])
                            }
                        }
                    }
                }
            }

            const int nWant = global_config.rare_cheats.camera_observer;
            if (nWant != 0 && CameraApplyObserverByName != nullptr) {
                const auto sno = GlobalSNOGet(static_cast<SNO>(nWant));
                auto       nm  = SNOToString(0x1A, static_cast<int>(sno), 0);
                if (nm.str() != nullptr && nm.str()[0] != '\0') {
                    CameraApplyObserverByName(0, nm.str(), -1);
                    PRINT("[d3hack-camera] applied observer 0x%X = \"%s\" (sno %d)", nWant,
                          nm.str(), static_cast<int>(sno))
                } else {
                    PRINT("[d3hack-camera] observer 0x%X resolved to sno %d but has no name",
                          nWant, static_cast<int>(sno))
                }
            }
        }

        if (g_nAncientRollsThisWorld > 0) {
            PRINT("[d3hack-custom] loot rolls last world: %d total, %d stock-untouched, "
                  "%d rank-raised, %d guaranteed primals",
                  g_nAncientRollsThisWorld, g_nAncientStockThisWorld,
                  g_nAncientRaisedThisWorld, g_nPrimalsThisWorld)
        }
        g_nAncientRollsThisWorld  = 0;
        g_nAncientStockThisWorld  = 0;
        g_nAncientRaisedThisWorld = 0;
        g_nPrimalsThisWorld       = 0;
    }

    // d3hack-custom: TRAMPOLINE, not REPLACE. The stock roll has to stay reachable --
    // see the note in AugmentSpecifier.
    HOOK_DEFINE_TRAMPOLINE(ForceAncient) {
        static void StockRoll(LootSpecifier *tSpecifier, const ACDID idACDLooter) {
            Orig(tSpecifier, idACDLooter);
        }

        static void Callback(LootSpecifier *tSpecifier, const ACDID idACDLooter) {
            ActorCommonData *ptACDLooter = ACDTryToGet(idACDLooter);
            AugmentSpecifier(tSpecifier, ptACDLooter, idACDLooter, &StockRoll);
            if (ActorCommonData *ptACDPlayer = ptACDLooter; ptACDPlayer) {
                FastAttribKey tKey;
                tKey.nValue = RECEIVED_SEASONAL_LEGENDARY;
                ACD_AttributesSetInt(ptACDPlayer, tKey, 0);
                // tKey.nValue = TARGETED_LEGENDARY_CHANCE;
                // ACD_AttributesSetInt(ptACDPlayer, tKey, 100000);
                // FastAttribKey flKey;
                // flKey.nValue = LEGENDARY_FIND_COMMUNITY_BUFF;
                // ACD_AttributesSetFloat(ptACDPlayer, flKey, 100.0f);
            }
        }
    };

    // d3hack-custom: the multiplier the per-attribute paragon limit is scaled by.
    // MUST stay in step with the shift chosen for patch_paragon_limit_scale in
    // patches.cpp -- the two scale the SAME stock 50 read from two different places,
    // and any disagreement shows up as a category that grants points the spend path
    // refuses, or accepts points it was never granted.
    inline auto ParagonLimitFactor() -> int {
        const int want = global_config.rare_cheats.paragon_stat_cap;
        if (want <= 50)
            return 1;
        for (u32 t = 1u; t <= 5u; ++t) {
            if (50 * static_cast<int>(1u + (1u << t)) == want)
                return static_cast<int>(1u + (1u << t));
        }
        return 5;  // patches.cpp falls back to k = 2, i.e. x5
    }

    // Snapshot taken at install, so the hot path never reaches into global_config.
    inline int s_nParagonLimitFactor = 1;

    // d3hack-custom: the per-CATEGORY paragon point POOL. This is a different limit
    // from the per-stat spend cap, and it is the one that actually bites.
    //
    // 0x527780 walks GB_PARAGON_BONUSES and sums each bonus's limit into its category's
    // capacity; 0x527630 then hands each category min(points_earned, capacity) and adds
    // the overflow to Core. Four bonuses per non-Core category at the stock limit of 50
    // makes that capacity 200 -- which is the ceiling seen in play, and it is why the
    // spare points pile up in Core instead. Worked example at paragon 824: the rotation
    // gives 206 to each of the four categories, the three non-Core ones clamp to 200, and
    // the 3 x 6 left over land in Core for 206 + 18 = 224.
    //
    // The catch is that 0x527780 does NOT call the limit getter at 0x5271F0. It reads the
    // field straight out of the GameBalance record:
    //
    //     0x52786C  ldr w26, [x8, x9]      x9 = 0x18 (cap off) or 0x1C (cap on)
    //
    // so patch_paragon_limit_scale, which scales that getter's RETURN, moves the spend
    // check to 250 and leaves the pool at 200. Exactly the reported symptom: 250 allowed
    // into any one stat, only 200 points ever granted to spend in that category.
    //
    // Patching the record data instead does not work -- 0x6CA760 hands back a refcounted
    // temporary, which is why RaiseParagonStatLimits() was removed. So scale the loaded
    // value, with the same factor as the getter patch.
    //
    // Installed at 0x527870 (`ldr x8, [sp, #0x40]`), the instruction after the load. The
    // inline callback runs BEFORE it and it does not touch w26, so w26 already holds the
    // limit and nothing downstream of the callback re-reads it.
    HOOK_DEFINE_INLINE(ParagonCategoryCapacity) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            const int nFactor = s_nParagonLimitFactor;
            if (nFactor <= 1)
                return;
            const u32 uLimit = ctx->W[26];
            // 0x527888 computes headroom as 0x7FFFFFFF - running_total and drops the
            // addend entirely if it does not fit, so a scaled limit that overflows would
            // silently zero a whole category rather than raise it. Leave those alone --
            // main stat and Vitality already read 100000 here.
            if (uLimit == 0u || uLimit > (0x7FFFFFFFu / static_cast<u32>(nFactor)))
                return;
            ctx->W[26] = uLimit * static_cast<u32>(nFactor);
        }
    };

    // d3hack-custom: the elite engage/kill events, for a D3-PC-style combat log.
    //
    // On PC the chat pane prints "<player> has engaged <elite>" / "has slain <elite>".
    // The Switch build fires the SOUND but shows no text. The events themselves are
    // clearly still in this binary -- the packed string table carries
    // EngagedEliteChampion, EngagedEliteRare, EngagedEliteTreasureGoblin and
    // KilledEliteChampion -- and there are matching ACD attributes:
    //
    //     ELITE_ENGAGED        0x508
    //     ENGAGED_RARE_TIME    0x509
    //     LAST_ACD_KILLED_TIME 0x472
    //
    // Attributes are far cheaper to hook than the named-event system, and cheaper than
    // the monster-death handler, which is still unfound (it is the same blocker as the
    // Visions of Enmity task). So before writing any UI, prove which of these actually
    // move during a pack fight and what they carry. Read-only; nothing is changed.
    //
    // Display is already solved: d3::imgui_overlay::PostOverlayNotification(color, ttl,
    // fmt, ...) posts a TTL-faded toast, which is exactly the PC chat-history behaviour.
    inline bool s_bEliteEventProbe = false;

    // d3hack-custom: buff expiry lives in an ATTRIBUTE, not the script binding.
    //
    // PowerBuffSetDuration (0x904C64) was hooked and logged ZERO calls across a shrine pickup,
    // so shrine buffs never touch the script path -- that whole route is dead. What they do
    // touch is BUFF_ICON_END_TICK0..31 (0x272..0x291): the tick the buff expires on.
    //
    // Scaling that means extending the remaining time rather than replacing it, so whatever
    // Gloves of Worship and Flavor of Time already contributed is preserved and multiplied --
    // which is the whole reason for scaling instead of the infinite-buffs attribute.
    //
    // The probe logs the key, param and value so the shrine and pylon buffs can be told apart
    // from every other buff in the game before anything is scaled. Nothing is modified until
    // that is known: guessing here would stretch every buff in the game, including monster
    // affixes and cooldowns.
    inline auto IsBuffEndTick(u32 uAttr) -> bool { return uAttr >= 0x272u && uAttr <= 0x291u; }

    HOOK_DEFINE_TRAMPOLINE(EliteEventProbe) {
        static void Callback(ActorCommonData *tACD, FastAttribKey tKey, s32 nValue) {
            // d3hack-custom: name the active legendary powers. ITEM_POWER_PASSIVE (0x50D) is
            // written per equipped legendary, with the POWER SNO as the key's param -- so this
            // both proves the attribute works and hands over the SNOs for Gloves of Worship and
            // Flavor of Time without guessing at item tables.
            if (global_config.rare_cheats.shrine_duration_probe) {
                const u32 uA2 = static_cast<u32>(KeyGetAttrib(tKey));
                if (uA2 == 0x50Du && nValue != 0) {
                    const s32   snoP = static_cast<s32>(KeyGetParam(tKey));
                    const char *szP  = SnoName(snoP);
                    static int  s_nIP = 0;
                    if (s_nIP < 60) {
                        ++s_nIP;
                        PRINT("[d3hack-item] ITEM_POWER_PASSIVE power=%d \"%s\" = %d", snoP,
                              szP != nullptr ? szP : "?", nValue)
                    }
                }
            }

            if (global_config.rare_cheats.shrine_duration_probe) {
                const u32 uAttr = static_cast<u32>(KeyGetAttrib(tKey));
                if (IsBuffEndTick(uAttr)) {
                    static int s_nT = 0;
                    if (s_nT < 40) {
                        ++s_nT;
                        PRINT("[d3hack-buff] END_TICK attr=0x%03X key=%08X value=%d", uAttr,
                              static_cast<u32>(tKey.nValue), nValue)
                    }
                }
            }
            if (s_bEliteEventProbe) {
                const int nAttr = static_cast<int>(KeyGetAttrib(tKey));

                // CENSUS: one line per distinct attribute id, ever.
                //
                // The first version of this probe used a single 40-line budget across
                // three hand-picked attributes. LAST_ACD_KILLED_TIME (0x472) is written
                // to every party ACD several times per kill, so it burned the whole
                // budget in two seconds and starved out the very attributes the probe
                // existed to catch. That produced a confident-looking "ELITE_ENGAGED
                // never fires" which was not a result at all.
                //
                // A per-id bitmap cannot be starved: the loudest attribute costs exactly
                // one line, same as the quietest. It also stops us guessing ids -- this
                // names everything that moves during a pack fight, which is what we need
                // for the engage/kill events AND for whatever drives the Convention of
                // Elements popup on its 4s cadence.
                if (nAttr >= 0 && nAttr < 0x600) {
                    static u8  s_uSeen[0x600 / 8] {};
                    static int s_nCensus = 0;
                    const int  nByte     = nAttr >> 3;
                    const u8   uBit      = static_cast<u8>(1u << (nAttr & 7));
                    if ((s_uSeen[nByte] & uBit) == 0u && s_nCensus < 0) {
                        s_uSeen[nByte] |= uBit;
                        ++s_nCensus;
                        PRINT("[d3hack-attr] attr 0x%X first write = %d gbid=%08X param=%ld",
                              nAttr, static_cast<int>(nValue),
                              tACD != nullptr ? static_cast<u32>(tACD->hGB.gbid) : 0u,
                              static_cast<long>(KeyGetParam(tKey)))
                    }
                }

                // FOCUSED: the two engage attributes get their own budget so repeats are
                // visible, and only on a CHANGE -- these are the candidates for the
                // "has engaged" line and we want to see them fire per pack, not per tick.
                // d3hack-custom: WHO WROTE IT. This is the route to the monster name.
                //
                // ENGAGED_RARE_TIME (0x509) and LAST_ACD_KILLED_TIME (0x472) are both
                // confirmed firing, and both land on the PLAYER's ACD, so the attribute
                // itself can never say which pack. But something decided to write them,
                // and that something had the monster in hand when it did.
                //
                // So capture the return address. It names the function that made the
                // decision -- the engage handler and the kill handler respectively -- and
                // those are where the monster lives. Same trick the sound probe was going
                // to use, except aimed at a signal already proven to fire rather than one
                // I hoped would.
                //
                // Deduped by (attr, caller): 0x509 is restamped every tick while engaged,
                // so without this the same call site would repeat hundreds of times. What
                // matters is the SET of distinct call sites, not the count.
            // d3hack-custom: the kill line is driven by polling what we announced --
            // see PollAnnouncedDeaths. Both attribute routes were measured dead.
            PollAnnouncedDeaths();

            if (nAttr == ENGAGED_RARE_TIME || nAttr == LAST_ACD_KILLED_TIME ||
                    nAttr == ENGAGED_GOBLIN_TIME) {
                    const auto lr   = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
                    const auto uOff = static_cast<u32>(lr - exl::util::modules::GetTargetStart());
                    struct Site { int nAttr; u32 uOff; };
                    static Site s_tSites[12] {};
                    static int  s_nSites = 0;
                    bool        bKnown   = false;
                    for (int i = 0; i < s_nSites; ++i) {
                        if (s_tSites[i].nAttr == nAttr && s_tSites[i].uOff == uOff) {
                            bKnown = true;
                            break;
                        }
                    }
                    if (!bKnown && s_nSites < 12) {
                        s_tSites[s_nSites].nAttr = nAttr;
                        s_tSites[s_nSites].uOff  = uOff;
                        ++s_nSites;
                        PRINT("[d3hack-caller] attr 0x%X written from +0x%X (gbid=%08X)",
                              nAttr, uOff,
                              tACD != nullptr ? static_cast<u32>(tACD->hGB.gbid) : 0u)
                    }
                }

                if (nAttr == ELITE_ENGAGED || nAttr == ENGAGED_RARE_TIME) {
                    static int s_nElite    = 0;
                    static s32 s_nLastVal  = -0x7FFFFFFF;
                    static int s_nLastAttr = -1;
                    if ((nValue != s_nLastVal || nAttr != s_nLastAttr) && s_nElite < 24) {
                        s_nLastVal  = nValue;
                        s_nLastAttr = nAttr;
                        ++s_nElite;
                        PRINT("[d3hack-elite] ENGAGE attr 0x%X = %d acd=%p gbid=%08X param=%ld",
                              nAttr, static_cast<int>(nValue), static_cast<void *>(tACD),
                              tACD != nullptr ? static_cast<u32>(tACD->hGB.gbid) : 0u,
                              static_cast<long>(KeyGetParam(tKey)))
                    }
                }
            }
            // d3hack-custom: FIND THE CAMERA BY WALKING -- self-driving version.
            //
            // The user's insight: the camera follows the player, so its position is
            // written every frame and the OFFSET between camera and player is the zoom.
            // Walking cannot reveal the zoom itself (that is the part which does NOT
            // change) -- it reveals WHERE THE CAMERA IS. That is the step every previous
            // attempt skipped: five message-route dead ends were all me guessing at
            // structure from disassembly instead of locating the thing empirically.
            //
            // The first version asked the player to react to a "MOVE NOW" line in a
            // console that prints ServiceNifm spam every millisecond. Unreadable, so the
            // probe never got its second sample. This one needs no timing and no reading:
            // it samples every ~3s and only reports an offset once it has changed on
            // THREE CONSECUTIVE samples. Normal play satisfies that; a value that ticks
            // once or jitters does not.
            // d3hack-custom: drive the Observer asset scan on its own tick. It needs no
            // player pointer, no movement and no camera lock, so it must not sit inside the
            // walking probe gates -- those are exactly what kept earlier probes from ever
            // running.
            // d3hack-custom: the write trap rides the same tick. It needs only the camera
            // lock, so it is deliberately independent of the scan.
            // The old camera sweep, its verify pass and the ACD/Player walk probes have all
            // done their job the moment the lock exists, but they kept re-sweeping and
            // re-verifying for the rest of the session. Together with the other probes that
            // was six of them running at once and ~270 log lines a minute through
            // OutputDebugString, which is expensive under emulation -- that is the lag.
            if (s_uCamEye != 0ull && s_bCamWalkArmed) {
                s_bCamWalkArmed = false;
                PRINT("[d3hack-own] camera locked at %p -- legacy sweep/walk probes stopped",
                      reinterpret_cast<void *>(s_uCamEye))
            }

            // Runs on every invocation, not on the 3 s tick -- it has to beat the frame.
            if (global_config.rare_cheats.active)
                ProjectionReapply();

            if (global_config.rare_cheats.active && global_config.rare_cheats.camera_trap)
                CameraOwnerProbe();

            if (global_config.rare_cheats.active &&
                global_config.rare_cheats.camera_observer_scan && !s_bObsScanDone) {
                const u64 uObsNow = svcGetSystemTick();
                // The game thread's only job here is to capture the anchor and hand the
                // sweep to the worker.
                if (s_uObsAnchor != 0ull)
                    StartScanWorker();
                if (s_uObsTick == 0ull ||
                    (uObsNow - s_uObsTick) > (d3::pools::kTicksPerSecond)) {
                    s_uObsTick = uObsNow;
                    // Take a live heap pointer to anchor the sweep on. Without this the
                    // walk starts at 0 and never reaches the arena the game actually uses.
                    if (s_uObsAnchor == 0ull && GetPrimaryPlayer != nullptr &&
                        ACDTryToGet != nullptr) {
                        if (auto *pAn = ACDTryToGet(GetPrimaryPlayer());
                            reinterpret_cast<uintptr_t>(pAn) >= 0x10000ull)
                            s_uObsAnchor = reinterpret_cast<uintptr_t>(pAn);
                    }
                    if (!g_bScanThreadUp)
                        ObserverAssetScan();     // fallback if the thread could not start
                }
            }

            if (s_bCamWalkArmed && GetPrimaryPlayerForGameConnection != nullptr) {
                const u64 uNow = svcGetSystemTick();
                if (s_uCamWalkTick == 0ull ||
                    (uNow - s_uCamWalkTick) > (3ull * d3::pools::kTicksPerSecond)) {
                    s_uCamWalkTick = uNow;
                    // CONTROL: scan the player's own ACD as well. Its position provably
                    // moves when you walk, so if the ACD shows nothing either, the probe
                    // logic is broken rather than the Player struct being the wrong place.
                    // "Nothing moved" must not be allowed to mean two different things --
                    // that ambiguity has cost several runs tonight.
                    void *pScan = nullptr;
                    if (GetPrimaryPlayer != nullptr && ACDTryToGet != nullptr)
                        pScan = ACDTryToGet(GetPrimaryPlayer());
                    if (reinterpret_cast<uintptr_t>(pScan) >= 0x10000ull) {
                        const auto *fa = reinterpret_cast<const float *>(pScan);
                        static float s_arAcd[216] {};
                        static int   s_nAcdPass = 0;
                        ++s_nAcdPass;
                        if (s_nAcdPass == 2 || s_nAcdPass == 5) {
                            int nA = 0;
                            for (int i = 0; i < 216 && nA < 12; ++i) {
                                const float a = s_arAcd[i], b = fa[i];
                                const float d = (b > a) ? (b - a) : (a - b);
                                if (d > 0.05f && d < 400.0f && a != 0.0f) {
                                    ++nA;
                                    PRINT("[d3hack-acdwalk] +0x%03X %d.%03d -> %d.%03d", i * 4,
                                          static_cast<int>(a), static_cast<int>((a<0?-a:a)*1000.0f)%1000,
                                          static_cast<int>(b), static_cast<int>((b<0?-b:b)*1000.0f)%1000)
                                }
                            }
                            PRINT("[d3hack-acdwalk] pass %d: %d moving floats in ACD", s_nAcdPass, nA)
                        }
                        for (int i = 0; i < 216; ++i)
                            s_arAcd[i] = fa[i];
                    }

                    // g_idGameConnection is cached once at world init and was sitting at
                    // FFFFFFFF, so GetPrimaryPlayerForGameConnection returned null and the
                    // camera scan NEVER RAN -- no hits and no negative, just silence. The
                    // ACD control kept reporting throughout because it does not depend on
                    // this pointer, which is the only reason the difference was visible.
                    //
                    // Fetch the connection live instead of trusting the cached global, and
                    // fall back to the cached one if the live call is also invalid.
                    GameConnectionID idConn = g_idGameConnection;
                    if (ServerGetOnlyGameConnection != nullptr) {
                        const auto idLive = ServerGetOnlyGameConnection();
                        if (static_cast<u32>(idLive) != 0xFFFFFFFFu)
                            idConn = idLive;
                    }
                    auto *pPlayer  = GetPrimaryPlayerForGameConnection(idConn);
                    if (reinterpret_cast<uintptr_t>(pPlayer) < 0x10000ull) {
                        // Report it rather than silently spinning: the previous run went
                        // quiet for eight minutes and a null here is the likeliest reason.
                        if ((s_nCamWalkPass & 7) == 0)
                            PRINT("[d3hack-walk] player ptr not ready (%p) conn=%X cached=%X",
                                  static_cast<void *>(pPlayer), static_cast<u32>(idConn),
                                  static_cast<u32>(g_idGameConnection))
                        ++s_nCamWalkPass;
                    } else {
                        // d3hack-custom: DELTA MATCHING.
                        //
                        // The ACD control proved the method and handed over the player's
                        // world position: ACD +0x060/+0x064/+0x068 = x/y/z. It moved
                        // 391.250 -> 423.220 in one sample.
                        //
                        // That gives a far sharper signature than "changed by a walking
                        // amount". The camera tracks the player, so it moves by the SAME
                        // delta. A float that changes by exactly the player's dx, in a
                        // world-coordinate range, is the camera's x -- and its neighbours
                        // are y and z. Ordinary moving values (timers, animation, health)
                        // will not coincidentally match a specific dx to two decimals.
                        //
                        // Window widened to 8 KB: 2 KB found nothing, and Player is a big
                        // struct -- tAVCameraTransform sits ~88 fields in, easily past 2 KB.
                        constexpr int kWords = 2048;  // 8 KB of the Player struct
                        const auto   *f      = reinterpret_cast<const float *>(pPlayer);
                        ++s_nCamWalkPass;

                        float flDx = 0.0f;
                        if (GetPrimaryPlayer != nullptr && ACDTryToGet != nullptr) {
                            if (auto *pA2 = ACDTryToGet(GetPrimaryPlayer());
                                reinterpret_cast<uintptr_t>(pA2) >= 0x10000ull) {
                                const auto *fa2 = reinterpret_cast<const float *>(pA2);
                                flDx = fa2[0x60 / 4] - s_flLastPx;
                                s_flLastPx = fa2[0x60 / 4];
                                s_flLastPy = fa2[0x64 / 4];
                                s_flLastPz = fa2[0x68 / 4];
                            }
                        }
                        const float flAbsDx = (flDx < 0.0f) ? -flDx : flDx;

                        // d3hack-custom: drive the heap scanner from the ACD position,
                        // which the control proved is live and correct.
                        // Run the sweep EVERY tick, moving or not. The flAbsDx gate was
                        // there for the old movement-based verify; the exact-signature lock
                        // and the offset-triple hunt need no movement at all, so gating on
                        // it just stalls the scan whenever the player stands still -- which
                        // is exactly what happened: one heartbeat, then silence.
                        if (!s_bSweepDone)
                            CameraHeapScan(s_flLastPx, s_flLastPy);
                        else
                            CameraHeapVerify(s_flLastPx, s_flLastPy);


                        if (s_nCamWalkPass > 1 && flAbsDx > 1.0f) {
                            int nShown = 0;
                            for (int i = 0; i < kWords && nShown < 24; ++i) {
                                // The camera EASES toward the player -- it lags, and slows
                                // on direction changes -- so its per-sample delta is NOT
                                // the player's delta. Matching dx exactly would miss it.
                                //
                                // Proximity is the lag-proof signature instead: a trailing
                                // camera is still near the player in world space. Require
                                // the value to sit within 300 units of the player's x AND
                                // to be moving. A smoothed follower passes; unrelated
                                // world-scale numbers that happen to drift do not, because
                                // they are not pinned to the player's coordinate.
                                // NaN DEFEATS NEGATIVE TESTS. Padding and uninitialised
                                // words read as float are NaN, and every comparison with
                                // NaN is false -- so `if (x < lo || x > hi) continue;`
                                // lets NaN straight through, and static_cast<int>(NaN)
                                // then prints as 0. That is exactly what the last run
                                // produced: 24 "hits" all reading 0.000 while the player
                                // was at x=413.971, every one of them garbage.
                                //
                                // Written as POSITIVE assertions instead, NaN fails them
                                // and is rejected, which is the behaviour I wanted.
                                // -ffast-math IS ON (cmake/toolchain.cmake:124), which
                                // implies -ffinite-math-only: the compiler is entitled to
                                // assume NaN and Inf never occur and to DELETE any check
                                // for them. That is why both the negative form
                                // (`if (x < lo || x > hi)`) and the "NaN-safe" positive
                                // form (`if (!(x > lo && x < hi))`) let 0xFFFFFFFF through
                                // -- they were optimised out of existence. Raw bits are
                                // what exposed it: v=FFFFFFFF, adx=7FFFFFFF, both NaN.
                                //
                                // No float comparison in this build can reject a NaN. Test
                                // the EXPONENT BITS instead: all ones means NaN or Inf, and
                                // an integer mask is not something fast-math may assume
                                // away. Any float sanity check anywhere in this fork has
                                // the same defect.
                                u32 ubits;
                                __builtin_memcpy(&ubits, &f[i], 4);
                                if ((ubits & 0x7F800000u) == 0x7F800000u)
                                    continue;  // NaN or Inf
                                u32 ubprev;
                                __builtin_memcpy(&ubprev, &s_arCamWalk[i], 4);
                                if ((ubprev & 0x7F800000u) == 0x7F800000u)
                                    continue;

                                const float d  = f[i] - s_arCamWalk[i];
                                const float ad = (d < 0.0f) ? -d : d;
                                if (!(ad > 0.05f && ad < 400.0f))
                                    continue;
                                const float v = f[i];
                                if (!(v > -100000.0f && v < 100000.0f))
                                    continue;
                                const float dx = v - s_flLastPx;
                                const float adx = (dx < 0.0f) ? -dx : dx;
                                if (!(adx < 300.0f))
                                    continue;
                                ++nShown;
                                // RAW BITS. The decimal formatting said v = 0.000 while
                                // the same iteration had just proved |v - playerX| < 300
                                // with playerX = 389.689 -- arithmetically impossible, so
                                // the FORMATTING is lying, not the filter. Print the float
                                // bit patterns and the integer millis; %08X cannot be
                                // misread the way a hand-rolled fixed-point print can.
                                u32 uv, upx, uadx;
                                __builtin_memcpy(&uv, &v, 4);
                                __builtin_memcpy(&upx, &s_flLastPx, 4);
                                __builtin_memcpy(&uadx, &adx, 4);
                                PRINT("[d3hack-cam] +0x%04X v=%08X (%dm) px=%08X (%dm) adx=%08X (%dm)",
                                      i * 4, uv, static_cast<int>(v * 1000.0f),
                                      upx, static_cast<int>(s_flLastPx * 1000.0f),
                                      uadx, static_cast<int>(adx * 1000.0f))
                            }
                            if (nShown > 0) {
                                s_bCamWalkArmed = false;
                                PRINT("[d3hack-cam] %d fields track the player (pass %d)",
                                      nShown, s_nCamWalkPass)
                            } else if (s_nCamWalkPass >= 10) {
                                s_bCamWalkArmed = false;
                                PRINT("[d3hack-cam] no field in 8KB of Player tracks the "
                                      "player -- camera is elsewhere (renderer global?)%s", "")
                            }
                        }
                        for (int i = 0; i < kWords; ++i)
                            s_arCamWalk[i] = f[i];
                    }
                }
            }

            // d3hack-custom: THE ZOOM. eye = target + factor * (eye - target).
            //
            // The eye vec4 is rebuilt by the game every frame, so this must be pushed
            // continuously rather than set once -- this hook fires hundreds of times a
            // second, which is why it is the right place. Re-guarded on every write: if
            // the struct stops looking like a camera (w drifts off 1.0, offset absurd)
            // the lock is dropped rather than scribbling into reused memory.
            // Gated separately now: CameraZoom drives the ASSET patch, and running both
            // at once would scale the offset twice and re-create the runaway.
            if (s_uCamEye != 0ull && global_config.rare_cheats.camera_eye_write &&
                global_config.rare_cheats.camera_zoom != 0.0f) {
                auto       *pe = reinterpret_cast<float *>(s_uCamEye);
                const auto *pt = reinterpret_cast<const float *>(s_uCamEye - 0x20);
                if (!FloatIsSane(&pe[0]) || !FloatIsSane(&pt[0]) ||
                    !(pe[3] > 0.9f && pe[3] < 1.1f)) {
                    s_uCamEye = 0ull;
                } else {
                    // Detect object reuse: the look-at must still be sitting on the player.
                    // If this allocation has been recycled for something else we would be
                    // scribbling 5x offsets into freed memory, which is the other candidate
                    // for the crash.
                    // Refresh the player position from the ACD rather than trusting the
                    // 3-second cache. The write runs hundreds of times a second, and the
                    // cached value is stale almost always -- the log shows the player at
                    // (293,261) and then (2870,2384) moments apart. Comparing a live target
                    // against a stale position made a perfectly healthy camera look like a
                    // recycled allocation, and the lock dropped after ONE write.
                    static u64   s_uPosTick = 0ull;
                    static float s_flNowPx  = 0.0f;
                    static float s_flNowPy  = 0.0f;
                    const u64    uPosNow    = svcGetSystemTick();
                    if (uPosNow - s_uPosTick > (d3::pools::kTicksPerSecond / 10ull)) {
                        s_uPosTick = uPosNow;
                        if (GetPrimaryPlayer != nullptr && ACDTryToGet != nullptr) {
                            if (auto *pAn = ACDTryToGet(GetPrimaryPlayer());
                                reinterpret_cast<uintptr_t>(pAn) >= 0x10000ull) {
                                const auto *fn = reinterpret_cast<const float *>(pAn);
                                if (FloatIsSane(&fn[0x60 / 4]) && FloatIsSane(&fn[0x64 / 4])) {
                                    s_flNowPx = fn[0x60 / 4];
                                    s_flNowPy = fn[0x64 / 4];
                                }
                            }
                        }
                    }
                    const float tdx = pt[0] - s_flNowPx;
                    const float tdy = pt[1] - s_flNowPy;
                    // SKIP, do not DROP. The lock died after exactly one write every time,
                    // which is what you would expect if the camera is double-buffered: we
                    // lock one struct, the game swaps to the other, our copy's target stops
                    // following the player, and a drop-on-mismatch rule throws away a
                    // perfectly good pointer that would be live again next frame.
                    //
                    // Only genuinely corrupt memory (checked above via w and NaN) justifies
                    // dropping. A target that has wandered just means "not this frame".
                    if (!((tdx * tdx + tdy * tdy) < (120.0f * 120.0f))) {
                        static int s_nSkip = 0;
                        if ((++s_nSkip % 600) == 0)
                            PRINT("[d3hack-cam!] skipping writes -- target off-player (%d,%d) vs (%d,%d)",
                                  static_cast<int>(pt[0]), static_cast<int>(pt[1]),
                                  static_cast<int>(s_flNowPx), static_cast<int>(s_flNowPy))
                    } else {
                    const float ox = s_flOrigOx;   // ALWAYS the lock-time offset
                    const float oy = s_flOrigOy;
                    const float oz = s_flOrigOz;
                    const float m  = __builtin_sqrtf(ox * ox + oy * oy + oz * oz);
                    if (m > 5.0f && m < 4000.0f) {
                        // CameraZoom is a MULTIPLIER here: 1.5 = 50% further out.
                        const float k = global_config.rare_cheats.camera_zoom;
                        pe[0] = pt[0] + ox * k;
                        pe[1] = pt[1] + oy * k;
                        pe[2] = pt[2] + oz * k;

                        // Does the write SURVIVE? The lock is confirmed and the write runs,
                        // yet nothing changed on screen -- so either the game recomputes
                        // the eye before rendering, or this is a copy nobody reads. Read
                        // the offset back a moment later: if it has returned to ~84 the
                        // game is overwriting us and the fix is WHERE we write, not what.
                        static u64 s_uLastCheck = 0ull;
                        const u64  uNowC        = svcGetSystemTick();
                        if (uNowC - s_uLastCheck > d3::pools::kTicksPerSecond) {
                            s_uLastCheck = uNowC;
                            const float rx = pe[0] - pt[0];
                            const float ry = pe[1] - pt[1];
                            const float rz = pe[2] - pt[2];
                            PRINT("[d3hack-cam!] wrote k=%dm, offset now %dm (was %dm)",
                                  static_cast<int>(k * 1000.0f),
                                  static_cast<int>(__builtin_sqrtf(rx * rx + ry * ry + rz * rz) * 1000.0f),
                                  static_cast<int>(m * 1000.0f))
                        }
                    }
                    }
                }
            }

            // d3hack-custom: DEFERRED camera apply.
            //
            // Every camera entry point enqueues an SMSG_* through 0x71DA90 -- four for
            // four. Those are SERVER->CLIENT messages, and sInitializeWorld has been seen
            // running with "Connection: ffffffff", i.e. no valid connection. Firing the
            // camera command there may simply have been queuing into nothing, which would
            // explain all three "changed nothing" results without the mechanism being
            // wrong.
            //
            // So retry once, well after the world is live, from a hook that only runs
            // during actual gameplay. This setter is extremely hot, so the work is a tick
            // compare behind a bool that is false forever after the single attempt.
            if (s_bCameraPending && CameraSetZoomValue != nullptr) {
                const u64 uNow = svcGetSystemTick();
                if (s_uCameraArmTick != 0ull &&
                    (uNow - s_uCameraArmTick) > (8ull * d3::pools::kTicksPerSecond)) {
                    s_bCameraPending = false;
                    CameraSetZoomValue(0, global_config.rare_cheats.camera_zoom, 0.0f);
                    PRINT("[d3hack-camera] deferred zoom %d/1000 fired mid-gameplay",
                          static_cast<int>(global_config.rare_cheats.camera_zoom * 1000.0f))
                }
            }

            Orig(tACD, tKey, nValue);
        }
    };

    // d3hack-custom: the same census over the FLOAT setter.
    //
    // The int census logged 211 distinct attributes during a pack fight and
    // ELITE_ENGAGED (0x508) was not among them, while its neighbour ENGAGED_RARE_TIME
    // (0x509) was and behaved exactly as its name says -- a timestamp restamped every
    // tick while a rare is engaged. So the int table is correctly numbered here and
    // 0x508 genuinely does not travel through ACD_AttributesSetInt. The float path was
    // never covered, and a flag-ish attribute is just as likely to be written as 0.0/1.0.
    //
    // Why 0x508 is worth the second look rather than settling for the timer: the timer
    // lives on the PLAYER's ACD (gbid BE27DC19 in every sample), so it can tell us that
    // an engagement happened but never WHICH elite. If ELITE_ENGAGED is instead written
    // on the monster, then the tACD argument here IS the monster and its gbid names the
    // pack -- which is the whole difference between "has engaged an elite" and
    // "has engaged Thundering Damned Herald".
    HOOK_DEFINE_TRAMPOLINE(EliteEventProbeFloat) {
        static void Callback(ActorCommonData *tACD, FastAttribKey tKey, float flValue) {
            // d3hack-custom: is a buff timer written as a FLOAT? Last cheap check -- see the
            // note above the AttrGetInt hook for the three routes already ruled out.
            if (global_config.rare_cheats.shrine_duration_probe) {
                const u32 uA = static_cast<u32>(KeyGetAttrib(tKey));
                if (uA >= 0x252u && uA <= 0x291u) {
                    static int s_nF = 0;
                    if (s_nF < 30) {
                        ++s_nF;
                        PRINT("[d3hack-buff] FLOAT tick attr=0x%03X key=%08X value=%d.%03d", uA,
                              static_cast<u32>(tKey.nValue), static_cast<int>(flValue),
                              static_cast<int>((flValue < 0 ? -flValue : flValue) * 1000.0f) % 1000)
                    }
                }
            }
            if (s_bEliteEventProbe) {
                const int nAttr = static_cast<int>(KeyGetAttrib(tKey));
                if (nAttr >= 0 && nAttr < 0x600) {
                    static u8  s_uSeenF[0x600 / 8] {};
                    static int s_nCensusF = 0;
                    const int  nByte      = nAttr >> 3;
                    const u8   uBit       = static_cast<u8>(1u << (nAttr & 7));
                    if ((s_uSeenF[nByte] & uBit) == 0u && s_nCensusF < 0) {
                        s_uSeenF[nByte] |= uBit;
                        ++s_nCensusF;
                        PRINT("[d3hack-attrf] attr 0x%X first write = %d/1000 gbid=%08X param=%ld",
                              nAttr, static_cast<int>(flValue * 1000.0f),
                              tACD != nullptr ? static_cast<u32>(tACD->hGB.gbid) : 0u,
                              static_cast<long>(KeyGetParam(tKey)))
                    }
                }
                // Every write of the engage attributes, whoever owns them. The gbid is
                // the payload that matters: player gbid means the timer story, anything
                // else means we have the monster.
                if (nAttr == ELITE_ENGAGED || nAttr == ENGAGED_RARE_TIME ||
                    nAttr == ENGAGED_GOBLIN_TIME) {
                    static int s_nF = 0;
                    if (s_nF < 24) {
                        ++s_nF;
                        PRINT("[d3hack-elite] FLOAT attr 0x%X = %d/1000 acd=%p gbid=%08X",
                              nAttr, static_cast<int>(flValue * 1000.0f),
                              static_cast<void *>(tACD),
                              tACD != nullptr ? static_cast<u32>(tACD->hGB.gbid) : 0u)
                    }
                }
            }
            Orig(tACD, tKey, flValue);
        }
    };

    // d3hack-custom: the engage and kill handlers, found by return address.
    //
    //   ENGAGE  0x853500  writes ENGAGED_RARE_TIME    from +0x853724
    //   KILL    0x842DF0  writes LAST_ACD_KILLED_TIME from +0x843184
    //
    // In both, the ACD receiving the attribute is the PLAYER (gbid BE27DC19), fetched
    // from an actor table with the 0x360 stride -- so the write target is never the
    // monster. But the monster has to be in scope for the handler to have fired, and in
    // both cases it comes in as an argument:
    //
    //   0x853500: mov x21,x1 / mov x19,x0   -- two actor pointers
    //   0x842DF0: mov w26,w0 / mov w22,w1   -- ints, immediately resolved to objects by
    //             0x86DF70 and 0x870850, i.e. ACDIDs. That argument shape (victim, killer,
    //             plus damage-ish extras) is what a death handler looks like.
    //
    // INLINE hooks, deliberately: the signatures are inferred, not known, and an inline
    // callback reads x0/x1 straight out of the context without committing to an ABI. A
    // TRAMPOLINE here would need a declared prototype and a wrong one corrupts the call.
    // Read-only -- nothing is written back.
    inline bool s_bHandlerProbe = false;

    HOOK_DEFINE_INLINE(EngageHandlerProbe) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            // NOTE: this callback used to open with `static int s_n; if (s_n >= 10) return;`
            // -- a probe budget from when this was a diagnostic. Once the combat log moved
            // in behind it, that budget silently switched the FEATURE off after ten engage
            // calls, which is one pack. Hence "it only fired for the first pack". The
            // diagnostic prints below keep their own budgets; the log must not have one.
            auto *pA = reinterpret_cast<ActorCommonData *>(ctx->X[0]);
            if (pA == nullptr)
                return;

            // x1 is the player (gbid BE27DC19) and x0 is the other actor, whose gbid
            // reads FFFFFFFF -- which is the giveaway that gbid is the WRONG field here.
            // GBID identifies GameBalance records (items); a monster is identified by its
            // actor SNO. ActorCommonData carries snoActor, eMonsterRarity and tSNOName,
            // any of which would name the pack.
            //
            // But this header's layout has already been wrong once for 2.7.6 (the
            // LootSpecifier fields disagree with what the game actually indexes), so
            // trusting the struct offsets here is how the next run gets wasted. Dump the
            // raw head of the ACD alongside the typed reads and cross-check: id should
            // match the ACDID space, snoActor should be a plausible SNO, and rarity should
            // be a small enum. Whichever words line up is the truth.
            // d3hack-custom: NAME THE PACK.
            //
            // The raw dump validated the header layout against the live struct, which was
            // the thing worth checking before trusting any offset here:
            //
            //     w[0] = id        7CCB0481   matches the typed read
            //     w[6] = snoActor  000018D7
            //     w[7] = tSNOName.snoGroup    = 1   (Actor)
            //     w[8] = tSNOName.snoHandle   = 000018D7  == snoActor
            //
            // snoActor and tSNOName.snoHandle agreeing is the cross-check: two independent
            // fields carrying the same value at the offsets the header predicts. So the
            // struct is right for 2.7.6 here and pA->snoActor can be read directly.
            //
            // gbid reading FFFFFFFF was never a failure -- GBID identifies GameBalance
            // records, i.e. items. A monster is identified by its actor SNO, which is why
            // four runs of chasing gbid found nothing.
            //
            // SNOToString(1, sno, 0) is already used twice in this file for exactly this
            // (group 1 = Actor). It allocates, so it stays behind the probe budget.
            // d3hack-custom: locate eMonsterRarity empirically.
            //
            // Filtering on pA->eMonsterRarity produced NOTHING for a real elite pack, so
            // the header's offset for it is wrong -- unsurprising, this header is already
            // documented as disagreeing with 2.7.6 elsewhere. snoActor was verifiable
            // because tSNOName.snoHandle duplicates it; rarity has no such twin, so it has
            // to be found by comparison instead.
            //
            // Dedupe by snoActor: each monster TYPE logs once. That is what stops trash
            // starving the budget -- the previous run burned all eight lines on goatmen
            // and moths before the pack arrived. A handful of types per fight, elites
            // included, all fit.
            //
            // Print the words after the region already identified (hGB at w14/w15) so the
            // elite's row can be diffed against a normal monster's. Whichever column is 0
            // for Goatman_Melee_A and non-zero for the pack is eMonsterRarity.
            // d3hack-custom: THE COMBAT LOG LINE.
            //
            // Dedupe by ACD id, not by snoActor: the handler re-fires for as long as you
            // are engaged, and two packs of the same monster type are still two packs.
            // The id is unique per actor instance, so each elite announces exactly once.
            if (global_config.rare_cheats.combat_log && pA->eMonsterRarity != 0) {
                // Deduping by ACD id was wrong: a champion pack is four separate
                // monsters, each with its own ACD, so one pull produced four lines.
                // Deduping by snoActor alone is the opposite error -- every later pack of
                // the same monster type in the rift would go unannounced.
                //
                // So: same snoActor within a short window counts as the same pack. A pack's
                // members all engage within a second or so of each other, while the next
                // pack of that type is minutes away. svcGetSystemTick is already used in
                // this file for exactly this kind of brake.
                // d3hack-custom: announce the PACK LEADER, not the minion you happened to
                // touch first.
                //
                // A yellow pack is one rare "master" plus its minions. Engaging a minion
                // should still read as the pack's name -- naming the minion is both wrong
                // and useless for deciding whether to fight it.
                //
                // ActorCommonData::idOwner (w[5] in the dump, right after ann) is the link.
                // For a minion it points at the master; for a standalone elite it points at
                // something that is not an elite, in which case we keep the actor itself.
                // Guard on the owner actually being an elite so pets, summons and player
                // followers can never be mistaken for a pack leader.
                // d3hack-custom: friendlies are not packs.
                //
                // "Hireling Enchantress" appeared in the log, which means a FOLLOWER was
                // announced as an elite. Followers legitimately carry a non-zero rarity, so
                // the `eMonsterRarity != 0` test alone was never enough to separate "elite
                // monster" from "elite-flagged ally". eHirelingClass is the field that
                // actually distinguishes them, and it costs one comparison.
                if (pA->eHirelingClass != 0)
                    return;

                ActorCommonData *pLeader = pA;
                if (ACDTryToGet != nullptr) {
                    if (ActorCommonData *pOwner =
                            ACDTryToGet(static_cast<ACDID>(pA->idOwner));
                        pOwner != nullptr && pOwner != pA && pOwner->eMonsterRarity != 0 &&
                        pOwner->eHirelingClass == 0) {
                        pLeader = pOwner;
                    }
                }

                static constexpr u64 kPackWindowTicks = 15ull * d3::pools::kTicksPerSecond;
                struct Said { int nSno; u64 uTick; };
                static Said s_arSaid[16] {};
                static int  s_nSaid = 0;
                const int   nSnoNow = static_cast<int>(pLeader->snoActor);
                const u64   uNow    = svcGetSystemTick();
                bool        bSaid   = false;
                for (auto &t : s_arSaid) {
                    if (t.nSno == nSnoNow && t.uTick != 0ull &&
                        (uNow - t.uTick) < kPackWindowTicks) {
                        t.uTick = uNow;  // keep the window alive while the pack is alive
                        bSaid   = true;
                        break;
                    }
                }
                if (!bSaid) {
                    s_arSaid[s_nSaid].nSno  = nSnoNow;
                    s_arSaid[s_nSaid].uTick = uNow;
                    s_nSaid                 = (s_nSaid + 1) % 16;

                    const char *sz = nullptr;
                    if (SNOToString != nullptr) {
                        auto tName = SNOToString(1, static_cast<int>(pLeader->snoActor), 0);
                        sz         = tName.str();
                    }

                    // Only rarity 1 has actually been observed (a champion pack). Rather
                    // than invent a table for values nobody has seen, anything else prints
                    // its number -- so an unknown rarity shows up as "Elite(3)" in play and
                    // names itself instead of being silently mislabelled.
                    // Rarity values, read off actual play rather than assumed:
                    //   1  Champion  (blue pack, all members identical)
                    //   2  Rare      (yellow master)
                    //   3  Minion    (the master's escorts)
                    //
                    // 3 = minion is inferred, but from two independent pairings a second
                    // apart -- SkeletonSummoner_C(2) with Skeleton_C(3), and
                    // GoatMutant_Ranged_B(2) with SoulRipper_B(3). Anything else still
                    // prints its number so an unseen value names itself instead of being
                    // mislabelled.
                    const int nRarity = static_cast<int>(pLeader->eMonsterRarity);

                    // Minions are skipped outright. Naming one is wrong (you care about the
                    // master) and the master announces itself anyway -- it is engaged in the
                    // same moment, as both pairings above show. This is the answer to
                    // "engaged a minion, want the master's name" that does NOT depend on
                    // resolving the link: idOwner did not connect them (ownerUsed=0 on every
                    // rarity-3 line this run), so suppressing the minion is the fix that
                    // actually works today.
                    if (nRarity == 3)
                        return;

                    // d3hack-custom: make the asset name readable.
                    //
                    // ActorGetNameFromSNO returns the ASSET name -- champion_unburied_a5,
                    // QuillDemon_FastAttack_A -- because that is what the .mon file carries
                    // (its Name field, at offset 1244). The localised display name lives in
                    // a .stl string table, and the rare title ("Thundering Damned Herald")
                    // is composed client-side from two GBIDs out of RareMonsterNamesTable.
                    // Neither is reachable without more hooks, and the PC archives in this
                    // repo are a server-side subset with no string tables at all, so there is
                    // nothing to extract offline either.
                    //
                    // But the part that matters -- WHICH PACK -- is already in the asset
                    // name; it is just ugly. So clean it up rather than resolve it:
                    //     champion_unburied_a5     -> Unburied
                    //     QuillDemon_FastAttack_A  -> Quill Demon Fast Attack
                    //     SkeletonSummoner_C       -> Skeleton Summoner
                    char szPretty[64] {};
                    PrettyMonsterName(szPretty, sizeof(szPretty), sz);

                    // The rift guardian comes through as rarity 7. "Elite(7) Lr Boss Dark
                    // Angel" is neither its name nor a useful label -- it is THE boss, so it
                    // gets its own colour and no rarity prefix at all.
                    char szAffix[128] {};
                    BuildAffixList(szAffix, sizeof(szAffix), pLeader);

                    if (nRarity == 7) {
                        const char *szGName =
                            (szPretty[0] != 0) ? szPretty : "Rift Guardian";
                        RememberAnnounced(pLeader->id, nRarity, szGName, szAffix);
                        d3::imgui_overlay::PostCombatLog(
                            0.85f, 0.85f, 0.85f, "Engaged " "\x01" "%s%s%s",
                            RarityColorHex(nRarity, false), szGName, szAffix);
                        return;
                    }

                    char szWhat[48] {};
                    if (nRarity == 1) {
                        nn::util::SNPrintf(szWhat, sizeof(szWhat), "Champion");
                    } else if (nRarity == 2) {
                        nn::util::SNPrintf(szWhat, sizeof(szWhat), "Rare");
                    } else {
                        nn::util::SNPrintf(szWhat, sizeof(szWhat), "Elite(%d)", nRarity);
                    }

                    // The rarity colour now rides on the NAME via markup (RarityColorHex),
                    // not on the whole entry, so the per-line floats are gone.
                    // d3hack-custom: WHERE DO THE AFFIX GBIDs LIVE?
                    //
                    // The affix names are solved: GameBalance/MonsterAffixes.gam gives 51
                    // GBID -> name pairs, and the GBID is a hash OF THE NAME, so that table
                    // is fixed content. ArcaneEnchanted / Desecrator / Vortex / Shielding
                    // are all in it, matching the health bar exactly once camelCase is split.
                    //
                    // What is NOT known is where the per-monster GBIDs sit at runtime. But
                    // since the exact values are now known, this is an EXACT search rather
                    // than a shape hunt -- the kind that has actually worked today. Scan the
                    // elite's ACD for any of them and report the offsets; wherever several
                    // land in a row is the affix array.
                    if (global_config.rare_cheats.elite_event_probe) {
                        static int s_nAffixScan = 0;
                        if (s_nAffixScan < 6) {
                            ++s_nAffixScan;
                            const auto *pW = reinterpret_cast<const u32 *>(pLeader);
                            int         nHits = 0;
                            for (int q = 0; q < 0x600 && nHits < 16; ++q) {
                                const u32 uV = pW[q];
                                if (uV == 0u || uV == 0xFFFFFFFFu)
                                    continue;
                                for (const auto &t : kAffixNames) {
                                    if (t.uGbid != uV)
                                        continue;
                                    ++nHits;
                                    PRINT("[d3hack-affix] +0x%03X = %08X  %s", q * 4, uV,
                                          t.szName)
                                    break;
                                }
                            }
                            PRINT("[d3hack-affix] %s: %d affix gbids found in 6KB of ACD",
                                  (szPretty[0] != 0) ? szPretty : "?", nHits)
                        }
                    }

                    const char *szShow = (szPretty[0] != 0) ? szPretty : "?";
                    RememberAnnounced(pLeader->id, nRarity, szShow, szAffix);
                    d3::imgui_overlay::PostCombatLog(
                        0.85f, 0.85f, 0.85f, "Engaged %s " "\x01" "%s%s%s", szWhat,
                        RarityColorHex(nRarity, false), szShow, szAffix);

                    // One log line per announced pack, so the rarity numbers and the
                    // minion->master resolution can both be checked against what was
                    // actually on screen.
                    PRINT("[d3hack-log] rarity=%d leader=%s ownerUsed=%d sno=%d",
                          nRarity, (sz != nullptr) ? sz : "?",
                          (pLeader != pA) ? 1 : 0, nSnoNow)

                }
            }

            static int s_arSeenSno[24] {};
            static int s_nSeenSno = 0;
            const int  nSno       = static_cast<int>(pA->snoActor);
            bool       bSeen      = false;
            for (int i = 0; i < s_nSeenSno; ++i) {
                if (s_arSeenSno[i] == nSno) {
                    bSeen = true;
                    break;
                }
            }
            if (!bSeen && s_nSeenSno < 24) {
                s_arSeenSno[s_nSeenSno++] = nSno;
                const char *sz = nullptr;
                if (SNOToString != nullptr) {
                    auto tName = SNOToString(1, nSno, 0);
                    sz         = tName.str();
                }
                const auto *w = reinterpret_cast<const u32 *>(pA);
                PRINT("[d3hack-name] sno=%d name=\"%s\" typedRarity=%d", nSno,
                      (sz != nullptr) ? sz : "?", static_cast<int>(pA->eMonsterRarity))
                PRINT("[d3hack-name]   w14..w21: %08X %08X %08X %08X %08X %08X %08X %08X",
                      w[14], w[15], w[16], w[17], w[18], w[19], w[20], w[21])
            }
        }
    };

    HOOK_DEFINE_INLINE(KillHandlerProbe) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (!s_bHandlerProbe)
                return;
            static int s_n = 0;
            if (s_n >= 10)
                return;
            ++s_n;
            // w0/w1 are ACDIDs here, not pointers. Resolve them the way the fork already
            // does everywhere else and print both gbids -- whichever is not the player is
            // the thing we want to name in the combat log.
            const auto idA = static_cast<ACDID>(ctx->W[0]);
            const auto idB = static_cast<ACDID>(ctx->W[1]);
            ActorCommonData *pA = ACDTryToGet(idA);
            ActorCommonData *pB = ACDTryToGet(idB);
            PRINT("[d3hack-handler] KILL w0=%d(gbid=%08X) w1=%d(gbid=%08X) w2=%d",
                  static_cast<int>(ctx->W[0]),
                  pA != nullptr ? static_cast<u32>(pA->hGB.gbid) : 0u,
                  static_cast<int>(ctx->W[1]),
                  pB != nullptr ? static_cast<u32>(pB->hGB.gbid) : 0u,
                  static_cast<int>(ctx->W[2]))
        }
    };

    // d3hack-custom: the main view camera.
    //
    // D3 cameras are "Observer" assets, reached through the GlobalSNO table:
    //     0x407 EDITMODE_DEFAULT   0x408 DEFAULT          0x409 CONSOLE_DEFAULT
    //     0x40A RETRO              0x40B CONSOLE_RETRO    0x40C ZOOMED
    //     0x40D WIDE               0x40E CONSOLE_ZOOMED   0x40F CONSOLE_WIDE
    //     0x410 CONSOLE_WIDE_RETRO
    // and the UI/store/inventory cameras run from 0x411..0x42x.
    //
    // Which of these the Switch build uses for gameplay has NOT been established --
    // that is what the probe is for. It logs every observer id the game resolves, once
    // each, so a single launch names the live camera instead of us guessing. Expect the
    // inventory and store observers to appear too; the gameplay one is whichever shows
    // up on entering a world.
    //
    // The override then swaps that resolution, which is the cheapest possible zoom
    // experiment: no Observer asset layout has to be understood, we just hand the game a
    // different camera it already ships. It is a fixed step rather than an arbitrary
    // percentage -- scaling the asset's own distance/FOV field is the follow-up, and it
    // needs the layout, which needs the probe result first.
    inline bool s_bCameraObserverProbe    = false;
    inline int  s_nCameraObserverOverride = 0;

    HOOK_DEFINE_TRAMPOLINE(CameraObserver) {
        static auto Callback(const SNO eGlobalSNO) -> SNO {
            const int nId = static_cast<int>(eGlobalSNO);

            // d3hack-custom: CENSUS of every GlobalSNO resolved AFTER the first world
            // is live, one line per distinct id.
            //
            // The previous control logged the first 20 calls with a shared budget. All 20
            // were spent at boot on id=0 before the build banner even printed, so it never
            // observed a single gameplay call -- the same starvation bug that void'd the
            // first elite probe. It did prove the hook itself fires, which is the one
            // thing that was genuinely unknown.
            //
            // The 0x154..0x157 (engage/kill sounds) and 0x407..0x422 (observers) filters
            // below were separately budgeted and so were NOT starved -- but neither can
            // tell the difference between "this id never comes" and "GlobalSNOGet is idle
            // during play". A per-id bitmap gated on world entry answers both at once:
            // any ids at all means the function is live and those ranges are truly unused;
            // silence means it goes quiet after boot and this whole route is finished.
            if (g_bWorldEntered && nId >= 0 && nId < 0x700) {
                static u8  s_uSeenSno[0x700 / 8] {};
                static int s_nSnoCensus = 0;
                const int  nByte        = nId >> 3;
                const u8   uBit         = static_cast<u8>(1u << (nId & 7));
                if ((s_uSeenSno[nByte] & uBit) == 0u && s_nSnoCensus < 0) {
                    s_uSeenSno[nByte] |= uBit;
                    ++s_nSnoCensus;
                    PRINT("[d3hack-snoget] in-world id=0x%X", nId)
                }
            }

            // d3hack-custom: THE ENGAGE/KILL EVENT, found via the sound.
            //
            // The sound proves the event exists -- three pack fights showed ELITE_ENGAGED
            // is never written on any actor, but the audio still fires, so the trigger was
            // never an attribute. It is a GlobalSNO sound lookup:
            //
            //     0x154 SOUND_MONSTER_ENGAGED     0x155 SOUND_GOBLIN_ENGAGED
            //     0x156 SOUND_COW_GOBLIN_ENGAGED  0x157 SOUND_ELITE_KILLED
            //
            // and those resolve through this very function, which was already hooked for
            // the camera probe. Every one of these resolutions IS one combat-log line.
            //
            // The return address is the payload that matters. It names the function that
            // decided to play the sound -- i.e. the engage/kill handler -- and that
            // function is where the monster is in scope. Disassemble the caller from the
            // offset logged here and the identity problem is solved at its source, rather
            // than by scanning nearby actors and guessing which one was engaged.
            if (nId >= 0x154 && nId <= 0x157) {
                static int s_nSound = 0;
                if (s_nSound < 24) {
                    ++s_nSound;
                    const auto lr = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
                    PRINT("[d3hack-sound] EVENT GlobalSNO 0x%X caller=+0x%lX", nId,
                          static_cast<unsigned long>(lr - exl::util::modules::GetTargetStart()))
                }
            }
            if (s_bCameraObserverProbe && nId >= 0x407 && nId <= 0x422) {
                static u32 s_uSeen = 0u;
                const u32  uBit    = 1u << (nId - 0x407);
                if ((s_uSeen & uBit) == 0u) {
                    s_uSeen |= uBit;
                    PRINT("[d3hack-camera] observer GlobalSNO 0x%X -> sno %d", nId,
                          static_cast<int>(Orig(eGlobalSNO)))
                }
            }
            if (s_nCameraObserverOverride != 0 && (nId == 0x408 || nId == 0x409))
                return Orig(static_cast<SNO>(s_nCameraObserverOverride));
            return Orig(eGlobalSNO);
        }
    };

    HOOK_DEFINE_REPLACE(Return_True) {
        static auto Callback() -> BOOL { return 1; }
    };

    HOOK_DEFINE_INLINE(FloatingDmgHook) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            // BB5E8
            auto *DmgColor = reinterpret_cast<RGBAColor *>(ctx->X[3]);
            *DmgColor      = crgbaRed;
        }
    };

    HOOK_DEFINE_INLINE(VarResHook) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            auto *ptVarRWindow = reinterpret_cast<VariableResRWindowData *>(ctx->X[19]);
            if (ptVarRWindow != nullptr && global_config.resolution_hack.active) {
                const float min_pct        = std::clamp(global_config.resolution_hack.min_res_scale, 10.0f, 100.0f);
                const float max_pct        = std::clamp(global_config.resolution_hack.max_res_scale, min_pct, 100.0f);
                ptVarRWindow->flMinPercent = min_pct * 0.01f;
                ptVarRWindow->flMaxPercent = max_pct * 0.01f;
            }
        };
    };

    HOOK_DEFINE_TRAMPOLINE(SpoofDocked) {
        static auto Callback() -> nn::oe::OperationMode {
            if (!global_config.resolution_hack.spoof_docked)
                return Orig();
            return nn::oe::OperationMode_Docked;
        }
    };

    HOOK_DEFINE_TRAMPOLINE(SpoofPerfMode) {
        static auto Callback() -> nn::oe::PerformanceMode {
            if (!global_config.resolution_hack.spoof_docked)
                return Orig();
            return nn::oe::PerformanceMode_Boost;
        }
    };

    // d3hack-custom: Ramaladni's Gift target gate, 0x4F6C90.
    //
    // `CanAddSocketsToItem(target, consumable)` -- returns -1 for a valid target, a GAMEERROR
    // otherwise. Forcing -1 alone did NOT change the UI target list, and the Gift's record
    // holds no AddSocketsType specifiers (probe found zero ids in 0..0x800), and nothing in
    // the binary ever WRITES 0x1A1..0x1A6. So the list is built from something else entirely.
    //
    // Hooked rather than byte-patched so it can report whether the list even asks. If opening
    // the Gift's target list produces no CanAddSockets lines, the list never consults this
    // function and the filter is elsewhere; if it does and the items still do not appear, the
    // caller ignores the answer. Either result kills a whole branch of the search.
    HOOK_DEFINE_TRAMPOLINE(CanAddSockets) {
        static auto Callback(void *pTarget, void *pConsumable) -> int {
            const int ret = Orig(pTarget, pConsumable);
            static int s_nLogged = 0;
            if (s_nLogged < 60) {
                ++s_nLogged;
                u32 gbid = 0;
                if (reinterpret_cast<uintptr_t>(pTarget) >= 0x1000ull)
                    gbid = *reinterpret_cast<const u32 *>(static_cast<u8 *>(pTarget) + 0x3C);
                PRINT("[d3hack-diag] CanAddSockets target=%p gbid=%08X -> %d", pTarget, gbid, ret)
            }
            return global_config.rare_cheats.rama_any_item ? -1 : ret;
        }
    };

    // d3hack-custom: instrumentation for the Ramaladni's Gift target list.
    //
    // Confirmed dead ends, both by measurement not reasoning:
    //   - 0x4F6C90 CanAddSocketsToItem: trampoline logged ZERO calls with the list open.
    //   - 0x4F6BE0 ItemIsSocketable: its six category-flag tests were bypassed and the list
    //     did not change.
    //
    // So log what the game actually asks while the list is on screen. ItemIsSocketable
    // reports whether it is consulted at all, and the flag test reports every query for one
    // of the six category bits (0x1B 0x1C 0x1D 0x1E 0x30 0x32) with the item it was asked
    // about -- if the list filters on categories at all, that is where it will show.
    // set once the inventory/socket UI is up, so the hot flag-test log ignores boot traffic
    inline bool g_bInItemUI = false;

    HOOK_DEFINE_TRAMPOLINE(ItemIsSocketable) {
        static auto Callback(void *pItem) -> int {
            const int ret = Orig(pItem);
            g_bInItemUI    = true;
            static int s_n = 0;
            if (s_n < 300) {
                ++s_n;
                u32 gbid = 0;
                if (reinterpret_cast<uintptr_t>(pItem) >= 0x1000ull)
                    gbid = *reinterpret_cast<const u32 *>(static_cast<u8 *>(pItem) + 0x3C);
                // Log the caller too. This returns 1 for every item now and the list still
                // shows weapons only, so whoever calls it either ignores the answer or is
                // not the list -- the return address distinguishes those.
                const uintptr_t uRet  = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
                const uintptr_t uBase = GameOffset(0);
                PRINT("[d3hack-diag] ItemIsSocketable gbid=%08X -> %d  from %06X", gbid, ret,
                      static_cast<u32>(uRet - uBase))
            }
            return ret;
        }
    };

    HOOK_DEFINE_TRAMPOLINE(ItemFlagTest) {
        static auto Callback(u32 gbid, int nBit) -> int {
            const int ret = Orig(gbid, nBit);
            // Every bit now, not just the six -- five predicates have been eliminated and
            // the list must be testing SOMETHING per item. Gated on g_bInItemUI so boot
            // traffic does not fill the cap: open the Gift list last and read the tail.
            if (g_bInItemUI) {
                // Log the CALLER, not just the query. The six-bit sequence is run by more
                // than one function -- 0x4F6BE0's copy is already bypassed and these queries
                // still happen -- so the return address is what names the real filter.
                static int s_n = 0;
                if (s_n < 300) {
                    ++s_n;
                    const uintptr_t uRet  = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
                    const uintptr_t uBase = GameOffset(0);
                    PRINT("[d3hack-diag] ItemFlagTest gbid=%08X bit=%02X -> %d  from %06X", gbid,
                          nBit, ret, static_cast<u32>(uRet - uBase))
                }
            }
            return ret;
        }
    };

    // d3hack-custom: what does the game actually ask when the Gift's target list opens?
    //
    // Ruled out by measurement so far:
    //   - 0x4F6C90 CanAddSocketsToItem : ZERO calls with the list open.
    //   - 0x4F6BE0 ItemIsSocketable    : bypassed, returns 1 for everything, list unchanged.
    //   - 0x5018C0 (same six-flag shape, and the source of every category query seen while
    //     the list was open) : bypassed too, list STILL unchanged. So those queries were
    //     incidental -- seeing a call is not evidence it gates anything.
    //
    // 0x4F60B0 is the item-action API both the inventory (0x36D048) and the add-sockets
    // confirmation (0x3E4D6C) go through, and -1 means "allowed". Log the action id, the
    // result and the CALLER so the list's own query can be picked out of the traffic. The
    // cap is deliberately high: open the list right before quitting and read the tail, since
    // the log is timestamped.
    HOOK_DEFINE_TRAMPOLINE(ItemActionCheck) {
        static auto Callback(int nAction, void *p1, void *p2) -> int {
            const int ret = Orig(nAction, p1, p2);
            static int s_n = 0;
            if (s_n < 400) {
                ++s_n;
                const uintptr_t uRet  = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
                const uintptr_t uBase = GameOffset(0);
                PRINT("[d3hack-diag] ItemAction act=%02X -> %d  from %06X", nAction, ret,
                      static_cast<u32>(uRet - uBase))
            }
            return ret;
        }
    };

    // d3hack-custom: the generic item-list filter, 0x1F2740.
    //
    // This is what builds filtered item lists. 0x1F2600 maps a filter-kind id to a PREDICATE
    // function pointer (hash table at 0x114F190), and 0x1F2740 loops the item array calling
    // it through `blr x23`, keeping items the predicate accepts.
    //
    // Every ItemIsSocketable call logged came through here (all 300, from 0x1F27A4), and
    // that predicate now returns 1 for everything -- yet the Gift's list is unchanged. The
    // likely reason: those calls belong to some OTHER feature's list (the jeweler's socket
    // UI, say) and the Gift's list uses a different kind id, hence a different predicate.
    //
    // So log the kind id and how many items survived. Open the Gift's list and whichever
    // kind fires at that moment is the one to go after -- then its predicate comes straight
    // out of the 0x1F2600 table.
    HOOK_DEFINE_TRAMPOLINE(ItemListFilter) {
        static auto Callback(void *pCtx, int nKind, void *pOut, void *p3) -> void * {
            auto *ret = Orig(pCtx, nKind, pOut, p3);
            static int s_n = 0;
            if (s_n < 200) {
                ++s_n;
                long long nGot = -1;
                if (reinterpret_cast<uintptr_t>(pOut) >= 0x1000ull)
                    nGot = *reinterpret_cast<const long long *>(static_cast<u8 *>(pOut) + 8);
                const uintptr_t uRet  = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
                const uintptr_t uBase = GameOffset(0);
                PRINT("[d3hack-diag] ItemListFilter kind=%08X kept=%d  from %06X", nKind,
                      static_cast<int>(nGot), static_cast<u32>(uRet - uBase))
            }
            return ret;
        }
    };

    // d3hack-custom: the kind -> predicate registry, 0x1F2600.
    //
    // 0x1F2740 loops the item array calling a predicate obtained from here. Hooking the
    // registry itself yields the whole map in one run: which predicate each filter kind
    // resolves to. ItemIsSocketable (0x4F6BE0) is the predicate for exactly one kind; the
    // Gift's list will name its own. Once a kind is tied to a predicate address, that
    // predicate is the thing to bypass -- no more guessing which of the several six-flag
    // copies matters.
    HOOK_DEFINE_TRAMPOLINE(ItemFilterSelect) {
        static auto Callback(int nKind) -> void * {
            auto *pFn = Orig(nKind);
            static int s_n = 0;
            if (s_n < 120) {
                ++s_n;
                const uintptr_t uBase = GameOffset(0);
                const uintptr_t uFn   = reinterpret_cast<uintptr_t>(pFn);
                PRINT("[d3hack-diag] FilterSelect kind=%02X -> predicate %06X", nKind,
                      static_cast<u32>(uFn >= uBase ? uFn - uBase : uFn))
            }
            return pFn;
        }
    };

    // d3hack-custom: make every socket consumable claim ALL six target categories.
    //
    // AddSocketsType_Weapon..Jewelry (0x1A1..0x1A6) have **no writer anywhere in the
    // binary** -- confirmed by scanning every attribute set site including the neighbour-
    // offset form. So they are never set on Ramaladni's Gift and always read as the
    // attribute default. Whatever UI code asks "which categories can this consumable
    // target", it can only ever get the stock weapons answer, which is exactly the symptom:
    // a scrollable list of every weapon without a socket.
    //
    // Ten patches at the reader end changed nothing, so answer the QUESTION instead: when
    // anything asks for one of those six attribute keys, say yes. The keys are
    // 0xFFFFF1A1..0xFFFFF1A6 (0xFFFFF000 | id).
    //
    // 0x46FAB0 is the generic int attribute getter and is very hot, so the range check is
    // done first and Orig is skipped entirely for our six keys. Gated on RamaladniAnyItem so
    // it can be turned off if the cost shows.
    HOOK_DEFINE_TRAMPOLINE(IntAttribGet) {
        static auto Callback(void *pObj, u32 uKey) -> int {
            if (uKey >= 0xFFFFF1A1u && uKey <= 0xFFFFF1A6u &&
                global_config.rare_cheats.rama_any_item) {
                static int s_n = 0;
                if (s_n < 12) {
                    ++s_n;
                    PRINT("[d3hack-diag] AddSocketsType key %08X -> forced 1", uKey)
                }
                return 1;
            }
            return Orig(pObj, uKey);
        }
    };

    // d3hack-custom: 0x4F8790 ItemHasNoSockets -- the registered predicate that supplies the
    // "without sockets" half of the Gift's list (registry entry CA9860).
    //
    // The AddSocketsType attributes turned out to be read ZERO times all session, so the
    // "weapons" half of the list is not a runtime attribute query. This decides where it
    // comes from instead: log which items the predicate is even OFFERED.
    //
    //   - offered every item (boots included) -> the weapon restriction is applied AFTER
    //     this, downstream of the predicate.
    //   - offered only weapons -> the candidate set is already weapons-only before the
    //     predicate runs, and the restriction lives wherever that set is chosen.
    HOOK_DEFINE_TRAMPOLINE(ItemHasNoSockets) {
        static auto Callback(void *pItem) -> int {
            const int ret = Orig(pItem);
            static int s_n = 0;
            if (s_n < 120) {
                ++s_n;
                u32 gbid = 0;
                if (reinterpret_cast<uintptr_t>(pItem) >= 0x1000ull)
                    gbid = *reinterpret_cast<const u32 *>(static_cast<u8 *>(pItem) + 0x3C);
                PRINT("[d3hack-diag] HasNoSockets gbid=%08X -> %d", gbid, ret)
            }
            return ret;
        }
    };

    // d3hack-custom: do .ui assets pass through the file layer by name?
    //
    // The romfs holds 396 `UI\*.ui` definitions, including a whole
    // ConsoleInventoryList*Template family (Cube, Jeweler, Mystic, Equipment, Dye...). If
    // the Gift's picker declares its filter kind in one of those, that explains why twelve
    // CODE patches did nothing -- the list is described in DATA.
    //
    // Repacking the container is blocked (custom Blizzard format, unmapped index plus a
    // probable checksum at header+0x38), but there is no need to repack: if asset loads pass
    // through the file layer by name, the game decompresses the .ui itself and a hook can
    // read it straight out of memory. That is the cheap way in.
    //
    // Log the first argument of each as a string when it looks like one.
    HOOK_DEFINE_TRAMPOLINE(ProbeFileOpen) {
        static auto Callback(const char *szPath, u64 a1, u64 a2, u64 a3) -> u64 {
            static int s_n = 0;
            if (s_n < 60 && reinterpret_cast<uintptr_t>(szPath) >= 0x1000ull) {
                char b[80];
                int  i = 0;
                for (; i < 79; ++i) {
                    const char c = szPath[i];
                    if (c == 0)
                        break;
                    b[i] = (c >= 0x20 && c < 0x7F) ? c : '.';
                }
                b[i] = 0;
                if (i > 2) {
                    ++s_n;
                    PRINT("[d3hack-diag] FileOpen \"%s\"", b)
                }
            }
            return Orig(szPath, a1, a2, a3);
        }
    };

    // d3hack-custom: every worn item gets its sockets FREE, costing no affix slot.
    //
    // This is what Ramaladni's Gift was wanted for. Rolling Sockets at the Mystic works on
    // every slot now, but enchanting REPLACES an affix, so a socket costs a stat -- and an
    // item only has so many affix slots. The Gift exists precisely to add a socket without
    // spending one, and its picker is UI-data-driven and unreachable from code (twelve
    // mechanisms eliminated; opening the list makes no predicate calls at all).
    //
    // So deliver the outcome instead of the item: SOCKETS (0x187) is READ-ONLY in code --
    // all 25 sites are reads, the writes come from the affix system -- which means the value
    // can simply be answered. Report the configured count for any worn item whose own roll is
    // lower, and the sockets exist alongside its normal affixes rather than instead of one.
    //
    // Scope is deliberately tight:
    //   - only the SOCKETS key, so one integer compare on the hot path and nothing else;
    //   - only ACDs that are items (+0x38 == 2);
    //   - only gbids in the equippable-item set built by PatchItemSocketCategoryFlags, so gems,
    //     potions and crafting mats are untouched;
    //   - never lowers a roll, so a natural 3-socket chest keeps its 3.
    //
    // MaxSockets is already 3 on every worn type, so this stays inside what the game's own
    // socketing UI considers legal.
    // d3hack-custom: ACD_AttributesGetInt (0x46FAB0), shared by every feature that answers
    // an attribute query differently than the game would.
    //
    // One address can only own one trampoline, so features that override an attribute read
    // dispatch on the key here rather than each installing their own hook. The key is
    // `id | (param << 12)`, so param -1 gives 0xFFFFFxxx -- 0x187 Sockets, 0x2C5
    // Cannot_Be_Added_To_AI_Target_List, 0x1E3 Hireling_Class.
    //
    // Attribute ids come from DiIiS's `GameAttribute.List.cs`, which is numbered IDENTICALLY
    // to this build -- verified on four independent anchors (Sockets 391/0x187,
    // Forced_Hireling_Power 688/0x2B0, Cannot_Be_Added_To_AI_Target_List 709/0x2C5,
    // Gold_PickUp_Radius 1076/0x434, that last one matching the id this project had already
    // derived by hand). `re/attrnames.py` recovers the same table straight out of the binary
    // when a cross-check is wanted.
    // d3hack-custom: scale buff durations -- shrines and pylons.
    //
    // Chosen over the game's own HAS_INFINITE_SHRINE_BUFFS attribute deliberately. Infinite
    // buffs would make Flavor of Time and Gloves of Worship pointless, and the whole appeal of
    // those items is that you have to go and get them. Scaling MULTIPLIES whatever the game
    // computed, so the items still do their work and this stretches the result.
    //
    // Durations are not Script Formulas -- a full formula probe over a shrine pickup with
    // Gloves of Worship equipped showed no shrine power at all. They are set through the
    // script binding PowerBuffSetDuration, found via its binding record in .rodata
    // (name at 0xE5F150, function at 0x904BC0). Its two arguments are fetched by 0xB1A1B0:
    //
    //     0x904C40  bl   0xB1A1B0        arg1 -> d0
    //     0x904C4C  mov  v8.16b, v0.16b  arg1 kept in v8
    //     0x904C60  bl   0xB1A1B0        arg2 -> d0
    //     0x904C64  fcvt s0, d0          <-- HOOKED: arg2 IS the duration
    //     0x904C90  fcvtzs w0, d8        arg1 -> int, the buff/power id
    //
    // So d0 is the duration and d8 identifies what it belongs to, both live at 0x904C64.
    //
    // The id is logged for the first calls because the shrine powers are known
    // (Shrine_Power_Blessed 278268 .. Frenzied 278271) but the PYLON powers are not -- they
    // have to be read off a real pickup rather than guessed.
    inline constexpr s32 kShrinePowers[] = {278268, 278269, 278270, 278271};

    inline auto IsShrinePower(s32 sno) -> bool {
        for (s32 p : kShrinePowers)
            if (p == sno)
                return true;
        return false;
    }

    HOOK_DEFINE_INLINE(BuffDurationScale) {
        static void Callback(exl::hook::InlineFloatCtx *ctx) {
            const int nPct = global_config.rare_cheats.shrine_duration_percent;
            const double dDur = ctx->D[0];
            const s32    sno  = static_cast<s32>(ctx->D[8]);

            static int s_nLog = 0;
            if (global_config.rare_cheats.shrine_duration_probe && s_nLog < 40) {
                ++s_nLog;
                PRINT("[d3hack-buff] SetDuration power=%d duration=%d.%03d%s", sno,
                      static_cast<int>(dDur),
                      static_cast<int>((dDur < 0 ? -dDur : dDur) * 1000.0) % 1000,
                      IsShrinePower(sno) ? "  (known shrine)" : "")
            }

            if (nPct == 100 || nPct < 1)
                return;
            if (!IsShrinePower(sno))
                return;
            // Multiply, never replace: Gloves of Worship and Flavor of Time have already had
            // their say by this point and their work must survive.
            ctx->D[0] = dDur * (static_cast<double>(nPct) / 100.0);
        }
    };

    // d3hack-custom: buff-timer hunt -- three routes eliminated BY MEASUREMENT.
    //
    //   1. Script Formulas -- a full formula probe over a shrine pickup with Gloves of Worship
    //      equipped logged no shrine power at all.
    //   2. PowerBuffSetDuration (0x904C64) -- hooked, and across a shrine AND a pylon it logged
    //      exactly one unrelated call (power=0, 16s). Shrines never take that path.
    //   3. BUFF_ICON_END_TICK via ACD_AttributesSetInt -- hook was live (the elite probe had it
    //      installed) and not one write in 0x272..0x291 appeared.
    //
    // The float setter is checked in EliteEventProbeFloat below rather than with a second hook
    // of its own: it is already hooked there, and two trampolines on one function is a bug
    // waiting to happen.

    HOOK_DEFINE_TRAMPOLINE(AttrGetInt) {
        static auto Callback(void *pObj, u32 uKey) -> int {
            const int ret = Orig(pObj, uKey);

            // ---- shrine and pylon buffs that do not run out ---------------------------
            //
            // The game already has the concept: HAS_INFINITE_SHRINE_BUFFS (0x553) is a real
            // attribute it checks when deciding whether a shrine buff should tick down. So
            // rather than hunting the duration formula for every shrine and pylon and scaling
            // each one, answer that question with yes.
            //
            // Same reasoning as the follower no-aggro hook above: answering the query beats
            // writing the attribute, because there is nothing to re-apply after a world change
            // and nothing to keep in sync when a buff is re-acquired.
            //
            // Pylons vanish when a new Greater Rift opens regardless, so the blast radius is
            // one rift; in Nephalem rifts and the open world it is just generous.
            if (uKey == 0xFFFFF553u) {
                if (!global_config.rare_cheats.infinite_shrine_buffs)
                    return ret;
                static int s_n = 0;
                if (s_n < 3) {
                    ++s_n;
                    PRINT("[d3hack-custom] shrine/pylon buffs set to never expire (was %d)", ret)
                }
                return 1;
            }

            // ---- followers the monsters should walk past ------------------------------
            // Answering the AI's own question is better than writing the attribute at spawn:
            // there is nothing to keep in sync, nothing to re-apply after a world change, and
            // if the follower is swapped mid-run the next query already tells the truth.
            // Hireling_Class: 1 Templar, 2 Scoundrel, 3 Enchantress.
            if (uKey == 0xFFFFF2C5u) {
                if (ret != 0 || !global_config.rare_cheats.follower_no_aggro)
                    return ret;
                if (reinterpret_cast<uintptr_t>(pObj) < 0x1000ull)
                    return ret;
                const int nClass = Orig(pObj, 0xFFFFF1E3u);
                if (nClass != 2 && nClass != 3)
                    return ret;
                static int s_n = 0;
                if (s_n < 4) {
                    ++s_n;
                    PRINT("[d3hack-custom] follower no-aggro: hireling class %d hidden from AI "
                          "target lists", nClass)
                }
                return 1;
            }

            if (uKey != 0xFFFFF187u)
                return ret;
            const int nWant = global_config.rare_cheats.free_sockets;
            if (nWant <= 0 || ret >= nWant)
                return ret;
            if (reinterpret_cast<uintptr_t>(pObj) < 0x1000ull)
                return ret;
            auto *p = static_cast<u8 *>(pObj);
            if (*reinterpret_cast<const s32 *>(p + 0x38) != 2)
                return ret;  // not an item ACD
            const u32 gbid = *reinterpret_cast<const u32 *>(p + 0x3C);
            if (!std::binary_search(d3::g_arEquippableGbids.begin(), d3::g_arEquippableGbids.end(), gbid))
                return ret;  // not equippable
            static int s_n = 0;
            if (s_n < 10) {
                ++s_n;
                PRINT("[d3hack-custom] free sockets: gbid %08X %d -> %d", gbid, ret, nWant)
            }
            return nWant;
        }
    };

    inline void SetupUtilityHooks() {
#if D3HACK_ENABLE_UTILITY_DEBUG_HOOKS
        SetupUtilityDebugHooks();
#endif

        if (global_config.rare_cheats.active && global_config.rare_cheats.equip_any_slot) {
            EquipAny::
                InstallAtFuncPtr(ACDInventoryItemAllowedInSlot);
        }

        if (global_config.resolution_hack.active) {
            VarResHook::
                InstallAtSymbol("sym_var_res_label");
        }
        if (global_config.rare_cheats.active && global_config.rare_cheats.xp_gr_bonus > 1) {
            // d3hack-custom: the ENTRY of AddExperience, see the note on the hook. Skipping the
            // install removes the greater-rift bonus and the pool bonus together, which is the
            // A/B for "is the experience work responsible for this at all".
            if (global_config.rare_cheats.xp_hook_mode > 0) {
                s_nXpHookMode = global_config.rare_cheats.xp_hook_mode;
                HighGRExperience::InstallAtOffset(0x79FE6C);  // NOT 0x79FE40 or 0x79FE84
            }
            PRINT("[d3hack-custom] experience hook mode %d (0=absent 1=inert 2=probe 3=full)",
                  global_config.rare_cheats.xp_hook_mode)
            // These two used to be gated together, which is how they stayed tangled for so
            // long. Loading the saved count is harmless bookkeeping; installing a hook at an
            // address that was only ever a guess is not, and turning the pair on correlates 1:1
            // with the world-entry crash across eight runs. They are separate knobs now.
            if (global_config.rare_cheats.pool_xp_percent > 0)
                pools::Load();
            if (global_config.rare_cheats.wells_as_pools ||
                global_config.rare_cheats.well_spawn_probe) {
                HealthWellToPool::InstallAtOffset(0x94D570);  // d3hack-custom
                // MarkerWellToPool (0x4EF6A4) retired -- it writes ACD+0x14, a downstream
                // copy. Proven inert in game: "field write 1" logged, well unchanged.
                ActorSpawnWellToPool::InstallAtOffset(0x86E2E0);  // d3hack-custom
                PRINT_LINE("[d3hack-custom] actor-spawn funnel hook installed at 0x86E2E0");
                PRINT("[d3hack-custom] health-well swap hook installed at 0x94D570 (swap=%d probe=%d)",
                      global_config.rare_cheats.wells_as_pools ? 1 : 0,
                      global_config.rare_cheats.well_spawn_probe ? 1 : 0)
            }
            if (global_config.rare_cheats.gr_density_multiplier > 1 ||
                global_config.rare_cheats.world_density_multiplier > 1) {
                GreaterRiftDensity::InstallAtOffset(0x94BCAC);  // d3hack-custom
                PRINT("[d3hack-custom] density hook installed at 0x94BCAC (rift x%d, world "
                      "x%d, rifts_only=%d)",
                      global_config.rare_cheats.gr_density_multiplier,
                      global_config.rare_cheats.world_density_multiplier,
                      global_config.rare_cheats.gr_density_rifts_only ? 1 : 0)
            }
            // d3hack-custom: the rift map pick. Installed whenever there is a ban list OR
            // the probe is on -- banning must not depend on a diagnostic being enabled.
            if (!global_config.rare_cheats.banned_rift_maps.empty() ||
                !global_config.rare_cheats.map_density_overrides.empty() ||
                global_config.rare_cheats.prefer_low_fog ||
                global_config.rare_cheats.map_name_overlay ||
                global_config.rare_cheats.world_gen_probe) {
                RiftMapPick::InstallAtOffset(0x785D30);
                RiftTilesetAssign::InstallAtOffset(0x78866C);   // d3hack-custom: the map itself
                // d3hack-custom: learn id -> map, then swap the ID before anything derives
                // from it. RiftMapAssign stays installed as the readout and as a backstop for
                // ids the resolver has not been seen for yet.
                TilesetResolve::InstallAtOffset(0x677180);
                RiftAreaSwap::InstallAtOffset(0x784EE4);   // read-only now, see the hook body
                RiftMapAssign::InstallAtOffset(0x4BC450);      // d3hack-custom: readout
                // d3hack-custom: BuffDurationScale (0x904C64) IS DELIBERATELY NOT INSTALLED.
                //
                // Two independent reasons, both already established by measurement:
                //
                //   1. It cannot work. Route #2 of the buff-timer hunt was eliminated because
                //      across a shrine AND a pylon it logged exactly one unrelated call
                //      (power=0, 16s). Shrines never take that path.
                //   2. It is actively harmful. It is an InlineFloatCtx hook sitting on an
                //      `fcvt` inside a float save/restore path, and it appeared to corrupt
                //      float registers globally -- the user reported the game running in
                //      "speedhack mode". HANDOFF.md records it as "Removed."
                //
                // It was never actually removed, only left gated. The trap: HANDOFF tells the
                // next session to run the item-power probe "gated on ShrineDurationProbe", and
                // that flag re-installed THIS hook as a side effect. The flag still enables the
                // real probes (the ITEM_POWER_PASSIVE reads); it no longer arms this.
                //
                // ShrineDurationPercent is therefore inert. That is correct -- it never worked.
                (void) 0;
                d3::ResolveRiftBans();
                PRINT("[d3hack-ban] map assign hooked at 4BC450 (%d maps banned)", d3::s_nBannedMap)
                PRINT("[d3hack-pick] weather pick hooked at 785D30 (lowfog=%d), tileset assign "
                      "at 78866C", global_config.rare_cheats.prefer_low_fog ? 1 : 0)
            }
            // d3hack-custom: the world factory, read-only. Its own flag -- this is pure
            // diagnostics and must not ride along with the ban list, which is on permanently.
            // WorldCreateProbe also carries the map overlay's ARRIVAL signal (arg2 == 1 on a
            // rift level world), so it must install whenever the overlay is on -- not only when
            // the diagnostic is. The rest of the probes stay behind WorldGenProbe.
            // Also required by GreaterRiftDensityRiftsOnly: this hook is what CLEARS
            // s_snoAssignedMap when you leave a rift, and without it the gate would treat town
            // as a rift forever.
            if (global_config.rare_cheats.world_gen_probe ||
                global_config.rare_cheats.map_name_overlay ||
                global_config.rare_cheats.gr_density_rifts_only) {
                WorldCreateProbe::InstallAtOffset(0xD956C);
            }
            if (global_config.rare_cheats.damage_bonus_probe) {
                // Confirm EACH install and report the symbol pointers. Four probes have now
                // come back empty and there was no way to tell "never fired" from "never
                // installed" -- the exact ambiguity this project has already paid for once.
                PRINT("[d3hack-dmg] symbols: ACD_Get=%d FastGet=%d ACD_SetF=%d ACD_SetI=%d",
                      ACD_AttributesGetInt != nullptr, FastAttribGetValueInt != nullptr,
                      ACD_AttributesSetFloat != nullptr, ACD_AttributesSetInt != nullptr)
                if (ACD_AttributesGetInt != nullptr) {
                    DamageBonusReadProbe::InstallAtFuncPtr(ACD_AttributesGetInt);
                    PRINT_LINE("[d3hack-dmg] installed on ACD_AttributesGetInt");
                }
                if (FastAttribGetValueInt != nullptr) {
                    DamageBonusFastRead::InstallAtFuncPtr(FastAttribGetValueInt);
                    PRINT_LINE("[d3hack-dmg] installed on FastAttribGetValueInt");
                }
                if (ACD_AttributesSetFloat != nullptr) {
                    DamageBonusFloatProbe::InstallAtFuncPtr(ACD_AttributesSetFloat);
                    PRINT_LINE("[d3hack-dmg] installed on ACD_AttributesSetFloat");
                }
                if (FastAttribGetValueFloat != nullptr) {
                    DamageBonusFastReadF::InstallAtFuncPtr(FastAttribGetValueFloat);
                    PRINT_LINE("[d3hack-dmg] installed on FastAttribGetValueFloat");
                }
                if (ACD_AttributesGetFloat != nullptr) {
                    DamageBonusAcdReadF::InstallAtFuncPtr(ACD_AttributesGetFloat);
                    PRINT_LINE("[d3hack-dmg] installed on ACD_AttributesGetFloat");
                }
                // Proof of life, independent of the filter: count EVERY read that reaches the
                // FastAttrib getter and report the first few attribute ids seen. If this stays
                // silent the hook is not running at all; if it prints but never shows 0x50D,
                // then item-power reads genuinely do not come through here.
                PRINT_LINE("[d3hack-dmg] (a 'saw N reads' line follows once traffic starts)");
            }
            if (global_config.rare_cheats.set_bonus_any_weapon) {
                SetBonusSkillCapture::InstallAtOffset(0x97B694);
                SetBonusAnyWeapon::InstallAtOffset(0x97B6AC);
                PRINT_LINE("[d3hack-set] weapon-gate bypass installed at 0x97B694 / 0x97B6AC");
            }
            if (global_config.rare_cheats.momentum_autofire_every > 0 ||
                global_config.rare_cheats.momentum_no_decay) {
                MomentumAutoFire::InstallAtOffset(0x8560A4);   // after w3 is set, args live
                MomentumKeepAlive::InstallAtOffset(0x98F9A8);  // the expiry branch
                // MomentumNoTick (0x98F464) is NOT installed: suppressing the Lua tick also
                // removed the movement speed the same effect applies. Stretch instead.
                MomentumSlowTick::InstallAtOffset(0x98F4E4);   // the reschedule interval
                PRINT_LINE("[d3hack-momentum] expiry keep-alive at 0x98F9A8 (no external calls)");
                MomentumGrantBlock::InstallAtOffset(0x8565CC);  // the grant block itself
                PRINT("[d3hack-momentum] auto-fire grant every %d strafe ticks",
                      global_config.rare_cheats.momentum_autofire_every)
            }
            // The auto-fire gate reads s_nMomentumNow, which is only maintained by the buff
            // hook below -- so that hook has to install whenever ANY momentum feature is on.
            // Gating it on the two shortcuts alone left the counter pinned at 0 and the
            // auto-fire silently rejected every tick.
            if ((global_config.rare_cheats.momentum_no_decay ||
                 global_config.rare_cheats.momentum_autofire_every > 0 ||
                 global_config.rare_cheats.momentum_duration_pct > 100) &&
                !global_config.rare_cheats.buff_stack_probe) {
                BuffStackProbeId::InstallAtOffset(0x69EDB8);
                PRINT_LINE("[d3hack-momentum] Momentum decay hold installed at 0x69EDB8");
            }
            if (global_config.rare_cheats.buff_stack_probe) {
                PRINT("[d3hack-buffstack] symbols: SetInt=%d SetFloat=%d",
                      ACD_AttributesSetInt != nullptr, ACD_AttributesSetFloat != nullptr)
                if (ACD_AttributesSetInt != nullptr) {
                    BuffStackProbeInt::InstallAtFuncPtr(ACD_AttributesSetInt);
                    PRINT_LINE("[d3hack-buffstack] installed on ACD_AttributesSetInt");
                }
                if (ACD_AttributesSetFloat != nullptr) {
                    BuffStackProbeFloat::InstallAtFuncPtr(ACD_AttributesSetFloat);
                    PRINT_LINE("[d3hack-buffstack] installed on ACD_AttributesSetFloat");
                }
                BuffStackProbeId::InstallAtOffset(0x69EDB8);
                MomentumGrantOuter::InstallAtOffset(0x856080);
                MomentumGrantArgs::InstallAtOffset(0x8560A0);   // args still in w0..w7
                MomentumGrantDispatch::InstallAtOffset(0x8522E0);
                PRINT_LINE("[d3hack-buffstack] installed on the id setter 0x69ED90");
                PRINT_LINE("[d3hack-mgrant] grant-path entry probes at 0x856080 / 0x8522E0");
                PRINT_LINE("[d3hack-buffstack] TEMPORARY -- hot hook, turn off when done");
            }
            if (global_config.rare_cheats.empowered_gem_upgrades > 0) {
                GemGrantMaxCapture::InstallAtOffset(0x77BBE0);
                GemGrantBonusSet::InstallAtOffset(0x77BC84);
                PRINT("[d3hack-gw] empowered gem grant hooked at 0x77BBE0 / 0x77BC84 (%d)",
                      global_config.rare_cheats.empowered_gem_upgrades)
            }
            if (global_config.rare_cheats.rift_reward_probe ||
                global_config.rare_cheats.damage_bonus_probe) {
                GemUpgradeWriteProbe::InstallAtFuncPtr(ACD_AttributesSetInt);
                PRINT_LINE("[d3hack-gw] gem-upgrade WRITE probe installed (0x580/0x581/0x582)");
                RiftRewardProbe::InstallAtFuncPtr(ACD_AttributesGetInt);
                PRINT_LINE("[d3hack-rr] rift-reward attribute probe installed (0x570..0x59F)");
            }
            // EmpoweredGemUpgrades (the 0x243ABC read hook) is deliberately NOT installed.
            // It changed the number on screen and nothing else -- the budget is owned by the
            // WRITE at +77BC94, which GemUpgradeWriteProbe now adjusts.
            // RiftPlanDump carries the map SUBSTITUTION, not just the dump, so it must
            // install whenever the feature is on -- gating it on the diagnostic meant turning
            // WorldGenProbe off silently disabled banning. The other three are pure
            // diagnostics and stay behind the probe flag.
            if (global_config.rare_cheats.world_gen_probe ||
                global_config.rare_cheats.rift_map_substitute) {
                RiftPlanQuery::InstallAtOffset(0x816E98);   // sets the once-per-rift trigger
                RiftPlanDump::InstallAtOffset(0x816EAC);
                // Install confirmation, because this pair carries the FEATURE. Silence from a
                // hook with no install line proves nothing, and that is precisely how the
                // WorldGenProbe trigger gate went unnoticed.
                PRINT("[d3hack-plan] plan hooks installed at 0x816E98 / 0x816EAC "
                      "(substitute=%d probe=%d)",
                      global_config.rare_cheats.rift_map_substitute ? 1 : 0,
                      global_config.rare_cheats.world_gen_probe ? 1 : 0)
            }
            if (global_config.rare_cheats.world_gen_probe) {
                RiftPlanAppendProbe::InstallAtOffset(0x8174AC);
                RiftRecordInit::InstallAtOffset(0x676B68);
                PRINT_LINE("[d3hack-wc] world-factory probe installed at 0xD956C (read-only)");
                PRINT_LINE("[d3hack-plan] rift floor-plan probe installed at 0x8174AC (read-only)");
            }
            if (global_config.rare_cheats.power_random_probe ||
                global_config.rare_cheats.world_gen_probe ||   // d3hack-custom: same inner RNG
                (global_config.rare_cheats.power_random_bias_sno != 0 &&
                 global_config.rare_cheats.power_random_bias_pct != 100)) {
                ScriptRandomHook::InstallAtOffset(0x8D5910);     // d3hack-custom
                ScriptRandomIntHook::InstallAtOffset(0x8D5860);  // d3hack-custom
                RandomIntInnerHook::InstallAtOffset(0x9384B0);   // d3hack-custom
                if (global_config.rare_cheats.world_gen_probe) {
                    RngCoreHook::InstallAtOffset(0xA48200);       // d3hack-custom: LCG core
                    // d3hack-custom: all three sInitializeWorld call sites. Confirm the
                    // install in the log -- a hook that never fires looks exactly like a hook
                    // that was never installed, and that ambiguity already cost one run.
                    WorldEntryRecord::InstallAtOffset(0x812114);
                    WorldEntryRecord2::InstallAtOffset(0x8125E8);
                    WorldEntryRecord3::InstallAtOffset(0x8156CC);
                    NativeRangeRoll::InstallAtOffset(0x11E660);   // d3hack-custom
                    PRINT("[d3hack-entry] installed at %d world-entry call sites + native "
                          "range roll at 11E660", 3)
                }
                PRINT("[d3hack-custom] script RNG hook installed at 0x8D5910 (probe=%d bias=%d%%)",
                      global_config.rare_cheats.power_random_probe ? 1 : 0,
                      global_config.rare_cheats.power_random_bias_pct)
            }
            if (global_config.rare_cheats.power_formula_probe ||
                (global_config.rare_cheats.power_formula_sno != 0 &&
                 global_config.rare_cheats.power_formula_percent != 100)) {
                PowerFormulaHook::InstallAtOffset(0x9A0900);   // d3hack-custom
                if (global_config.rare_cheats.power_record_probe)
                    PowerRecordProbe::InstallAtOffset(0x8A02D0);  // d3hack-custom
                PRINT("[d3hack-custom] power formula hook installed at 0x9A0900 (probe=%d)",
                      global_config.rare_cheats.power_formula_probe ? 1 : 0)
            }
            if (global_config.rare_cheats.pool_grant_hook) {
                pools::Load();
                PoolGranted::InstallAtOffset(0x7A0BA0);  // d3hack-custom
                PRINT_LINE("[d3hack-custom] pool grant hook installed at 0x7A0BA0");
            }
            if (global_config.rare_cheats.pool_touch_hook) {
                pools::Load();
                PoolOfReflectionTouched::InstallAtOffset(0x2BC520);  // d3hack-custom
                PRINT_LINE("[d3hack-custom] pool touch hook INSTALLED at 0x2BC520 -- unverified "
                           "address, implicated in the world-entry crash. Turn "
                           "PoolOfReflectionTouchHook off if the game dies entering a world.");
            }
            CanAddSockets::InstallAtOffset(0x4F6C90);  // d3hack-custom
            AttrGetInt::InstallAtOffset(0x46FAB0);     // d3hack-custom
            ItemHasNoSockets::InstallAtOffset(0x4F8790);  // d3hack-custom
            ItemIsSocketable::InstallAtOffset(0x4F6BE0);  // d3hack-custom
            // 0x4F44F0 is extremely hot (535 call sites); only hook it when probing.
            if (global_config.rare_cheats.item_socket_probe) {
                ItemFlagTest::InstallAtOffset(0x4F44F0);     // d3hack-custom
                ItemActionCheck::InstallAtOffset(0x4F60B0);  // d3hack-custom
                ItemListFilter::InstallAtOffset(0x1F2740);   // d3hack-custom
                ItemFilterSelect::InstallAtOffset(0x1F2600); // d3hack-custom
            }
            PRINT("[d3hack-custom] xp x%d from GR %d, pools +%d%% each, <=%d levels per grant "
                  "(hook 0x%X)",
                  global_config.rare_cheats.xp_gr_bonus, global_config.rare_cheats.xp_gr_bonus_min_gr,
                  global_config.rare_cheats.pool_xp_percent,
                  global_config.rare_cheats.pool_xp_levels_per_grant, 0x79FE6C)
        }

        // d3hack-custom: partial blood shard pickup, and the probes that watch it.
        //
        // The split hooks are the SHIPPING feature, so they install on their own flag -- not
        // on shard_probe. Gating a feature behind its diagnostic is the exact mistake that
        // silently uninstalled ViewDolly, documented a few blocks down.
        if (global_config.rare_cheats.active &&
            (global_config.rare_cheats.partial_currency_pickup ||
             global_config.rare_cheats.shard_probe)) {
            ShardPileSplit::InstallAtOffset(0x48F9E0);
            ShardPileKeep::InstallAtOffset(0x48F9AC);
            PRINT("[d3hack-shard] pile split hooks installed (gate 0x48F9E0, keep 0x48F9AC)%s",
                  "")
        }
        if (global_config.rare_cheats.active && global_config.rare_cheats.shard_probe) {
            CurrencyModifyProbe::InstallAtOffset(0x47FF40);
            ShardCapProbe::InstallAtOffset(0x48F304);
            PRINT("[d3hack-shard] probes installed (modify 0x47FF40, clamp 0x48F304)%s", "")
        }

        // d3hack-custom: THE camera zoom -- scale the eye where the camera update stages it.
        // ViewDolly is the shipping knob and lives inside this hook, so it MUST be part of
        // the install condition -- turning the diagnostic flags off would otherwise silently
        // uninstall the feature.
        if (global_config.rare_cheats.active &&
            (global_config.rare_cheats.view_dolly != 0.0f ||
             global_config.rare_cheats.camera_dist_scale > 0.0f ||
             global_config.rare_cheats.camera_field_dump ||
             global_config.rare_cheats.camera_dist_log)) {
            // 0x2651F0 turned out to be the MENU camera update -- the vec3s it stages are
            // normalised basis vectors, and scaling them distorted the menu screens while
            // leaving gameplay untouched. Census the builder itself instead.
            ViewBuildCensus::InstallAtOffset(0x29B590);
            PRINT("[d3hack-vcall] camera hooked at 0x29B590, dolly %d.%03d",
                  ObsWhole(global_config.rare_cheats.view_dolly),
                  ObsMilli(global_config.rare_cheats.view_dolly))
        }

        // d3hack-custom: the folded colour/light setter -- NOT the camera. Kept off.
        if (global_config.rare_cheats.active &&
            (global_config.rare_cheats.camera_dist_scale > 0.0f ||
             global_config.rare_cheats.camera_dist_log)) {
            if (global_config.rare_cheats.camera_dist_caller != 0) {
                CameraEyeSet::InstallAtOffset(0x29B4D0);
                PRINT("[d3hack-eye] folded setter hooked at 0x29B4D0 (caller 0x%X)",
                      static_cast<u32>(global_config.rare_cheats.camera_dist_caller))
            }
        }

        // d3hack-custom: view-matrix dolly -- the source-side zoom.
        if (global_config.rare_cheats.active &&
            (global_config.rare_cheats.view_dolly != 0.0f ||
             global_config.rare_cheats.view_dolly_log)) {
            // superseded by the eye dolly at the view-builder entry; kept installable only
            // when its own log flag is set, so ViewDolly no longer double-applies.
            if (global_config.rare_cheats.view_dolly_log)
                ViewMatrixDolly::InstallAtOffset(0x29B754);
            PRINT("[d3hack-dolly] view-matrix hook installed at 0x29B754 (dolly %d.%03d)",
                  ObsWhole(global_config.rare_cheats.view_dolly),
                  ObsMilli(global_config.rare_cheats.view_dolly))
        }

        // d3hack-custom: the camera work rides the ACD attribute hook. Install it on its own
        // when the elite probe is off, so a diagnostic run does not have to drag the combat
        // log's probes -- and their log volume -- along with it.
        if (global_config.rare_cheats.active && !global_config.rare_cheats.elite_event_probe &&
            (global_config.rare_cheats.camera_trap || global_config.rare_cheats.camera_observer_scan ||
             global_config.rare_cheats.shrine_duration_probe)) {
            EliteEventProbe::InstallAtFuncPtr(ACD_AttributesSetInt);
            PRINT("[d3hack-own] ACD hook installed for the camera probes only%s", "")
        }

        // d3hack-custom: elite engage/kill event probe. Read-only, off by default.
        if (global_config.rare_cheats.active && global_config.rare_cheats.elite_event_probe) {
            s_bEliteEventProbe = true;
            EliteEventProbe::InstallAtFuncPtr(ACD_AttributesSetInt);
            s_bHandlerProbe = true;
            EngageHandlerProbe::InstallAtOffset(0x853500);
            KillHandlerProbe::InstallAtOffset(0x842DF0);
            PRINT("[d3hack-handler] entry probes installed (engage 0x853500, kill 0x842DF0)%s", "")
            EliteEventProbeFloat::InstallAtFuncPtr(ACD_AttributesSetFloat);
            PRINT("[d3hack-elite] event probe installed (attrs 0x508 0x509 0x472)%s", "")
        }

        // d3hack-custom: main-view camera probe / observer swap. Off by default, and the
        // hook is not installed at all unless one of the two is set -- GlobalSNOGet is hot.
        if (global_config.rare_cheats.active &&
            (global_config.rare_cheats.camera_observer_probe ||
             global_config.rare_cheats.elite_event_probe ||
             global_config.rare_cheats.camera_observer_override != 0)) {
            s_bCameraObserverProbe    = global_config.rare_cheats.camera_observer_probe;
            s_nCameraObserverOverride = global_config.rare_cheats.camera_observer_override;
            CameraObserver::InstallAtFuncPtr(GlobalSNOGet);
            PRINT("[d3hack-camera] observer hook installed (probe=%d override=0x%X)",
                  s_bCameraObserverProbe ? 1 : 0, s_nCameraObserverOverride)
        }

        // d3hack-custom: pair the per-stat getter patch with the category pool, or the
        // pool stays at the stock 4 x 50 = 200 and the extra per-stat room is unreachable.
        if (global_config.rare_cheats.active && global_config.rare_cheats.paragon_stat_cap > 50) {
            s_nParagonLimitFactor = ParagonLimitFactor();
            ParagonCategoryCapacity::InstallAtOffset(0x527870);
            PRINT("[d3hack-custom] paragon category pool scaled x%d at 0x527870 "
                  "(non-Core capacity 200 -> %d)",
                  s_nParagonLimitFactor, 200 * s_nParagonLimitFactor)
        }

        if (global_config.loot_modifiers.active) {
            ForceAncient::
                InstallAtFuncPtr(LootRollForAncientLegendary);
        }
        if (global_config.rare_cheats.active && global_config.rare_cheats.move_speed != 1.0) {
            MoveSpeed::
                InstallAtFuncPtr(move_speed);
        }
        if (global_config.rare_cheats.active && global_config.rare_cheats.attack_speed != 1.0) {
            AttackSpeed::
                InstallAtFuncPtr(attack_speed);
        }
        if (global_config.rare_cheats.active && global_config.rare_cheats.floating_damage_color) {
            FloatingDmgHook::
                InstallAtSymbol("sym_floating_dmg");
        }

        if (global_config.resolution_hack.spoof_docked) {
            SpoofPerfMode::InstallAtFuncPtr(nn::oe::GetPerformanceMode);
            SpoofDocked::InstallAtFuncPtr(nn::oe::GetOperationMode);
        }
    }

}  // namespace d3

namespace d3 {
    inline void MapFloatsFlush() {
        MapFloatsSave();
    }
}  // namespace d3
