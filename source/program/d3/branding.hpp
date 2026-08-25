#pragma once

// d3hack-custom: branding macros ONLY. This header must never gain an #include.
//
// These used to live in d3/setting.hpp, which pulls symbols/common.hpp, logging.hpp and
// <cstdio>. build_stamp.cpp needs nothing but these string macros, yet including setting.hpp
// cost it 9.9 s to compile -- and because the build-stamp header regenerates whenever a source
// changes, that 9.9 s sat on the critical path of EVERY build, ahead of the link. Measured with
// -fsyntax-only against the real compile flags.
//
// setting.hpp includes this file, so every existing user of these macros is unaffected.
// D3HACK_FULLVER stays in setting.hpp because it needs D3CLIENT_VER from build_info.hpp.

#define D3HACK_VER    "D3Hack v3.8"
#define D3HACK_AUTHOR "Jester"
#define D3HACK_MODDER "Slothic"
#define CRLF          "\n"
// d3hack-custom: ONE tagline drives every surface that shows branding -- the autosave
// splash (build_stamp.cpp), the in-game watermark (D3HACK_FULLVER, drawn through
// g_tSignature in patches.cpp) and the lobby toast in _lobby.hpp. They were three
// separate strings all wired to D3HACK_WEB, so changing the branding used to mean
// editing each of them and missing one.
#define D3HACK_TAGLINE                                                                \
    "D3Hack - Created by " D3HACK_AUTHOR " - Modified and Improved by " D3HACK_MODDER \
    " - Wudijo is a dirty botter."
// Signature::szName is a fixed char[64], so that one surface gets a short form.
#define D3HACK_TAGLINE_SHORT D3HACK_VER " - " D3HACK_AUTHOR " / " D3HACK_MODDER
#define D3HACK_DESC          D3HACK_TAGLINE
