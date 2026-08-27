# Contributing to d3hack

**This file is written to be read by an AI coding assistant.** If you are a person who
wants to contribute and you work with Claude, ChatGPT, Copilot or similar, hand it this
file first. It explains not just how the project is laid out, but the specific mistakes
this codebase has already made — most of which are invisible until you have burned a
play session on them.

If you are a person reading it directly: everything here still applies, you will just
find it blunter than a normal README.

---

## 1. What this project actually is

d3hack is a **runtime hook mod** for **Diablo III: Eternal Collection on Nintendo
Switch**. It is C++ built with devkitA64, shipped as an `exefs/subsdk9` module using
[exlaunch](https://github.com/shadowninja108/exlaunch). It loads into the game process
and rewrites behaviour by installing hooks at fixed addresses in the game binary.

It is **not** a save editor, **not** a static binary patcher, and **not** a server
emulator. If a proposed change does not run inside the game process, it does not belong
here.

Most testing happens on the **Ryujinx emulator**. It also runs on real hardware via
Atmosphere, but almost nobody tests that path, so do not claim a change works there.

### The hard constraint: game build 2.7.6.90885

Every address in this repo is specific to that build. There is no symbol table and no
dynamic lookup for game functions — hooks are installed at **numeric offsets**.

A different game build silently means different offsets. The mod will still load, still
install its hooks, and then corrupt whatever now lives at those addresses. **There is no
version check that will save you.** If you are not on 2.7.6.90885, stop and say so
rather than debugging the symptoms.

You can confirm the build in the Ryujinx log header:

```
v2.7.6.90885 [01001b300b9be000] [64-bit]
```

---

## 2. Getting a build

### You do not install a toolchain

The build runs inside a Docker container (`devkitpro/devkita64`). You need Docker and
nothing else.

```sh
./build.sh              # build
./build.sh deploy       # build, then deploy ONLY if the build succeeded
./build.sh configure    # re-run cmake configure — REQUIRED after adding/removing a source file
./build.sh shell        # interactive shell in the build container
./build.sh stop         # remove the container
```

The container is persistent on purpose (a cold container cost ~9s per build on cache
misses alone). It is disposable — stop it whenever, nothing is stored inside it.

`configure` is not optional after adding a file. CMake's `CONFIGURE_DEPENDS` is
deliberately off because re-globbing cost 12.7s per build, so a new source file will
simply not be compiled until you configure.

### Deploying

`./deploy.sh` copies `build/work.nso` and `build/work.npdm` to **two** locations:

- `%APPDATA%/Ryujinx/mods/contents/01001b300b9be000/d3hack/exefs/` — what Ryujinx loads
- `%APPDATA%/Ryujinx/sdcard/atmosphere/contents/01001b300b9be000/exefs/` — real-hardware mirror

Deploying to only one of them silently runs the previous build. That has cost real
debugging time. Use `./build.sh deploy` rather than chaining commands yourself.

> **Never pipe the build through another command and chain deploy with `&&`.** A past
> session piped through `tail`, the shell saw `tail`'s exit status, a failed build
> "succeeded", and a stale binary shipped while the build-stamp header had already
> regenerated. `build.sh` now captures the build's own status specifically to prevent this.

### `-Werror` is on

Warnings are errors. Two that bite repeatedly:

- `%lld` on a `u64` is a hard `-Werror=format` failure. Use `%ld`.
- Unused variables in code you commented out.

---

## 3. Repository map

```
src/                          <- the git repo root
  source/program/
    main.cpp                  hook installation — the master switch for every feature
    config.hpp                the config STRUCT (fields + defaults + helpers)
    config.cpp                TOML read/write for some sections
    config_schema.cpp         schema-driven config: key aliases, accessors, GUI entries
    offsets.hpp               named SYMBOLS for hooks (sym_*)
    offsets_patch_only.inl    named PATCH POINTS for instruction rewrites (patch_*)
    d3/
      patches.cpp             instruction-level patches (MOVZ/MOVK/NOP rewrites)
      hooks/util.hpp          ~600KB — the bulk of the gameplay hooks
      hooks/resolution.hpp    display/resolution hooks
  examples/config/d3hack-nx/config.toml    shipped example config
  README.md                   upstream feature README
  README-fork.txt             the fork's user-facing feature docs

staging/                      <- packaging tree for releases (NOT in git)
re/                           <- Python reverse-engineering tooling (NOT in git)
HANDOFF.md                    <- ~210KB of internal findings (NOT in git)
```

`util.hpp` is far too large to read whole. Navigate it with `grep -n` and targeted
`sed -n 'START,ENDp'` reads. Do not attempt to load it into context in one piece.

---

## 4. The three systems you will touch

### 4.1 Hooks

Two macros, from exlaunch:

```cpp
HOOK_DEFINE_INLINE(MyHook) {
    static void Callback(exl::hook::InlineCtx *ctx) {
        // runs at ONE instruction; ctx->W[n] / ctx->X[n] are readable AND writable
    }
};

HOOK_DEFINE_TRAMPOLINE(MyHook) {
    static int Callback(void *self, int arg) {
        // wraps a whole function; call Orig(self, arg) to run the original
        return Orig(self, arg);
    }
};
```

Installed in `main.cpp`:

```cpp
MyHook::InstallAtOffset(0x816EAC);      // raw numeric offset
MyHook::InstallAtSymbol("sym_gfx_init"); // named entry from offsets.hpp
```

Prefer `InstallAtSymbol` with a named entry. Raw offsets scattered through the code are
how a build bump becomes unfixable.

### 4.2 Offset tables

Two separate tables, and they are not interchangeable:

- **`offsets.hpp`** — `sym_*` entries: *function or instruction addresses you hook.*
- **`offsets_patch_only.inl`** — `patch_*` entries: *instructions you rewrite in place.*

`patches.cpp` resolves a patch entry with `PatchTable("name")` and rewrites it:

```cpp
auto jest = patch::RandomAccessPatcher();
jest.Patch<ins::Movz>(PatchTable("patch_resolution_targets_01_movz"), reg::X8, outW);
jest.Patch<ins::Movk>(PatchTable("patch_resolution_targets_02_movk"), reg::X8, outH, ins::ShiftValue_32);
```

Always comment a patch entry with the instruction it is replacing:

```cpp
{util::ModuleIndex::Main, 0x0E7858, "patch_resolution_targets_01_movz"},  // MOVZ | X8 | output width
```

That comment is the only record of what the original instruction was. Without it, the
next person cannot tell a correct patch from a wrong one.

### 4.3 Config

A setting passes through **three** places. Miss one and it silently does nothing.

1. **`config.hpp`** — the field and its default:
   ```cpp
   bool my_feature = false;
   ```

2. **`config_schema.cpp`** — three additions:
   - a key-alias array (`kKeysMyFeature`)
   - a getter and a setter (`GetMyFeature` / `SetMyFeature`)
   - an entry in `k_entries` — **and bump the `std::array<Entry, N>` size**

3. **`examples/config/d3hack-nx/config.toml`** — so users can discover it.

Some older sections are parsed by hand in `config.cpp` instead. Follow whichever pattern
the surrounding section already uses; do not convert one to the other as a side effect
of an unrelated change.

Clamp and validate in the **setter**. An out-of-range value should fall back to the
stock default, not be passed through to the game.

---

## 5. Adding a feature: the checklist

1. Add the config field (`config.hpp`).
2. Wire it through `config_schema.cpp` — array, accessors, entry, size bump.
3. Add it to the example `config.toml`.
4. Write the hook.
5. Install it in `main.cpp` — **and read rule 1 below before you write the install condition.**
6. `./build.sh deploy`
7. Test in-game, reading the Ryujinx log.
8. Document it in `README-fork.txt` (both the index line and a detail section).

---

## 6. The rules

Every one of these was learned by losing a session to it. They are not style
preferences.

### Rule 1 — A config key that turns a feature ON must appear in that feature's install condition

**This project has hit this bug six separate times.** It is the single most common
failure mode here.

The shape: a feature's hook is installed only when some *unrelated* diagnostic flag is
true. The feature's own config key does nothing, and the user reports "it doesn't work"
about a feature that was never installed.

```cpp
// WRONG — the feature is gated on a probe flag that has nothing to do with it
if (global_config.rare_cheats.world_gen_probe)
    RiftMapBanHook::InstallAtOffset(0x816E98);

// RIGHT
if (global_config.rare_cheats.world_gen_probe || !global_config.rare_cheats.banned_rift_maps.empty())
    RiftMapBanHook::InstallAtOffset(0x816E98);
```

Real cases: rift map bans gated behind `WorldGenProbe`; `MapDensityOverrides` gated
behind a dead helper; the combat log gated behind `EliteEventProbe`.

**Before submitting, grep every config key you touched and confirm it appears in its own
install condition.** This takes seconds and it is the highest-value check in the project.

### Rule 2 — Every probe needs an install confirmation AND a liveness counter

If a probe logs nothing, that must not be ambiguous between "never fired" and "never
installed". Four probes on one feature once had no install line, so four play sessions
produced results that proved nothing.

```cpp
PRINT("[d3hack-myprobe] installed at %08X", 0x69EDB8)   // it exists
// ...and inside the hook:
static int s_nHits = 0;
if (++s_nHits % 1000 == 1)
    PRINT("[d3hack-myprobe] alive, %d hits", s_nHits)   // it runs
```

Silence only means something when you have proven the thing was installed.

### Rule 3 — There are FOUR attribute accessors, not two

`{ACD, FastAttrib} × {Int, Float}`. A float attribute watched only through the int
getters produces 80,000+ reads of real traffic and zero hits, which reads exactly like
"this attribute is never touched."

Cover all four before concluding an attribute is absent.

### Rule 4 — Never verify a write by reading it back through the same accessor

That confirms your own write and nothing else. Verify **downstream**, where the game
consumes the value.

### Rule 5 — Additive overrides only

Buffs are **floors over the stock roll**, gated by a threshold. Never replace a roll
outright. Replacing means a player with a better natural result gets a worse one.

```cpp
if (nRolled < nFloor) nRolled = nFloor;   // right
nRolled = nValue;                          // wrong
```

### Rule 6 — One variable per test run

A play session is an expensive experiment. Two changes at once means an ambiguous
result and a wasted session.

### Rule 7 — Instrument before patching

Log what the game actually does before changing it. Several features here were "fixed"
three or four times against a theory that measurement then disproved in one run.

### Rule 8 — Line endings

Many files in this tree are **CRLF**, some are mixed. A naive string replacement finds
zero matches and reports success. Check first:

```sh
grep -cU $'\r' path/to/file
```

When scripting an edit, validate **every** anchor before writing **any** file — a script
that exits partway leaves earlier edits unsaved.

### Rule 9 — A hook that never fires can still corrupt

Installation itself rewrites instructions. "It never logged anything, so it can't be the
cause" is false. Remove a suspect hook entirely before ruling it out.

### Rule 10 — Ship the learned state

A feature that learns something at runtime and caches it is a no-op for everyone except
the developer who already has the cache. If a feature depends on learned data, that data
has to ship with the release.

### Rule 11 — Crash truth is in the Ryujinx log

Not the game's own debug output. The Ryujinx log directory (`<Ryujinx>/Logs`) is the
authority, and **one log file can span several launches** — check timestamps and make
sure you are reading the run you think you are.

---

## 7. Testing

1. `./build.sh deploy`
2. Launch the game in Ryujinx.
3. Read the log.

`PRINT(...)` output goes to the Ryujinx log. It is enabled by default
(`D3HACK_ENABLE_DBGPRINT` defaults to `1` in `source/program/d3/setting.hpp`).

Prefix your log lines so they are greppable: `[d3hack-<feature>]`.

There are no automated tests and no CI. The build compiling is not evidence that a
feature works — every gating bug in Rule 1 compiled perfectly.

---

## 8. What not to do

- **Do not bump the game version.** Offsets are build-specific; a version bump is a
  reverse-engineering project, not a config change.
- **Do not add static `pchtxt`/IPS patches alongside the mod.** Third-party ultrawide
  and FPS patches rewrite some of the same instructions this mod does, and whichever
  applies last silently wins. Where such an offset is genuinely useful, implement it
  natively and credit the source.
- **Do not reformat files you are not changing.** These files are CRLF and huge; a
  whitespace pass makes the real diff unreviewable.
- **Do not delete a disproven probe without a note.** Some code is deliberately kept
  behind `if (false)` as a documented negative result, so the next person does not retry
  it. If you remove one, say why in the commit.
- **Do not claim a result you did not measure.** This project's notes distinguish
  "verified by reading the code" from "suspected", and so should you.

---

## 9. Submitting

- Branch from the current working branch, not an old tag.
- One feature per PR.
- In the description, state **what you actually tested and on what** — "built clean" is
  not a test result, and neither is "should work".
- If you changed a config key, confirm in the description that it appears in its own
  install condition (Rule 1).
- Update `README-fork.txt` for anything user-visible: the index line **and** a detail
  section.

If you are unsure whether something belongs here, open an issue describing the
behaviour you want before writing the hook. Finding the right offset is usually most of
the work, and someone may already know where it is.
