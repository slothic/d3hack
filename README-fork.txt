===========================================================================
 D3HACK v3.16
 A gameplay mod for Diablo III: Eternal Collection (Nintendo Switch)
===========================================================================

Needs game build 2.7.6.90885 exactly. Install = copy two folders (section 2).
Every feature below is a line in config.toml. Change any value, relaunch,
done. Settings are read once at startup.

    config.toml lives at   ...\sdcard\config\d3hack-nx\config.toml


===========================================================================
 WHAT THIS FORK ADDS
===========================================================================

D3Hack is not mine. This is a fork -- the original mod (resolution options,
the cheat menu, season spoofing, the loot-modifier block and the toggles at
the bottom of this list) is someone else's work and I have not touched most
of it.

Everything in THIS section is what the fork adds on top. If you want to know
what the mod did before I got to it, see "ALREADY IN D3HACK" further down.

Details for every entry are in section 1. The right column is the setting
that controls it.

 ITEMS AND SETS
   3 free sockets on every equippable item ....... FreeSocketsOnEveryItem = 3
   Sockets affix removed from all roll tables .... SocketAffixSuppress = true
   Set bonuses shift down a tier (4pc at 2) ...... SetBonusTierShift = true
   Kanai augment gem rank cap 150 -> 10000 ....... LegendaryGemUncapped = true
   Set damage bonuses ignore the weapon requirement  SetBonusAnyWeapon = true

 MONSTERS
   Monster density by map size ................... RiftDensitySmall / Normal / Large
   Monster density, everywhere else .............. WorldDensityMultiplier = 3
   Juggernaut, Wormhole and Shielding never roll .. DisabledMonsterAffixes
   (any elite affix can be disabled by name)

 EXPERIENCE
   Experience multiplier ......................... ExperienceMultiplier = 32
   ...applied to the rested half of a kill too ... XpScaleRested = true
   Greater Rift completion bonus multiplier ...... ExperienceMultiplierHighGR = 2
   Every health well is a Pool of Reflection ..... HealthWellsAsPoolsOfReflection = true
   Pools give a permanent, stacking XP bonus ..... PoolOfReflectionXpPercent = 25

 LOOT
   Ancients drop above a GR level ................ AncientMinGRLevel = 120
   Primals drop above a GR level ................. PrimalMinGRLevel = 151
   Guaranteed primals per rift ................... PrimalGuaranteedCount = 3
   (the game's own roll still runs and is only ever raised, so drops
    below those levels are untouched)

 GREATER RIFTS
   Rift level cap 150 -> 500 ..................... MaxGreaterRiftLevel = 500
   (511 is the game's own ceiling: it computes the maximum from the size
    of its rift-level table and clamps that to 511. Rows past the stock
    end are extrapolated from the designed per-level ratios, so HP, damage
    and XP keep climbing on the same curve.)
   Ban rift maps you do not want to play ......... RiftMapSubstitute = true
   Steer replacements toward maps you like ....... PreferredRiftMaps = "..."
   Empowered rifts grant more gem upgrades ....... EmpoweredGemUpgrades = 10

 PARAGON
   Paragon level cap 20000 -> 2 billion .......... MaxParagonLevel = 2000000000
   Points allowed in a single stat ............... ParagonStatCap = 250
   Over-limit categories are not wiped on load ... ParagonNoReset = true
   Main stat per paragon point (stock 5) ......... ParagonMainStatPerPoint = 30.0
   Vitality per paragon point (stock 5) .......... ParagonVitalityPerPoint = 30.0
   Flat main stat bonus, billions are fine ....... StatBonusMainStat = 32000000000
   Flat Vitality bonus, same .................... StatBonusVitality = 32000000000

 ALTAR OF RITES
   Eight seals take crafting materials instead
   of rare items, and the uber-organ seal takes
   materials instead of Infernal Machine organs .. AltarItemCostMode = 1
   Challenge Rift Cache seal takes ashes ......... AltarChallengeRiftCacheAshes = 55

 SEASONAL
   Visions of Enmity about 5x more often ......... PowerRandomBiasPercent = 20

 SKILLS
   Condemn's Vacuum pull 15 -> 120 yards ......... PowerFormulaScalePercent = 800
   (a general per-skill lever, not a Condemn special case)

 CAMERA AND UI
   Pull the camera back .......................... ViewDolly = 35.0
   Combat log: what you pulled, and what died .... CombatLog = true
   Gears of Dreadlands keeps its Momentum ........ MomentumAutoFireEvery = 4
   Ultrawide / any aspect ratio .................. AspectRatio = 2.3889

 FOLLOWERS
   Monsters ignore Scoundrel and Enchantress ..... FollowerNoAggro = true
   (the Templar is deliberately untouched)

 CURRENCY
   Pick up part of a blood shard pile ............ PartialCurrencyPickup = true


---------------------------------------------------------------------------
 ALREADY IN D3HACK  (not mine -- original mod)
---------------------------------------------------------------------------

   Resolution and docked/handheld options ........ [resolution_hack]
   Season spoofing, online toggle ................ [seasons]
   All season themes on at once, any season ...... [events] section
   Legacy of Nightmares toggle ................... LegacyOfNightmares
   Socket gems into any slot ..................... SocketGemsToAnySlot = true
   Instant cube, crafting and enchanting ......... InstantCubeAndCraftsAndEnchants = true
   Menu overlay .................................. [gui] Enabled
   God mode, no cooldowns, guaranteed legendaries,
   movement and attack speed, auto pickup,
   unlock all difficulties, and the rest ......... [rare_cheats], off by default

   The fork changes DEFAULTS for some of these -- Legacy of Nightmares is
   off, the menu overlay is off -- and section 1 says so where it matters.
   The features themselves are the original author's.


===========================================================================
 1. FEATURES
===========================================================================

Each entry: what it does, the setting, and what restores the original.


---------------------------------------------------------------------------
 SOCKETS AND ITEMS
---------------------------------------------------------------------------

3 FREE SOCKETS ON EVERY EQUIPPABLE ITEM
    Rings, amulets, helms, gloves, boots, shoulders, chest, legs, bracers,
    belts, weapons, shields. They cost no affix slot -- the item still rolls
    all its normal stats.
        FreeSocketsOnEveryItem = 3        0 = off
        AnySlotMaxSockets      = 3        must be >= the above

    The tooltip may still print "Sockets (1)" on an item whose base data
    says so. Ignore it -- the icon and the actual socket count are correct.

THE "SOCKETS" AFFIX IS REMOVED FROM ALL ROLL TABLES
    Since every item already has sockets, rolling the affix would waste a
    stat slot -- and the Mystic would offer it as an enchant, burning a real
    stat to grant something you already have.
        SocketAffixSuppress = true        false = stock

    Guaranteed sockets printed on set/legendary items (Blackthorne's, etc.)
    are part of the item and are left alone. They cost you nothing.

---------------------------------------------------------------------------
 SETS
---------------------------------------------------------------------------
GEARS OF DREADLANDS KEEPS ITS MOMENTUM
    Strafe auto-fires your last primary, but that shot never grants
    Momentum -- so the stacks bleed away while you do the one thing the
    set exists for.
        MomentumAutoFireEvery = 4         0 = off
    Blocks the decay and adds a stack every Nth write, so the number tunes
    how fast the stacks climb rather than whether they survive. Capped at
    the set's own 20. Stacks ratchet up and hold while you strafe.

    Two older settings are still there and are not needed if the above is
    on. MomentumNoDecay froze the count; MomentumDurationPercent stretches
    the buff timer, which is worth having if the buff itself expires on
    you rather than the stacks draining.


SET BONUSES SHIFT DOWN ONE TIER
    The 4-piece bonus lands on 2 pieces, the 6-piece lands on 4. Two-tier
    sets (Aughild's, Captain Crimson's, Cain's, Born's) collapse to 2/2.
    Single-tier sets are untouched, and nothing ever drops below its own
    lowest tier -- a 2pc never becomes a 1pc.
        SetBonusTierShift = true          false = stock

    Ring of Royal Grandeur still stacks on top, so it now buys the top tier
    at three pieces.


---------------------------------------------------------------------------
 MONSTERS AND DENSITY
---------------------------------------------------------------------------

MONSTER DENSITY x3
    More packs are placed, of the normal shape -- elite and champion ratios
    stay as designed, there are simply more of them.

    Rift floors are set by MAP SIZE, because a corridor map and an open field
    want very different numbers:
        RiftDensitySmall  = 5
        RiftDensityNormal = 10      maps with no size suffix
        RiftDensityLarge  = 20      _large and _extralarge
    Size is read off the map name, so all 164 maps are covered -- 56 small,
    68 normal, 40 large -- and any map a later patch adds is covered too.

    Everything that is not a rift floor:
        WorldDensityMultiplier = 3      town, open world, bounties

    And the fallbacks:
        GreaterRiftDensityMultiplier = 3     used when the size value is 0
        GreaterRiftDensityRiftsOnly  = false leave rifts alone / everything else
    All of them take 1..1000, 1 being stock.

    Precedence, most specific first:
        1. a MapDensityOverrides entry naming the map
        2. the size class for that map
        3. GreaterRiftDensityMultiplier
    An override REPLACES the others rather than stacking, so set it to the
    number you actually want. The log says which rule applied -- PER-MAP,
    small, normal, large or rift -- next to the map name.

    "Rift floor" means Greater AND Nephalem rifts. What the game can be asked
    while a floor is being built is "was this floor given a rift tileset", not
    which kind of rift owns it.

    RiftsOnly = true confines the multiplier to rift floors and leaves town
    and the open world at stock. It works properly from v3.10; before that it
    tested the rift TIER, which is not set yet when the world is built, so
    switching it on turned density off everywhere instead.

    This applies EVERYWHERE, not only in Greater Rifts. Rift-only is not
    possible: monsters are placed before the game has decided the rift tier,
    so at that moment there is nothing to test against.
    If a very dense area ever hitches, drop the multiplier to 2.

JUGGERNAUT, WORMHOLE AND SHIELDING DISABLED
    None of the three roll on elite packs any more.
        DisabledMonsterAffixes = "Juggernaut, Wormhole, Shielding"
                                                  "" = stock

    Juggernaut is the unstoppable charge, Wormhole is the one that teleports
    you around the arena, and Shielding is the periodic immunity that makes
    a pack take several times as long to kill for no added danger.

    It is a list -- add or remove any elite affix by name, separated by
    commas. Use the internal names exactly as spelled here (several are one
    word with no space):

        ArcaneEnchanted   Desecrator     Electrified    Fast
        FireChains        Frozen         FrozenPulse    Horde
        Illusionist       Jailer         Juggernaut     Knockback
        Molten            Mortar         Nightmarish    Orbiter
        Plagued           PoisonEnchanted               ReflectsDamage
        Shielding         Teleporter     Thunderstorm   Vampiric
        Vortex            Waller         Wormhole

    Also valid: Avenger, Missile Dampening, Health Link, Extra Health, and
    the ColdImmune / FireImmune / LightningImmune / PoisonImmune set.

    A name that does not resolve is reported in the log rather than silently
    ignored, so a typo is visible.


---------------------------------------------------------------------------
 EXPERIENCE AND POOLS OF REFLECTION
---------------------------------------------------------------------------

THE EXPERIENCE MULTIPLIER NOW APPLIES TO ALL OF A KILL
    Rested experience -- the bonus pool the game hands out alongside normal
    kill XP -- was never being scaled. It carries most of the large awards,
    including about a third of a Greater Rift completion, so the rate you
    actually got wandered between 1x and the number you set depending on what
    you were killing.
        XpScaleRested = true              false = stock

    If your experience gain used to feel inconsistent, this was why.

EVERY HEALTH WELL IS A POOL OF REFLECTION
    Not a well that also grants a pool -- the game genuinely spawns a pool,
    with the correct model, sound and interaction.
        HealthWellsAsPoolsOfReflection = true     false = stock

POOLS GIVE A PERMANENT, STACKING XP BONUS
    +25% per pool touched, saved across sessions, instead of a temporary
    buff the Altar makes pointless.
        PoolOfReflectionXpPercent = 25    0 = off
        PoolOfReflectionMaxCount  = 0     0 = unlimited
        PoolOfReflectionGrantHook = true

    CAREFUL: these two features multiply each other. Wells are far more
    common than pools ever were, so the bonus compounds fast. If it runs
    away, PoolOfReflectionMaxCount is your ceiling.

    Delete pools_u0.txt to reset the count to zero.

GREATER RIFT COMPLETION BONUS
    2x experience at GR151 and above, including Orek's end-of-rift payout.
        ExperienceMultiplierHighGR         = 2    1 = off
        ExperienceMultiplierHighGRMinLevel = 151
        ExperienceHookMode                 = 3   0 off, 1 idle,
                                                 2 GR bonus only, 3 all


---------------------------------------------------------------------------
 SKILLS
---------------------------------------------------------------------------

CONDEMN'S VACUUM PULL REACHES 120 YARDS  (stock is 15)
        PowerFormulaScaleSno       = 266627   0 = off
        PowerFormulaScaleIndexList = "16"
        PowerFormulaScalePercent   = 800      100 = stock

    This is a general lever, not a Condemn special case. ScaleSno picks the
    skill, ScaleIndexList picks which of that skill's numbers to scale.
    Point it at a different skill and you scale that instead.


---------------------------------------------------------------------------
 VISIONS OF ENMITY
---------------------------------------------------------------------------

ABOUT FIVE TIMES MORE OFTEN
        PowerRandomBiasSno     = 488544   0 = off
        PowerRandomBiasPercent = 20       100 = stock

    How it actually works, since no guide states this correctly: a Vision is
    NOT a flat percentage. The game rolls 1-in-N on every kill and counts N
    down from 500, so a Vision arrives at a uniformly random kill within
    500 -- about 250 on average. This setting shrinks that window.
        100 -> within 500 kills, avg 250   (stock)
         20 -> within 100 kills, avg 50    (shipped)
         10 -> within  50 kills, avg 25


---------------------------------------------------------------------------
 ALTAR OF RITES
---------------------------------------------------------------------------

THE UBER ORGAN SEAL COSTS SHARDS AND MATERIALS INSTEAD
    The seal that wants one of each Infernal Machine organ asks for blood
    shards and crafting materials instead, so the Altar does not stall behind
    an uber farm.
        AltarItemCostMode = 1             0 = stock

    Mode 2 also strips other seals' item requirements. That is more than most
    people want and it does not fix this seal anyway -- use 1.

EIGHT SEALS NO LONGER WANT A RARE ITEM
    They take Blood Shards or crafting materials instead, so nothing gates
    you behind a drop you cannot farm.
        AltarItemCostMode    = 1      0 = stock, 2 = convert every item seal
        AltarMaterialPercent = 100    scales all of the amounts below

        Reaper's Wraps ...........    600 Blood Shards
        Ring of Royal Grandeur ...    900   (the Ruby is still wanted)
        Petrified Scream .........  1,200
        Ancient Hellfire Amulet ..  1,500
        4 Set Dungeon Pages ......    125 souls / 500 crystals /
                                    1,000 shards / 250 parts
        Ancient Puzzle Ring ......  1,500   (bounty mats still wanted)
        Any Augmented Weapon .....  1,800
        Staff of Herding .........  2,100

CHALLENGE RIFT CACHE SEAL TAKES PRIMORDIAL ASHES
    So you can finish the altar without waiting on weekly challenge rifts.
        AltarChallengeRiftCacheAshes = 55    0 = stock


---------------------------------------------------------------------------
 LOOT: ANCIENTS AND PRIMALS
---------------------------------------------------------------------------

BELOW THE THRESHOLDS THE GAME'S OWN DROP RATES ARE UNTOUCHED
    Normal Nephalem rifts, the campaign, bounties, and Greater Rifts under
    AncientMinGRLevel roll ancients and primals exactly as the stock game
    does. The mod does not touch those drops at all.

HIGH GREATER RIFTS RAISE THE RANK, THEY DO NOT REPLACE THE ROLL
    Above the thresholds the game still rolls normally and the result is
    only ever raised, so a primal the game rolled by itself is still a
    primal and does not use up the guaranteed count.
        AncientMinGRLevel     = 120     every legendary at least Ancient
        PrimalMinGRLevel      = 151     first few legendaries of a rift
        PrimalGuaranteedCount = 3       how many of them, per rift
        AncientRank           = Normal  floor on EVERY drop everywhere;
                                        Normal means no override

    Set either MinGRLevel to 0 to switch that half off. To suppress
    ancients outright use DisableAncientDrops -- setting AncientRank to
    Normal does not do that, it just declines to raise anything.


---------------------------------------------------------------------------
 PARAGON PER-POINT VALUES
---------------------------------------------------------------------------

HOW MUCH ONE PARAGON POINT IS WORTH
    Stock is 5 main stat and 5 Vitality per point. These set it to anything.
        ParagonMainStatPerPoint = 30.0    0 = leave stock 5 alone
        ParagonVitalityPerPoint = 30.0    0 = leave stock 5 alone

    Main stat means Strength, Dexterity and Intelligence together -- the game
    keeps a separate record per class, and all seven are covered. Resistance
    All is also 5 per point but is NOT a core stat, so it is left alone.

    The value is written straight into the bonus's own script formula, so it is
    the real number the game multiplies by. Nothing is corrected afterwards and
    nothing recomputes it per frame.

WHY 650 IS THE MAXIMUM
    Not taste -- arithmetic. The most points that can ever go into one core stat
    is 100,000 x 33 = 3,300,000 (33 being the largest ParagonStatCap scale), and
    3,300,000 x 650 is INT32_MAX. So 650 is the largest per-point value that
    cannot overflow a 32-bit integer anywhere downstream, at ANY ParagonStatCap.
    Values above it are clamped and the log says so.

    Separately, main stat is a 32-bit FLOAT, which holds exact whole numbers only
    up to 16,777,216. Past that it still works, it just counts in steps -- 2 at
    33 million, 4 at 67 million, and so on. That is a precision limit, not a
    failure, so it is not enforced. If you want every number exact, keep
    points x per-point under 16.7 million.

WHAT ACTUALLY LIMITS A HIGH-PARAGON CHARACTER
    Not this setting -- the SPEND cap. Each core stat accepts 100,000 points,
    scaled by ParagonStatCap, and the non-core categories are hard-stopped at
    200 points each no matter how much paragon you have. Everything else spills
    into Core. So at 2 billion paragon only a few million points are spendable,
    and this setting is what turns those into a number worth having.

    ParagonStatCap only produces 50 x (1 + 2^k), so the values that actually do
    something are 150, 250, 450, 850 and 1650. Anything else silently becomes
    250.

PARAGON BONUS INSPECTOR
        ParagonBonusInspect = true        false = off, and it is off by default

    Dumps all 28 paragon bonus records at world entry -- name, both caps, and
    every attribute specifier with its decoded per-point value. Read-only.
    This is how the values above were found, and it is the thing to turn on if a
    future patch moves them.

---------------------------------------------------------------------------
 HUGE STATS
---------------------------------------------------------------------------

A FLAT MAIN STAT / VITALITY BONUS THAT CAN BE ENORMOUS
        StatBonusMainStat = 32000000000    0 = off
        StatBonusVitality = 32000000000    0 = off

    Main stat covers Strength, Dexterity and Intelligence -- whichever your class
    actually uses is the one you will see move.

    Measured in game: 32,000,000,000 gave a Dexterity total of 32,000,180,224.
    No overflow, no clamp, no negative.

WHY THIS IS SAFE WHEN A BIG BASE STAT IS NOT
    The game keeps your BASE stats and your BONUS stats in different places, and
    only the base ones pass through a piece of code that cannot handle numbers
    above about 2.1 billion. Past that it does not merely clamp -- it rolls over
    to a NEGATIVE number and writes that back onto your character.

    This setting writes to the BONUS slot, which that code never touches, and the
    game's own total already adds the bonus in. So the number reaches your damage
    by the game's own route and never goes near the part that breaks.

YOUR SAVE FILE IS NEVER TOUCHED
    The bonus slot is rebuilt from scratch every time you load -- it reads 0 on a
    fresh load even if it was huge when you saved. Verified by saving, reloading,
    and watching it come back empty.

    So the number lives in this config file, not in your character. It is
    re-applied on every world entry, including after a save and reload. Set both
    values to 0 and your character is exactly as it was -- there is nothing to
    undo and nothing left behind.

    It also never stacks. Each application raises the slot TO the configured value
    rather than adding to it, and if the game ever puts something bigger there,
    that is left alone.

HOW BIG CAN IT GO
    Far bigger than you need. The stat is stored as a 32-bit float, which reaches
    about 3.4e38 -- more range than a 64-bit whole number.

    What it does NOT keep past about 16.7 million is exact whole numbers. Above
    that it counts in steps, which is why 32,000,000,000 reads back as
    32,000,180,224. That is rounding, not an error, and it is invisible in play.

STATBONUSPROBE (diagnostics, off by default)
        StatBonusProbe = true
        StatBonusProbeValue = 32000000000    -1 = read only, write nothing

    Writes a test value into all four bonus slots and logs base, bonus and total
    for each, so you can see exactly what the game did with it. This is how the
    feature above was proven. Set the value to -1 to observe without writing,
    which is what you want if you are checking whether something survived a save.

---------------------------------------------------------------------------
 PARAGON AND GEMS
---------------------------------------------------------------------------

        MaxParagonLevel          = 2000000000
        ParagonStatCap           = 250        per-stat spend limit (stock 50)

    ParagonStatCap raises two limits that the game keeps in separate places:
    the per-stat spend limit, and the per-category point POOL. Stock 50 also
    means a non-Core category is only ever granted 4 x 50 = 200 points; at
    250 that pool becomes 1000.
        ParagonNoReset           = true
        MaxGreaterRiftLevel      = 500
        LegendaryGemMaxLevel     = 10000
        LegendaryGemUncapped     = true
        CubeAugmentGemRankCap    = 10000      stock 150
        LegendaryGemChanceFloorPercent  = 10  upgrade floor at GR151+
        LegendaryGemChanceFloorMinGRLevel = 151


---------------------------------------------------------------------------
 SEASONAL THEMES
---------------------------------------------------------------------------

ALL OF THEM AT ONCE, IN ANY SEASON
    The [events] section switches the season themes on independently of
    which season you are actually in -- Ethereals, Soul Shards, Sanctified
    items, Shadow Clones, the 4th Kanai's Cube slot, Visions of Enmity, the
    Altar of Rites, Kanai powers, Triune's Will, Royal Grandeur and the
    rest. One line each; set any to false to drop it.

LEGACY OF NIGHTMARES IS OFF
        LegacyOfNightmares = false      true = the old behaviour

    It grants a flat bonus for every Ancient item equipped and does not
    scale with gem rank. The Legacy of Dreams gem covers the same ground
    and does scale, and this mod uncaps gem rank to 10000, so the buff is
    redundant here.

    Careful: [seasons] SeasonNumber also feeds an event map that can force
    a theme ON regardless of what [events] says -- the map is OR'd over
    your settings, so it can add but never remove. Season 17 is the Legacy
    of Nightmares one. At the shipped SeasonNumber = 37 nothing is forced.


---------------------------------------------------------------------------
 CAMERA
---------------------------------------------------------------------------

PULL THE CAMERA BACK
    A wider view of the fight. This is a distance along the view direction,
    not a zoom multiplier, so it behaves the same at every camera angle.
        ViewDolly = 35.0                  0 = stock

    35 is roughly twice the stock view. Past about 80 the world starts
    running out before the screen does -- you will see the edge of what the
    game bothered to draw. That edge is a limit in the game, not a bug in
    the setting; back the number off until it is out of frame.

        ViewDollyShadows = false          true = shadows follow the camera

    Shadows are left on the stock camera on purpose. Moving them costs
    frames and changes nothing visible at sane dolly values.

    CameraZoom is the older knob and is superseded. Leave it at 0.


---------------------------------------------------------------------------
 COMBAT LOG
---------------------------------------------------------------------------

WHAT YOU PULLED, AND WHAT DIED
    A line along the bottom of the screen when you engage a champion pack,
    a rare pack or a Rift Guardian, and another when it dies.
        CombatLog = true                  false = off

    Needs [gui] Enabled = true. Visible can stay false -- that gives you the
    HUD without the menu and without the overlay taking your input.

    Map names do NOT appear here. They have their own small panel under the
    minimap, because the map you are standing in is state you want to glance
    at, not an event that scrolls away. Set MapNameOverlay = false to hide it.

    The monster name is coloured by rarity -- blue champion, yellow rare,
    pink guardian -- and dimmer once it is dead. Elite affixes are listed
    after the name and coloured by what they actually do to you:
        red     ground damage you have to move out of
        cyan    crowd control
        grey    defensive, a slower kill rather than a dangerous one
        violet  mobility and swarm

    Lines hold for three minutes and then fade.


---------------------------------------------------------------------------
 FOLLOWERS
---------------------------------------------------------------------------

MONSTERS IGNORE THE SCOUNDREL AND THE ENCHANTRESS
    They stop being added to monster target lists, so packs you are dragging
    to the next group do not peel off onto a follower who cannot die anyway.
        FollowerNoAggro = true            false = stock

    The Templar is deliberately untouched -- he is the one that is supposed
    to hold a pack. The two affected followers can still be clicked, healed
    and buffed; only monster targeting changes.


---------------------------------------------------------------------------
 DISPLAY
---------------------------------------------------------------------------

ULTRAWIDE AND OTHER ASPECT RATIOS
    The resolution hack could always change the output RESOLUTION, but the
    aspect ratio itself was a hardcoded 16:9 constant -- so asking for a wider
    output just stretched the same 16:9 image. This makes it a setting.
        [resolution_hack]
        AspectRatio = 1.7778              16:9 -- stock, and the default
        AspectRatio = 2.3704              2560x1080   (sold as 21:9)
        AspectRatio = 2.3889              3440x1440   (sold as 21:9)
        AspectRatio = 3.5556              5120x1440   (sold as 32:9)

    The value is your target width divided by its height, to 4 decimals.
    The marketing names are not exact and not interchangeable -- both of the
    panels sold as 21:9 above want a different number. Use the division, not
    the label.

    Two things have to move together, and both are handled for you: the docked
    display mode, and the HUD aspect constant. Patching only the first is what
    gives you an ultrawide backbuffer with a 16:9 HUD stretched across it.

    Needs [resolution_hack] SectionEnabled = true, and a restart. Set
    OutputTarget to the vertical resolution you want -- the width follows from
    the ratio, so 1440 at 3.5556 is 5120x1440.

    At 16:9 the aspect code is skipped entirely and the game is patched exactly
    as it always was, so the default build is byte-for-byte unchanged.

    A value outside 1.0-4.0 falls back to 16:9 rather than asking for a display
    mode the compositor will refuse. A black screen is the worst failure here,
    because the setting that caused it lives on the SD card, not in the in-game
    menu you would need to reach to undo it.

    Wide ratios raise memory use. If the game dies on load at a large output,
    raise the emulator's DRAM layout to 8GB.

    Credit: the HUD aspect instruction offsets came from Fl4sh9174's
    Switch-Emulator-Ultrawide-FPS-Mods pchtxt collection.
    Do NOT install those pchtxt files alongside d3hack. They write the same two
    instructions this setting writes, and whichever applies last silently wins.
    Most of that pack also targets 2.7.7.92380, which is not this game build.

---------------------------------------------------------------------------
 BLOOD SHARDS
---------------------------------------------------------------------------

PICK UP PART OF A PILE
    Stock behaviour is all-or-nothing: near the cap, a pile that does not fit
    entirely is refused, and the game reports "You have no place to put that
    item" even with an empty inventory -- the message is about a fallback
    path, not about bag space.
        PartialCurrencyPickup = true      false = stock

    With this on you take exactly what fits and the remainder stays on the
    ground at its reduced count, the way the PC version works. Nothing is
    lost and nothing is duplicated. At the cap it declines cleanly.

---------------------------------------------------------------------------
 QUALITY OF LIFE
---------------------------------------------------------------------------

        InstantCubeAndCraftsAndEnchants = true
        SocketGemsToAnySlot             = true

MENU OVERLAY IS OFF
    If you want the in-game settings menu, set Enabled = true in the [gui]
    section near the top of config.toml.

There is more in config.toml switched OFF by default -- god mode, no
cooldowns, guaranteed legendaries, movement and attack speed multipliers,
auto pickup, unlock all difficulties. Turn on whatever you like.


===========================================================================
 2. INSTALLING   (two folders, two different places)
===========================================================================

FIRST: BACK UP YOUR SAVE. This mod does not write to your save, but you are
changing how the game behaves.
    Ryujinx: %AppData%\Ryujinx\bis\user\save\
    Yuzu:    %AppData%\yuzu\nand\user\save\

You need game build 2.7.6.90885. This is not optional -- the mod edits the
game at exact memory addresses and those move between versions. On any other
build it refuses to start and the game closes or hangs at the loading screen.

    If your copy is NEWER, force 2.7.6 by layering these three files from a
    2.7.6 dump over your install with LayeredFS:
        exefs/main                     md5 c3d386af84779a9b6b74b3a3988193d2
        romfs/CPKs/CoreCommon.cpk      md5 618b6dffc4cf7c4da98ca47529a906c8
        romfs/CPKs/ServerCommon.cpk    md5 de80fce3642d9cde15147af544877983

The zip has two top-level folders and they go to DIFFERENT places:
    01001B300B9BE000\   <- the mod
    sdcard\             <- the settings

Press Win+R, type %AppData%, Enter. That is the folder below.

--- the mod ---
RYUJINX (Windows)   copy `01001B300B9BE000` into %AppData%\Ryujinx\mods\contents\
      ...\mods\contents\01001B300B9BE000\d3hack\exefs\subsdk9
      ...\mods\contents\01001B300B9BE000\d3hack\exefs\main.npdm
      ...\mods\contents\01001B300B9BE000\d3hack\romfs\d3gui\...
    Create mods\contents\ if missing, or right-click the game >
    "Open Mods Directory".
RYUJINX (macOS)     ~/Library/Application Support/Ryujinx/mods/contents/
RYUJINX (Linux)     ~/.config/Ryujinx/mods/contents/
YUZU                copy the `d3hack` folder into
                    %AppData%\yuzu\load\01001B300B9BE000\
REAL SWITCH         sdmc:/atmosphere/contents/01001B300B9BE000/exefs/subsdk9
                    (no `d3hack` folder -- exefs and romfs sit directly
                    under the title ID)

--- the settings ---
Copy the `config` folder from inside `sdcard\` to:
    RYUJINX (Windows)   %AppData%\Ryujinx\sdcard\
    RYUJINX (macOS)     ~/Library/Application Support/Ryujinx/sdcard/
    YUZU                %AppData%\yuzu\sdmc\
    REAL SWITCH         the root of your SD card
You should end up with:
    ...\sdcard\config\d3hack-nx\config.toml
    ...\sdcard\config\d3hack-nx\rift_data\...

Skip this and the mod still loads, but with its own defaults instead of the
tuned ones, and challenge rifts won't have their data.

--- turn mods on ---
RYUJINX: right-click the game > "Manage Mods", make sure d3hack is ticked.
YUZU:    right-click the game > Properties > Add-Ons, tick d3hack.

NOTE FOR RYUJINX USERS: renaming a mod folder does NOT disable it. Ryujinx
loads every subfolder it finds regardless of name. To disable, move the
folder out of mods\contents\ entirely.


===========================================================================
 3. CHECKING IT WORKED
===========================================================================

Load a character and look at any equippable item. It should show 3 socket
holes on the icon, whatever the slot.

For proof, open   ...\sdcard\config\d3hack-nx\D3Debug.txt   and look for:

    [D3Hack|exlaunch] Compiled at ...
    [d3hack-custom] set bonus tiers: ... verified stuck
    [d3hack-custom] altar: 8 seals converted
    [d3hack-custom] monster affix "Juggernaut" ... weight 100 -> 0
    [d3hack-well] SPAWN well 138989 -> pool 373463

If that file does not exist at all, the mod is not loading -- section 5.


===========================================================================
 4. CHANGING SETTINGS
===========================================================================

Open config.toml with any text editor. Values are `Name = value`, with
true/false for switches and plain numbers for the rest. Close the game
before editing and restart afterwards.

IMPORTANT: the game REWRITES this file on every launch, so any comments you
add are deleted. Keep notes in a separate file.

If you break it, delete it -- a fresh one with defaults is written next
launch.


===========================================================================
 5. IF SOMETHING GOES WRONG
===========================================================================

GAME CLOSES INSTANTLY OR HANGS ON A BLACK SCREEN
    Almost always the wrong game version -- see section 2. Check D3Debug.txt
    for "Unsupported build".

D3DEBUG.TXT DOESN'T EXIST
    The mod isn't loading. Check `subsdk9` is inside an `exefs` folder, the
    title ID folder is spelled exactly 01001B300B9BE000, and mods are
    enabled for the game in your emulator.

CRASHES AFTER ENTERING A WORLD
    Change ONE value, relaunch, test:
        GreaterRiftDensityMultiplier   = 1
        HealthWellsAsPoolsOfReflection = false
        PoolOfReflectionGrantHook      = false
        ExperienceHookMode             = 0
        PoolOfReflectionXpPercent      = 0
        SocketAffixSuppress            = false

TOO MANY MONSTERS / SLOWDOWN
    Lower RiftDensitySmall / Normal / Large, or set them to 0 and let
    GreaterRiftDensityMultiplier decide. 1 everywhere is stock. A rift
    floor stops multiplying after 4000 extra spawns and says so in the
    log, so a very high number concentrates monsters at the start of the
    floor rather than spreading them.

XP MULTIPLIER LOOKS WRONG
    Delete pools_u0.txt to reset the pool count.

WHERE THE LOGS ARE
    ...\sdcard\config\d3hack-nx\D3Debug.txt
        The mod's own log. It can lose the last few lines when the game
        dies, so don't assume the crash was at the last line you see.
    Ryujinx: <Ryujinx folder>\Logs\*.log
        Better for crashes -- unbuffered, full register dump, and one file
        covers several launches.

    EnableExceptionHandler already ships true, so crashes get a proper
    register dump without you doing anything.


===========================================================================
 6. WHAT CHANGED IN v3.16
===========================================================================

NEW FEATURES
  * A flat main stat / Vitality bonus that can be enormous. Values in the
    billions work: 32,000,000,000 was measured giving a Dexterity total of
    32,000,180,224, with no overflow and no clamp.
        StatBonusMainStat = 32000000000
        StatBonusVitality = 32000000000
    It writes to the BONUS attribute slots, not your base stats. The code
    that recalculates base stats cannot handle numbers past about 2.1
    billion -- it rolls over to a NEGATIVE value and writes that onto your
    character. The bonus slots are never touched by it, and the game's own
    total already adds them in, so the number reaches you by the game's own
    route. See section 1 for the full explanation.

  * Your save file is never modified by it. The bonus slot is rebuilt from
    scratch on every load, so the value lives in this config and is
    re-applied each time you enter a world. Set it to 0 and your character
    is exactly as it was.

DIAGNOSTICS
  * StatBonusProbe / StatBonusProbeValue -- write a test value into the four
    bonus slots and log base, bonus and total for each. -1 observes without
    writing, for checking whether something survived a save.
  * DamageCaptureSamples = 1000 -- capture N damage numbers and print count,
    mean and the ten largest. Run it with a setting off and again with it on;
    the ratio is the measured effect rather than an impression.
  * ParagonBonusInspect, DamageRouteProbe, ParagonPointsProbe -- read-only
    dumps used to find where paragon and damage values actually live.

REMOVED BEFORE RELEASE
  * DamageMultiplier and DamagePerMainStat existed briefly and were cut after
    measurement. The game ALREADY scales damage by main stat (1 point = +1%
    damage), so both double-counted a rule it applies itself. Three 1000-
    sample captures showed paragon alone moving damage 3.3-4.2x with them
    switched off. ParagonMainStatPerPoint is the correct and complete dial.

FROM v3.15
  * How much main stat and Vitality one paragon point grants, stock 5. This
    applies to every point you have ALREADY spent, not just new ones --
    measured at 29,072 Dexterity on stock and 173,347 at 30/point.
        ParagonMainStatPerPoint = 30.0
        ParagonVitalityPerPoint = 30.0
    Because damage scales off main stat, this raises stat, armour, health
    and damage together.
  * Ultrawide and any other aspect ratio. Both the display mode and the HUD
    aspect constant are patched -- doing only the first stretches the HUD.
        AspectRatio = 2.3889      3440x1440.  1.7778 = 16:9, the default
  * FIXED: roughly half the mod depended on an unrelated XP setting. A
    310-line block containing 62 hook installs was gated on
    ExperienceMultiplierHighGR > 1, which DEFAULTS to 1. Rift map bans,
    density, free sockets, follower no-aggro, Momentum, the big-number
    suffixes and every probe worked only because the shipped config happens
    to set 2. Setting it to 1 silently disabled all of them.
  * CONTRIBUTING.md added, written to be handed to an AI assistant.

FROM v3.14
  * Momentum documented. The feature shipped in v3.13 but appeared nowhere in
    this README, so nobody reading the docs could discover the setting.

FROM v3.13
  * Gears of Dreadlands keeps its Momentum, confirmed working. Stacks ratchet
    up and hold while strafing instead of bleeding away.
        MomentumAutoFireEvery = 8
    Fourth approach and the first that works: it holds the stack against
    decay and then builds, at the point the game stores the counter, rather
    than trying to convince the game a shot was fired.
  * The 55 maps a Greater Rift can actually roll are now marked. The list had
    164 entries, most of which are Nephalem-only -- which is why preferring
    them appeared to do nothing.

FROM v3.12
  * Damage numbers keep abbreviating past T: Q, Qi, Sx, Sp, Oc, No, Dc, Ud.
        BigNumberSuffixes = true
  * Show only the top N magnitudes. Once you hit for T the M numbers are
    noise, so anything more than N-1 tiers below your biggest hit is hidden.
        DamageNumberTiers = 2
  * FIXED: six of the shipped rift-map float values were wrong -- four
    Nephalem values on Greater Rift maps and two maps not in the GR pool at
    all. Replaced with the game's own data, read out of the romfs.

FROM v3.11
  * Rift density by map size, so small maps are not swamped and large ones
    are not empty.
        RiftDensitySmall = 5    RiftDensityNormal = 10    RiftDensityLarge = 20
  * FIXED: CombatLog = true was inert. The kill feed rode on a diagnostic
    flag (EliteEventProbe), so with the shipped combination neither the
    engaged line nor the kill line could ever appear. Map names were also
    crowding the feed and no longer are.

FROM v3.10
  * Greater Rift density actually works. The multiplier raised since v3.3
    was on a NON-RIFT spawner -- 4 density calls against 21,000 real spawns
    on rift floors, which is why 3x, 10x and 100x were indistinguishable in
    a Greater Rift. Found by walking the live stack at the spawn funnel;
    static reading could not find it because the path runs through a
    function pointer.
  * Rift and world density are separate settings now, and the ceiling is
    raised from 20.
        GreaterRiftDensityMultiplier = 3     WorldDensityMultiplier = 1
  * FIXED: per-map overrides were capped at 64, below the global values they
    are supposed to beat.

FROM v3.9
  * Gears of Dreadlands Momentum, and rift map bans that work off the machine
    they were developed on.
  * FIXED: GreaterRiftDensityRiftsOnly turned density OFF everywhere instead
    of restricting it to rifts. The gate tested a value that is always -1 at
    that point. What hid it is worth recording: the skip message advised
    turning the setting off, which made the symptom disappear and let the bug
    survive -- a workaround printed by the code that needs fixing reads as
    documentation.
  * FIXED: MapDensityOverrides had never applied, and the log could not show
    it. Same dead gate.
  * FIXED: rift map substitution was dead unless WorldGenProbe was also on.
  * FIXED: EmpoweredGemUpgrades fired on ordinary rifts -- the gate was not
    an empowerment test.

FROM v3.8
  * Ban the rift maps you do not want to play. BannedRiftMaps has existed
    for a while but could not actually enforce anything -- every previous
    attempt swapped the map too late, after the floor had already been
    budgeted, and dropped you into a floor you could not walk out of. The
    substitution now happens in the rift's floor PLAN, before a single
    floor is built, and carries the map's own tile-budget values with it.
    That was the missing piece: swapping a tileset while leaving the old
    map's budget behind is what trapped people.
        RiftMapSubstitute = true
        BannedRiftMaps = "x1_lr_tileset_crypt, ..."

    Bans apply per map name, so a family's small and large variants are
    separate entries. rift-maps.txt beside config.toml lists all 164.

  * Steer what a banned floor turns into. Maps you like are drawn from
    first, and the choice varies by floor so a rift is not eight copies of
    the same map.
        PreferredRiftMaps = "x1_lr_tileset_exterior_boneyards"

    There is also a whitelist if you would rather name what you DO want:
    AllowedRiftMaps = "..." makes everything else a candidate for
    replacement. One entry is a perfectly valid setting.

    !! READ THIS BEFORE YOU WRITE A BIG BAN LIST !!

    Greater Rifts draw from a SMALLER set of maps than Nephalem rifts, and
    a map can only be substituted IN if it is one the Greater Rift engine
    actually rolls. The reason is that each floor carries a tile budget
    belonging to its map, and swapping a tileset while keeping the old
    map's budget is what used to drop people into floors they could not
    walk out of. Those budgets are learned from real rifts and kept in
    rift-map-gr-floats.txt beside this file; the mod refuses to swap in
    any map it has no budget for rather than guess one.

    That file ships with 42 maps already in it, so bans work from your
    first rift. It grows on its own as you play.

    What this means in practice:

      * ONLY 55 OF THE 164 MAPS EXIST IN GREATER RIFTS. The other 109 are
        Nephalem-rift tilesets and the Greater Rift engine never rolls
        them, so naming one in PreferredRiftMaps or AllowedRiftMaps cannot
        do anything -- the name resolves fine, it simply never comes up.
        Battlefields of Eternity, Festering Woods, the Highlands and the
        Wilderness are all in that group and are common things to ask for.

        rift-maps.txt marks every map GR or "nephalem only" in its last
        column. That list comes from the game's own rift table, not from
        observation, so it is exact.

      * MapDensityOverrides sets density per map, for the open tilesets
        that look bare at a multiplier tuned for corridors. It works from
        v3.10 -- before that it was gated on the rift tier, which is not set
        when the world is built, so no override ever applied. It replaces
        the global value rather than adding to it, so an override LOWER than
        GreaterRiftDensityMultiplier makes that map emptier, not fuller.

        Overrides only ever apply on rift floors, since they key off the
        floor's rift tileset. Naming a map the Greater Rift engine does not
        roll will do nothing there -- the log says PER-MAP when one applies
        and rift or world when the global value is used instead.

      * If you ban nearly everything, check the log once. D3Debug.txt
        beside config.toml says one of:

            substitution ready: N eligible, M usable now ...
            SUBSTITUTION NOT READY: N eligible, 0 with known floats ...
            SUBSTITUTION CANNOT WORK: every map is banned ...

        The line appears every time you load into a world, before you open
        a rift. If it says NOT READY, the maps you left eligible are ones
        the game has not rolled yet -- it also lists them by name.

      * These four settings belong to the [rare_cheats] section of
        config.toml. Sections are in alphabetical order, so pasting them
        at the BOTTOM of the file puts them under [seasons], where they
        are ignored without any error.

  * Empowered Greater Rifts grant more gem upgrade attempts. Stock is
    three from completing plus one for empowering; this sets the total.
        EmpoweredGemUpgrades = 10
    A normal rift is untouched -- empowering is what the setting keys off,
    so it still costs gold and still means something.

  * Set damage bonuses ignore their weapon requirement. The Shadow's
    Mantle 2-piece gives 6000% Impale damage only while a melee weapon is
    equipped, which rules out every Demon Hunter ethereal, since those are
    all bows and crossbows. The bonus is granted either way by the game --
    it is simply left out of the per-skill total when you hold a bow -- so
    it is now added back.
        SetBonusAnyWeapon = true

FIXED
  * The map name panel advances when you take the portal. It used to show
    the map you were standing in as "Next" and never update "Current",
    because the game pre-builds the next floor while you are still on the
    current one and the panel was keyed off that build rather than off
    your arrival.

  * Three separate rift-map substitutions were live at once. Only one was
    documented as enabled and the other two had no size or rift-type
    checks at all, so they could fire in Nephalem rifts and swap in any
    map from the table. All three are gone, replaced by the single
    plan-level substitution above.

FROM v3.7
  * Pull the camera back. A distance along the view direction rather than a
    zoom multiplier, so it behaves the same at every camera angle.
        ViewDolly = 35.0        roughly twice the stock view
    Past about 80 you start seeing the edge of what the game bothered to
    draw. That edge is a limit in the game, not a bug in the setting.

  * Combat log. A line when you engage a champion pack, rare pack or Rift
    Guardian, and another when it dies. Names coloured by rarity, elite
    affixes coloured by what they actually do to you -- red is ground damage
    to move out of, cyan is crowd control, grey is defensive, violet is
    mobility and swarm.
        CombatLog = true

  * Monsters ignore the Scoundrel and the Enchantress, so packs you are
    dragging to the next group stop peeling off onto a follower who cannot
    die anyway. The Templar is deliberately untouched -- he is the one that
    is supposed to hold a pack.
        FollowerNoAggro = true

  * Pick up part of a blood shard pile. Stock behaviour near the cap is
    all-or-nothing: a pile that does not fit entirely is refused outright,
    with a message about bag space that has nothing to do with bag space.
    Now you take exactly what fits and the remainder stays on the ground at
    its reduced count, the way the PC version works. Nothing is lost and
    nothing is duplicated; at the cap it declines cleanly.
        PartialCurrencyPickup = true

FIXED
  * The experience multiplier now applies to ALL of a kill. The game awards
    experience down two paths and only one was ever being scaled -- and the
    unscaled one turned out to be the bigger of the two, carrying 16 of
    every 22 large awards and about a third of the Greater Rift completion
    bonus. That is why the effective multiplier used to wander between
    roughly 1x and 13.5x depending on what you were killing. It is now
    uniform.
        XpScaleRested = true

    The grant cap was wrong in the same area: it was computed from the
    experience still owed for your current level rather than a full level's
    cost, so it collapsed toward zero as you approached a level-up and reset
    the instant you got one.

  * The Altar of Rites uber organ seal is payable again. Its four demon
    organs are currency costs, not item requirements, so the pass that
    rewrites seal costs never looked at them -- and switching
    AltarItemCostMode to 2 did not help, it only stripped other seals'
    requirements as collateral. Organ costs are now rewritten directly.

  * ParagonNoReset no longer blocks respeccing. It used to disable the
    reset function outright, which is also the function the in-game respec
    button calls -- so points went straight back where they were. It now
    only blocks the load-time fixup that WIPES an over-limit category,
    which is the thing the setting was always for. Respec works normally
    with it left on.

    This supersedes the upgrade warning in the v3.4 notes below: the
    "set it to false, relaunch, respec, set it back" dance is no longer
    needed.

  * Kadala and Kanai legendary probability, and the paragon level field,
    both fixed earlier in this line and carried forward.

DOCUMENTATION
  * This README now opens with a complete list of what this FORK adds, with
    the setting for each, followed by a separate list of what was already in
    D3Hack. The original mod is someone else's work and the two were not
    distinguished before.

FROM v3.6
  * Safety pass over the mod's own patches. Several experiments that had
    been disproven were still installed and still writing: two rift-map
    swaps, a hook on a floating-point instruction that had been recorded
    as removed but was only switched off, and two "diagnostics" that were
    quietly widening serializer fields and could drop you to the main menu
    on a kill. All removed.

  * Settings that disable a risky patch now default to safe in the CODE,
    not only in the shipped config. A config.toml that fails to parse
    falls back to code defaults, so a single bad line used to re-arm
    several of them at once.

  * Raised the mod's internal hook budget. It was one hook away from its
    ceiling, and going over it aborts at boot with a message that names an
    allocator rather than saying "too many hooks".

FROM v3.5
  * The Legacy of Nightmares community buff is now OFF by default. It is a
    flat bonus per Ancient item equipped with no rank scaling, while the
    Legacy of Dreams gem covers the same ground and does scale with gem
    rank -- which this mod uncaps to 10000. Set LegacyOfNightmares = true
    in [events] to put it back.
  * Seasonal themes documented in section 1 for the first time, including
    the SeasonNumber event map that can force a theme on regardless of
    your [events] settings.

  * No binary change from v3.4 -- same subsdk9. This release differs only
    in the shipped config.toml and the README.

FROM v3.4
  * Ancients and primals drop again outside high Greater Rifts. v3.3
    REPLACED the game's own ancient/primal roll rather than building on top
    of it, so with the shipped AncientRank = Normal every drop came out
    rank 0 unless the GR120+ escalation fired. Normal Nephalem rifts, the
    campaign, bounties and every GR below 120 produced no ancients and no
    primals at all. The stock roll now always runs and the mod only ever
    raises the result.

  * Paragon categories grant their full share of points. ParagonStatCap
    raised the per-stat spend limit but not the per-category point pool,
    which stayed at the stock 4 x 50 = 200 because the game reads that
    limit from a second place. You could put 250 into a single stat and
    still never be given more than 200 points to spend in that category --
    the surplus was quietly diverted into Core instead.

    !! UPGRADING AN EXISTING CHARACTER: Core stops receiving that diverted
    surplus, but whatever you already spent there stays spent, so you can
    be over budget in Core until you respec. ParagonNoReset = true BLOCKS
    respeccing -- set it to false, relaunch, respec, then set it back if
    you want it. A fresh character is unaffected.

FROM v3.3
  * Monster density x3 everywhere.
  * Visions of Enmity about 5x more often. (And the real mechanic finally
    pinned down -- it is a 500-kill pity counter, not a flat chance.)
  * Every health well is a Pool of Reflection.
  * Set bonuses shift down a tier: 4pc at 2 pieces, 6pc at 4.
  * Eight Altar of Rites seals take Blood Shards instead of rare items.
  * Juggernaut affix disabled, and any affix can be disabled by name.
  * Condemn's Vacuum pull reaches 120 yards instead of 15, via a general
    per-skill scaling lever.
  * Menu overlay off by default.

  * Diagnostic and probe settings removed from the shipped config. They were
    reverse-engineering tools and defaulted to off anyway.

FROM v3.2
  * Pools of Reflection give a permanent, stacking, saved XP multiplier.
  * 3 free sockets on every equippable item, costing no affix slot.
  * The Sockets affix removed from all roll tables.
  * Kanai's Cube augment gem rank cap raised from 150 to 10000.
  * Altar Challenge Rift Cache seal replaced with 55 Primordial Ashes.
  * Greater-rift completion bonus fixed -- Orek's payout, the biggest grant
    of a run, never actually received the multiplier before.
  * Paragon level field widening no longer depends on debug logging being
    on, which had been silently corrupting paragon past 32767.
