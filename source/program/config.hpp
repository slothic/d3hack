#pragma once
#include "types.h"
#include "tomlplusplus/toml.hpp"
#include <algorithm>
#include <cmath>
#include <string>

#define D3HACK_SEASON_EVENT_FLAGS(X)               \
    X(IgrEnabled, false, false, "")                \
    X(AnniversaryEnabled, false, false, "")        \
    X(EasterEggWorldEnabled, false, false, "")     \
    X(DoubleRiftKeystones, true, false, "")        \
    X(DoubleBloodShards, true, false, "")          \
    X(DoubleTreasureGoblins, true, false, "")      \
    X(DoubleBountyBags, true, false, "")           \
    X(RoyalGrandeur, false, false, "")             \
    X(LegacyOfNightmares, false, false, "")        \
    X(TriunesWill, false, false, "")               \
    X(Pandemonium, false, false, "")               \
    X(KanaiPowers, false, false, "")               \
    X(TrialsOfTempests, false, false, "")          \
    X(ShadowClones, false, false, "")              \
    X(FourthKanaisCubeSlot, false, false, "")      \
    X(EtherealItems, false, false, "EthrealItems") \
    X(SoulShards, false, false, "")                \
    X(SwarmRifts, false, false, "")                \
    X(SanctifiedItems, false, false, "")           \
    X(DarkAlchemy, true, true, "")                 \
    X(NestingPortals, false, false, "")

struct PatchConfig {
    bool initialized   = false;
    bool defaults_only = true;

    enum class SeasonEventMapMode : u8 {
        MapOnly,
        OverlayConfig,
        Disabled,
    };

    struct ResolutionHackConfig {
        bool  active                = true;
        u32   target_resolution     = 1080;    // boosted docked resolution
        float min_res_scale         = 85.0f;   // boosted; default is 70%
        float max_res_scale         = 100.0f;  // default
        bool  spoof_docked          = false;
        bool  exp_scheduler         = true;
        float output_handheld_scale = 80.0f;  // percent (0 = auto/stock)

        struct ExtraConfig {
            static constexpr s32 kUnset          = -1;
            static constexpr s32 kMaxDimension   = 16384;
            static constexpr s32 kMaxRefreshRate = 1000;
            static constexpr s32 kMaxBitDepth    = 64;
            static constexpr s32 kMaxMsaaLevel   = 16;

            s32 window_left   = kUnset;
            s32 window_top    = kUnset;
            s32 window_width  = kUnset;
            s32 window_height = kUnset;
            s32 ui_opt_width  = kUnset;
            s32 ui_opt_height = kUnset;
            s32 render_width  = kUnset;
            s32 render_height = kUnset;
            s32 refresh_rate  = kUnset;
            s32 bit_depth     = kUnset;
            s32 msaa_level    = kUnset;
        };

        static constexpr u32 kClampTextureResolutionDefault = 1152;
        static constexpr u32 kClampTextureResolutionMin     = 100;
        static constexpr u32 kClampTextureResolutionMax     = 9999;
        u32                  clamp_texture_resolution       = kClampTextureResolutionDefault;

        ExtraConfig extra {};

        static constexpr float kAspectRatio          = 16.0f / 9.0f;
        static constexpr float kHandheldScaleMin     = 40.0f;
        static constexpr float kHandheldScaleMax     = 100.0f;
        static constexpr float kHandheldScaleStep    = 5.0f;
        static constexpr float kHandheldScaleDefault = 80.0f;

        static constexpr u32 WidthForHeight(u32 height) {
            return static_cast<u32>(height * kAspectRatio);
        }

        constexpr void SetTargetRes(u32 height) { target_resolution = height; }

        static constexpr u32 AlignEven(u32 value) { return value & ~1u; }
        // static constexpr u32 AlignDownPow2(u32 value, u32 alignment) { return value & ~(alignment - 1u); }

        constexpr u32 OutputWidthPx() const { return AlignEven(WidthForHeight(OutputHeightPx())); }
        constexpr u32 OutputHeightPx() const { return AlignEven(target_resolution); }

        static float NormalizeHandheldScale(float value) {
            if (value <= 0.0f) {
                return 0.0f;
            }
            value = std::clamp(value, kHandheldScaleMin, kHandheldScaleMax);
            if (kHandheldScaleStep > 0.0f) {
                value = std::round(value / kHandheldScaleStep) * kHandheldScaleStep;
            }
            return std::clamp(value, kHandheldScaleMin, kHandheldScaleMax);
        }

        float HandheldScaleFraction() const {
            if (output_handheld_scale <= 0.0f) {
                return kHandheldScaleDefault * 0.01f;
            }
            return std::clamp(output_handheld_scale, kHandheldScaleMin, kHandheldScaleMax) * 0.01f;
        }

        u32 OutputHandheldHeightPx() const {
            const float scale = HandheldScaleFraction();
            if (scale <= 0.0f) {
                return 0u;
            }
            const float scaled = scale * static_cast<float>(target_resolution);
            return AlignEven(static_cast<u32>(std::lroundf(scaled)));
        }

        u32 OutputHandheldWidthPx() const { return WidthForHeight(OutputHandheldHeightPx()); }

        constexpr bool ClampTexturesEnabled() const { return clamp_texture_resolution != 0; }
        constexpr u32  ClampTextureHeightPx() const { return clamp_texture_resolution; }
        constexpr u32  ClampTextureWidthPx() const { return WidthForHeight(ClampTextureHeightPx()); }
    };

    struct {
        bool active         = true;
        bool allow_online   = false;
        u32  current_season = 30;
        bool spoof_ptr      = false;
    } seasons;

    struct {
        bool active      = false;
        bool random      = true;
        u32  range_start = 0;
        u32  range_end   = 20;
    } challenge_rifts;

    struct {
        bool               active        = true;
        SeasonEventMapMode SeasonMapMode = SeasonEventMapMode::Disabled;
        // d3hack-custom: let Ethereals / Soul Shards / Sanctified items drop and be
        // usable on a non-seasonal hero, which is otherwise refused outright.
        bool               AllowSeasonalItemsOffSeason = false;
#define D3HACK_SEASON_EVENT_FIELD(name, default_config, default_map, legacy_key) bool name = default_config;
        D3HACK_SEASON_EVENT_FLAGS(D3HACK_SEASON_EVENT_FIELD)
#undef D3HACK_SEASON_EVENT_FIELD
    } events;

    struct {
        bool   active                  = true;
        double move_speed              = 2.5;
        double attack_speed            = 1.0;
        bool   floating_damage_color   = false;
        bool   guaranteed_legendaries  = false;
        bool   drop_anything           = false;
        bool   instant_portal          = true;
        bool   no_cooldowns            = false;
        bool   instant_craft_actions   = true;
        bool   any_gem_any_slot        = false;
        bool   auto_pickup             = false;
        bool   equip_any_slot          = false;
        bool   unlock_all_difficulties = true;
        bool   easy_kill_damage        = false;
        bool   infinite_mp             = true;
        bool   cube_no_consume         = false;
        int    momentum_autofire_every = 0;    // d3hack-custom: EXPERIMENTAL. Gears of Dreadlands auto-fires your last primary while strafing, but that shot never reaches the hit path that grants Momentum, so stacks bleed away. This re-invokes the grant path with the primary's power SNO every Nth Strafe tick (0 = off). 8 is a sensible starting value.
        int    momentum_duration_pct   = 100;  // d3hack-custom: Gears of Dreadlands Momentum buff duration, percent of stock (100 = off). Stacks drain as the buff's timer runs out, so lengthening the timer is what stops them falling off while you strafe. 100..10000.
        bool   momentum_no_decay       = false;// d3hack-custom: Gears of Dreadlands Momentum stacks stop decaying. They still build normally and still clear when the buff ends; they just never tick back down while it is active, so strafing does not bleed them away.
        bool   buff_stack_probe        = false;// d3hack-custom: TEMPORARY. Logs every write to the buff STACK counters (BUFF_ICON_COUNT0..11, 0x2FF..0x30A) with the buff's power SNO, the value and the caller, on BOTH the int and float setters. Finds where a stacking set buff (Gears of Dreadlands Momentum) is granted. This hooks a hot function -- turn it OFF when done.
        bool   rift_reward_probe       = false;// d3hack-custom: log every TIERED_LOOT_RUN_* attribute read (0x570..0x59F) with its value and the CALLER, so the attribute Urshi's gem-upgrade count actually comes from can be identified by observation instead of by a name that looks right.
        int    empowered_gem_upgrades  = 0;    // d3hack-custom: TOTAL gem-upgrade attempts an EMPOWERED greater rift grants (0 = stock). Applied as a FLOOR, never taking attempts away, and gated on the game's own empowerment flag (w25 at 0x77BC80) rather than on the bonus being non-zero -- the bonus is empowered PLUS deathless, so the old test also fired on any ordinary rift you finished without dying.
        bool   gem_upgrade_always      = false;
        bool   gem_upgrade_speed       = true;
        bool   gem_upgrade_lvl150      = false;
        bool   equip_multi_legendary   = true;
        bool   super_god_mode          = false;
        bool   extra_gr_orbs_elites    = false;
        int    extra_gr_orbs_count     = 20;   // d3hack-custom: orbs per elite kill (upstream hardcoded 999)
        int    max_greater_rift_level  = 0;    // d3hack-custom: 0 = stock 150; up to 511
        int    xp_multiplier           = 1;    // d3hack-custom: rounded down to a power of 2, max 2^30
        bool   gem_uncapped            = true; // d3hack-custom: remove the gem rank ceiling entirely
        int    gem_max_level           = 150;  // d3hack-custom: jump-to target, only when gem_uncapped is false
        int    xp_gr_bonus             = 1;    // d3hack-custom: extra XP factor at/above xp_gr_bonus_min_gr
        int    xp_gr_bonus_min_gr      = 151;  // d3hack-custom: displayed GR at which that factor starts
        int    gem_floor_min_gr        = 151;  // d3hack-custom: displayed GR at which the upgrade-chance floor starts
        int    gem_floor_percent       = 10;   // d3hack-custom: minimum upgrade chance at/above that GR
        int    cube_augment_gem_rank   = 10000;// d3hack-custom: rank ceiling Caldesann's Despair records (stock 150)
        int    altar_crc_ashes         = 55;   // d3hack-custom: Primordial Ashes replacing the altar's Challenge Rift Cache (0 = stock)
        std::string disabled_monster_affixes = "Juggernaut, Wormhole, Shielding"; // d3hack-custom: comma-separated elite/champion affix names whose selection weight is zeroed, so they never roll. Empty = stock.
        std::string preferred_rift_maps = "";  // d3hack-custom: maps you LIKE. When a floor is substituted the replacement is drawn from this list first (if its floats are cached); otherwise it falls back to the whole eligible set. The counterpart to BannedRiftMaps -- it biases toward a map without forcing every floor to be it. Empty = no preference.
        std::string allowed_rift_maps = "";     // d3hack-custom: WHITELIST of rift tileset names. If non-empty, any map NOT named here is treated as disallowed and will be substituted. This is the direct way to say "I only want boneyards" -- one entry is a perfectly valid config. Empty = disabled, use BannedRiftMaps instead.
        bool        rift_map_substitute = false;// d3hack-custom: actually PERFORM the substitution in the rift floor plan. Off by default: every previous substitution attempt trapped the player in an unleavable floor, and this one is new. Read the [d3hack-plan] dump before trusting it.
        std::string banned_rift_maps = "";      // d3hack-custom: comma-separated rift tileset names that will never be selected for a rift floor. One entry per map SNO, so small and large variants of the same map ban independently. See rift-maps.txt for the full list of 164 names. Empty = stock.
        bool   prefer_low_fog          = false;// d3hack-custom: pick a LowFog weather for rift floors when one exists for that tileset. Weather assets are named <tileset>_<colour>_<HiFog|LowFog>_<Bright|Dark> and the fog is what draws the seam at high ViewDolly -- the stock lists are 223 HiFog to 60 LowFog, which is why it shows up so often. Same map, same colour family, less fog. Falls back to the game's own choice for the few tilesets with no LowFog variant.
        bool   map_name_overlay        = true; // d3hack-custom: name each rift floor's map on screen as you enter it, using the combat-log line. The names match rift-maps.txt, so a map you dislike can go straight into BannedRiftMaps.
        std::string map_density_overrides = ""; // d3hack-custom: per-map monster density, "name=multiplier" separated by commas, e.g. "px_lr_tileset_exterior_battlefieldsdrlg_large=10". Overrides GreaterRiftDensityMultiplier on those maps only -- the big open tilesets can look bare at a global setting that suits corridors. Names are the same ones BannedRiftMaps uses; see rift-maps.txt.
        bool   elite_event_probe      = false;// d3hack-custom: log ELITE_ENGAGED / ENGAGED_RARE_TIME / LAST_ACD_KILLED_TIME attribute writes. Finds the events behind D3 PC's 'engaged/killed a pack' combat log lines.
        int    camera_observer         = 0;    // d3hack-custom: GlobalSNO of the Observer (camera) applied on world entry. 0 = stock. 0x40F CONSOLE_WIDE, 0x40E CONSOLE_ZOOMED, 0x40D WIDE, 0x40C ZOOMED, 0x409 CONSOLE_DEFAULT.
        bool   camera_observer_dump    = false;// d3hack-custom: log the asset name of every Observer GlobalSNO 0x407..0x422 once, so the ids above can be matched to real cameras.
        float  camera_zoom             = 0.0f; // d3hack-custom: main-view zoom MULTIPLIER on the camera eye offset. 0 = off, 1.0 = stock, 1.5 = 50% further out, 5.0 = very far. Was clamped to -1..1 from an earlier theory, which silently made every setting a no-op. The game clamps to this range itself; 0 = stock. Positive/negative is which way out -- try 1.0 first.
        bool   combat_log              = false;// d3hack-custom: D3-PC-style combat log in the overlay -- one line per elite pack engaged. Needs [gui] Enabled = true (Visible can stay false).
        bool   camera_observer_probe    = false;// d3hack-custom: log every Observer (camera) GlobalSNO the game resolves, once each. Names the live gameplay camera.
        int    camera_observer_override = 0;    // d3hack-custom: GlobalSNO id used in place of OBSERVER_DEFAULT/CONSOLE_DEFAULT for the main view (0 = off). 0x40F = CONSOLE_WIDE, 0x40E = CONSOLE_ZOOMED.
        bool   camera_observer_scan     = false;// d3hack-custom: scan memory for loaded Observer (camera) ASSETS and log each one. Every .obs asset is 108 bytes starting with the magic 0xDEADBEEF + type 57; +0x3C is the camera distance and +0x40 the eye offset vec3, with |V0| == distance. Decoded from the PC archives via the DiIiS parser in this repo's sibling tree.
        int    camera_observer_id       = 0;    // d3hack-custom: SNO id of the single Observer asset CameraZoom should scale (0 = auto: any isometric asset with x==y and distance 40..250). Read the ids off the CameraObserverScan log.
        bool   camera_eye_write         = false;// d3hack-custom: the OLD per-frame camera-eye write. Proven to land but the game restores the offset every frame, and repeated scaling ran away toward infinity. Superseded by the asset patch; kept off unless deliberately revisiting it.
        bool   camera_trap             = false;// d3hack-custom: page-protect the located camera eye read-only so the game's own per-frame write FAULTS, naming the function that builds the camera. Value scanning is exhausted -- the offset, its distance and its reach are computed at runtime and stored nowhere. This crashes the game on purpose; the crash dump is the result.
        float  view_dolly             = 0.0f; // d3hack-custom: pull the camera back along its own view axis, in world units, by patching the view matrix as it is BUILT (0x29B754). The projection/eye writes all lose a per-frame race because the buffers are transient; this changes the value at the point it is computed, so there is nothing to race. ~84 = one camera length back. Negative pushes in.
        bool   view_dolly_shadows     = false;// d3hack-custom: move the shadow/light cameras by the SAME world delta as the render camera. Without it their volumes stay sized for the un-dollied view and a hard seam appears where their coverage ends. Applied only while the isometric render camera is live, so menus (which never build one) are untouched.
        float  camera_dist_scale      = 0.0f; // d3hack-custom: THE camera zoom. Scales the eye's offset from its look-at target at the SOURCE -- SetCameraEyeTarget, 0x29B4D0, which writes the global camera the renderer, the culling frustum and input unprojection all read through GetCameraEye/GetCameraTarget. 0 = off, 1.5 = 50% further out. Unlike ViewDolly this keeps every consumer in agreement, so no edge pop-in and no input fighting.
        bool   camera_dist_log        = false;// d3hack-custom: log the eye/target pairs passing through the camera setter.
        int    camera_dist_caller     = 0;    // d3hack-custom: module-relative return address of the ONE call site whose eye should be scaled. 0x29B4D0 is linker-folded code shared by camera and colour setters, so the call site is the only thing that identifies it. Read it off the [d3hack-eye] caller census.
        bool   view_dolly_log         = false;// d3hack-custom: log the first few view matrices the builder produces, so the main camera can be told apart from shadow/reflection passes.
        float  view_dolly_zmin        = -140.0f;// d3hack-custom: only view matrices whose translation z falls between ZMin and ZMax get dollied. The builder serves every camera in the engine (world, shadow cascade, light, UI portrait), so this picks which one. Read the z values off the [d3hack-dolly] census.
        float  view_dolly_zmax        = -60.0f; // d3hack-custom: see ViewDollyZMin.
        bool   power_formula_probe     = false;// d3hack-custom: log every distinct (power SNO, Script Formula N) -> value the game evaluates. How you find the number behind a skill.
        bool   power_random_probe      = false;// d3hack-custom: log which powers roll the script RNG, and what they roll. Finds proc chances that are not Script Formulas.
        bool   big_number_suffixes     = false;// d3hack-custom: keep abbreviating past the game's top tier. Stock stops at T and then just grows the mantissa -- 12345T, 123456T -- so a quadrillion is unreadable. This continues the SAME rule the game uses (roll to the next tier once the mantissa reaches 10000) with Q, Qi, Sx, Sp, Oc, No, Dc. Nothing below the game's own ceiling is touched.
        bool   number_format_probe     = false;// d3hack-custom: TEMPORARY. Log distinct strings passing through the three font-draw entry points that look like large numbers, plus which register carried them. The game abbreviates only up to T -- there is no quadrillion suffix anywhere in the binary -- so this establishes what it actually renders once a value passes 999T, which decides whether the fix is a suffix table or a whole reformat. Font draw is EXTREMELY hot; leave it off unless you are looking.
        bool   rift_spawn_probe        = false;// d3hack-custom: TEMPORARY. While standing on a rift floor, log every DISTINCT caller of the actor-spawn funnel at 0x86E2E0 plus a running call count. 0x94BCAC turned out to be a non-rift spawner, so the path that populates a Greater Rift floor is unknown; this makes it name itself. A zero count is a real answer -- it means rift monsters do not come through that funnel at all.
        bool   world_gen_probe         = false;// d3hack-custom: log the world SNO on every world entry, plus every distinct small-span RandomInt roll made DURING world generation. A "pick one of N" roll is how a Vision of Enmity chooses its per-level monster type -- including the all-treasure-goblin level.
        int    power_random_bias_sno   = 0;    // d3hack-custom: power SNO whose script RNG rolls are biased (0 = off). 488544 is Vision of Enmity.
        int    power_random_bias_pct   = 100;  // d3hack-custom: multiplier on that power's rolls, percent. BELOW 100 makes `roll < chance` pass more often -- 25 is roughly 4x the procs.
        bool   power_record_probe      = false;// d3hack-custom: retired -- dumps the script-context record at 0x8A02D0. It holds no power SNO; kept only so the dead end is not re-walked.
        int    power_formula_sno       = 0;    // d3hack-custom: power SNO whose Script Formula is scaled (0 = off). Read it out of the probe log.
        std::string power_formula_index_list = ""; // d3hack-custom: comma-separated Script Formula indices to scale. Overrides PowerFormulaScaleIndex when set. Lets every radius-looking formula of a power move at once.
        int    power_formula_index     = -1;   // d3hack-custom: which Script Formula of that power to scale (-1 = all of them)
        int    power_formula_percent   = 100;  // d3hack-custom: scale applied to it, in percent
        bool   wells_as_pools          = false;// d3hack-custom: every spawned Health Well becomes a Pool of Reflection instead -- actor SNO 138989 rewritten to 373463 in the level-area spawn data
        bool   well_spawn_probe        = false;// d3hack-custom: name every actor SNO the level-area populator places, once each. Use it to find well variants the swap misses.
        int    gr_density_multiplier   = 1;    // d3hack-custom: spawn groups per level area INSIDE A RIFT (1 = stock). Applies to Greater and Nephalem rifts alike -- what is testable at world-generation time is "this floor was given a rift tileset", not which kind of rift it belongs to. Outside a rift, WorldDensityMultiplier is used instead. A MapDensityOverrides entry beats both.
        int    rift_density_small      = 0;    // d3hack-custom: rift-floor density for SMALL maps (0 = use GreaterRiftDensityMultiplier). Size is read off the map name, so it covers every small map including ones added later. A MapDensityOverrides entry still beats it.
        int    rift_density_normal     = 0;    // d3hack-custom: rift-floor density for NORMAL maps -- the ones with no size suffix, including the _gg greater-rift variants (0 = use GreaterRiftDensityMultiplier).
        int    rift_density_large      = 0;    // d3hack-custom: rift-floor density for LARGE maps, _large and _extralarge (0 = use GreaterRiftDensityMultiplier). A big open map spreads the same spawn count over several times the ground, which is why it wants a higher number than a corridor.
        int    world_density_multiplier = 1;   // d3hack-custom: spawn groups per level area OUTSIDE a rift -- town, the open world, bounties (1 = stock). Split from the rift value because a number that makes a rift floor worth clearing turns the open world into a slideshow, and vice versa.
        bool   gr_density_rifts_only   = true; // d3hack-custom: apply the density multiplier only inside a rift, leaving town and the open world at stock. Tests the assigned rift tileset, not the rift TIER -- the tier is not set yet when worldgen runs, which is why this setting used to switch density off everywhere instead of restricting it.
        bool   rift_level_dump         = false;// d3hack-custom: dump the TieredLootRunLevels asset through GBAssetGet -- the GR ladder AllGBIDsOfType cannot see. Hunting the Orek's Dream weight.
        bool   rift_table_dump         = false;// d3hack-custom: log GB_LOOTRUN_QUEST_TIERS (0x27) and GB_TIERED_LOOT_RUN_LEVELS (0x31) once. Where a curated-rift weight would live.
        bool   monster_affix_dump      = false;// d3hack-custom: log every GB_MONSTER_AFFIXES record once, with a name->gbid roll call, to find the field that gates elite-affix selection
        bool   altar_dump_tables       = false;// d3hack-custom: log the whole Altar of Rites cost/requirement table once
        int    altar_item_cost_mode    = 1;    // d3hack-custom: pay Altar of Rites item sacrifices in crafting materials. 0 = stock, 1 = the picked seals only (4 Set Dungeon Pages, Staff of Herding), 2 = every item sacrifice
        int    altar_material_percent  = 100;  // d3hack-custom: scale on the material amounts the two modes above charge
        bool   item_socket_probe       = false;// d3hack-custom: dump ItemType records around the MaxSockets field
        int    any_slot_max_sockets    = 3;    // d3hack-custom: MaxSockets forced on every equippable item type (0 = stock)
        bool   socket_affix_any_slot   = false;// d3hack-custom: let the Sockets affixes roll on every equipment slot. Pointless while free sockets are on -- it only creates dead rolls.
        bool   socket_affix_suppress   = true; // d3hack-custom: remove the Sockets affixes from every item group, so they can never drop or be offered by the Mystic. Correct whenever FreeSocketsOnEveryItem > 0.
        bool   set_bonus_tier_shift    = true; // d3hack-custom: shift every set bonus down one tier -- the 4pc lands on the 2pc and the 6pc lands on the 4pc
        bool   set_bonus_dump          = false;// d3hack-custom: log every set bonus record (set gbid, pieces required) as the tier shift is worked out
        bool   set_bonus_any_weapon    = false;// d3hack-custom: apply a set bonus's damage multiplier regardless of the weapon equipped. Shadow's Mantle grants ITEM_POWER_PASSIVE[318386] = 60 (the 6000%) at all times, but folds it into MULTIPLICATIVE_DAMAGE_PERCENT_BONUS_FOR_SKILL[Impale] only when a melee weapon is held -- 61.0 with a melee weapon, 1.0 with a bow. This re-adds it.
        bool   damage_bonus_probe      = false;// d3hack-custom: log every write of the skill-damage-bonus attributes (0x4AC/0x4AD/0x5A7/0x5A8/0x5A9) with the skill SNO it is keyed to, the value and the caller. Diff a melee weapon against a bow to find where a set bonus's weapon gate lives.
        std::string sno_name_list      = "";   // d3hack-custom: comma-separated SNO numbers to resolve to names at world init, e.g. "484633, 423230". Powers are not in the generated sno.hpp, and naming them from a hook that runs on a worker thread is what crashed the first world-factory probe -- this does it at sInitializeWorld, where SNOToString is already known safe.
        std::string set_bonus_inspect  = "";   // d3hack-custom: dump the full GB_SET_ITEM_BONUSES record -- piece count and all 8 AttributeSpecifiers -- for every set whose name contains this substring, e.g. "Shadow". How you find which specifier carries a set bonus and what gates it. Empty = off.
        bool   rama_any_item           = false;// d3hack-custom: Ramaladni's Gift accepts any equippable target, boots included. DEFAULT false ON PURPOSE: it arms the six item-category flag writes that caused the Kadala icon bug, and a config.toml that fails to parse falls back to these defaults. Matches the live config, so nothing changes in practice.
        int    free_sockets            = 3;    // d3hack-custom: sockets every worn item has for free, costing no affix slot (0 = off)
        int    pool_xp_percent         = 25;   // d3hack-custom: permanent XP bonus per Pool of Reflection touched, in percent (0 = off)
        int    pool_xp_cap             = 0;    // d3hack-custom: max pools counted, 0 = unlimited
        int    pool_xp_levels_per_grant = 25;  // d3hack-custom: most level-ups one XP grant may cause; the rest is carried, never dropped
        bool   xp_census              = false;// d3hack-custom: every 5s, log what the experience hook actually did -- calls, which early-exits were taken, raw vs delivered totals, the live carry and the last bound. Answers "is the multiplier not applying, or is it being metered?" without guessing.
        bool   shard_probe            = false;// d3hack-custom: log who calls ACD_ModifyCurrencyAmount for blood shards (currency type 1), with the caller's return address. The pickup refusal happens in the CALLER's cap check, not in the modify itself, and 37 call sites is too many to read -- so name the right one at runtime.
        bool   follower_no_aggro      = true; // d3hack-custom: monsters stop adding the Scoundrel and the Enchantress to their target lists. The Templar is left alone on purpose -- he is the one that is supposed to hold a pack. Attribute Cannot_Be_Added_To_AI_Target_List (0x2C5) rather than Untargetable (0x12B): this one only affects AI target selection, so the follower can still be clicked, healed, buffed and hit by anything already in flight.
        bool   infinite_shrine_buffs   = false;// d3hack-custom: shrine and pylon buffs never expire. Uses the game's own HAS_INFINITE_SHRINE_BUFFS attribute (0x553) rather than scaling each duration individually. Pylons are cleared when a new Greater Rift opens regardless, so this mostly matters in Nephalem rifts and the open world.
        int    shrine_duration_percent = 100;  // d3hack-custom: multiply shrine and pylon buff durations, percent. 600 = six times as long. MULTIPLIES what the game computed, so Flavor of Time and Gloves of Worship still do their work first -- that is why this exists rather than the InfiniteShrineBuffs switch above.
        bool   shrine_duration_probe   = false;// d3hack-custom: log every PowerBuffSetDuration call with its power id. How you find the PYLON power ids, which are not known yet -- the four shrine ones are.
        bool   camera_field_dump      = false;// d3hack-custom: one-shot dump of the camera/view object's float fields, from the view builder's caller (x19 at 0x29B590, untouched at function entry). Looking for a far clip plane: the seam that appears when the dolly pulls back would be geometry crossing it, and unlike the fog planes -- which are authored per level and reach the shader as scene_fogPlanes -- a clip plane is one float we can move.
        bool   partial_currency_pickup = true; // d3hack-custom: pick up as much of a blood shard pile as fits, PC-style, instead of refusing the whole pile. Stock code tests (current + pile) <= cap at 0x48C448 and bails to an item-placement fallback when it fails -- and blood shards have no inventory slot, so that fallback always ends in "You have no place to put that item". The clamp itself already exists downstream at 0x48F2B0; this only stops the gate from skipping it.
        bool   xp_scale_rested        = false;// d3hack-custom: also apply the pool/GR multipliers to the w3==0 grant. That path was skipped on the belief it was derived from the already-scaled amount, but measurement disproved it: 8,843,177,984 against a 41,849,104 kill, 15x LARGER than the scaled output on the same event. It carries 16 of every 22 large awards including ~1/3 of the GR completion, which is why the effective multiplier wanders between ~1x and 13.5x.
        int    xp_hook_mode            = 1;    // d3hack-custom: 0 = do not install the experience hook at all; 1 = install it but pass straight through; 2 = also read the hero's level and cap, still granting the stock amount; 3 = full. A bisect that needs no rebuild.
        bool   pool_touch_hook         = false;// d3hack-custom: hook the Pool of Reflection touch event directly instead of inferring it
        bool   pool_slot_probe         = false;// d3hack-custom: log the pool slot attributes as they change
        bool   pool_grant_hook         = true; // d3hack-custom: install the experience grant hook that applies the pool multiplier
        int    max_paragon_level       = 2000000000;// d3hack-custom: paragon level cap
        int    paragon_stat_cap        = 250;  // d3hack-custom: points allowed in a single paragon stat
        bool   paragon_no_reset        = true; // d3hack-custom: stop the game wiping a whole paragon category when a save is over the limit (which happens whenever ParagonStatCap is lowered). In-game respec still works -- only the two load-time fixup call sites are cut, not the reset function itself.
    } rare_cheats;

    struct {
        bool        active                    = true;
        bool        DisableAncientDrops       = false;
        bool        DisablePrimalAncientDrops = false;
        bool        DisableTormentDrops       = false;
        bool        DisableTormentCheck       = true;
        bool        SuppressGiftGeneration    = false;
        int         ForcedILevel              = 0;
        int         TieredLootRunLevel        = 0;
        int         AncientMinGRLevel         = 120;
        int         PrimalMinGRLevel          = 151;
        int         PrimalGuaranteedCount     = 3;
        std::string AncientRank               = "Normal";
        int         AncientRankValue          = 0;
    } loot_modifiers;

    struct {
        bool active                = false;
        bool buildlocker_watermark = false;
        bool ddm_labels            = true;
        bool fps_label             = true;
        bool var_res_label         = true;
    } overlays;

    struct {
        bool active                       = true;
        bool enable_crashes               = false;
        bool enable_debug_flags           = false;
        bool enable_error_traces          = true;
        bool enable_exception_handler     = true;
        bool enable_oe_notification_hook  = false;
        bool enable_pubfile_dump          = false;
        bool log_oe_notification_messages = false;
        bool tagnx                        = false;
    } debug;

    struct {
        bool        enabled                      = false;
        bool        visible                      = false;
        bool        allow_left_stick_passthrough = true;
        std::string language_override            = "";
    } gui;

    ResolutionHackConfig resolution_hack {};

    void ApplyTable(const toml::table &table);
};

extern PatchConfig global_config;

void LoadPatchConfig();
auto NormalizePatchConfig(const PatchConfig &config) -> PatchConfig;
auto LoadPatchConfigFromPath(const char *path, PatchConfig &out, std::string &error_out) -> bool;
auto SavePatchConfigToPath(const char *path, const PatchConfig &config, std::string &error_out) -> bool;
auto SavePatchConfig(const PatchConfig &config) -> bool;
