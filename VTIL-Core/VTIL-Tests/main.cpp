#if defined(__linux__)
// on some glibc toolchains SIGSTKSZ is not a constant expression...
#define DOCTEST_CONFIG_NO_POSIX_SIGNALS
#endif

#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
#ifndef DOCTEST_BREAK_INTO_DEBUGGER
#define DOCTEST_BREAK_INTO_DEBUGGER() __builtin_debugtrap()
#endif
#endif

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"