// Who built these programs, shown next to the version so a home-built copy
// can be told apart from an official release. Decided by the compiler:
// Visual Studio builds say "EDN - Built", the MinGW release builds say
// "Claude build". Override with -DPMP3_BUILDER_NAME="\"...\"" if needed.
#pragma once

#if !defined(PMP3_BUILDER_NAME)
#if defined(_MSC_VER)
#define PMP3_BUILDER_NAME "EDN - Built"
#else
#define PMP3_BUILDER_NAME "Claude build"
#endif
#endif
