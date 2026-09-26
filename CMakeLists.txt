cmake_minimum_required(VERSION 3.21)
project(Repentless VERSION 1.2.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# GEODE SDK DISCOVERY
# ---------------------------------------------------------------------------
if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK environment variable is not set.")
else()
    message(STATUS "Found Geode SDK: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# ---------------------------------------------------------------------------
# 1. SOURCE DEFINITIONS
#
# Translation units:
#   main.cpp             — settings cache + mod lifecycle (all platforms)
#   GameLoopHook.cpp     — PlayLayer consumer: queue drain + dt clamp + stats
#   DispatcherHook.cpp   — CCEGLView producer: touch capture (Android only)
#   ThreadOptimizer.cpp  — GL-thread CPU affinity + SCHED_FIFO (Android only)
#
# Headers (included, not compiled separately):
#   src/LockFreeQueue.hpp  — SPSC ring buffer (core data structure)
#   src/InputQueue.hpp     — InputEvent struct + g_inputQueue global
#   src/GlobalState.hpp    — g_inLevel flag (DispatcherHook ↔ GameLoopHook)
# ---------------------------------------------------------------------------
add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/GameLoopHook.cpp
    src/DispatcherHook.cpp
    src/ThreadOptimizer.cpp
)

# ---------------------------------------------------------------------------
# 2. PLATFORM-SPECIFIC SYSTEM LIBRARIES
# ---------------------------------------------------------------------------
if(WIN32)
    # timeBeginPeriod / timeEndPeriod
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm)
endif()

# ---------------------------------------------------------------------------
# 3. AGGRESSIVE COMPILER & LINKER FLAGS
#
# All flags are Clang/LLVM-specific.  The Android NDK ships Clang as its
# default compiler; MSVC path retained for Windows desktop builds.
# ---------------------------------------------------------------------------

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")

    # ── Global optimization flags (all platforms, Release only) ─────────────
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            -O3                         # Full optimization (inlining, vectorization)
            -fvisibility=hidden         # Strip unexported symbols → smaller .so
            -fvisibility-inlines-hidden # Inline functions → not exported
            -fno-exceptions             # Disable C++ exception tables (~5% size reduction)
            -fomit-frame-pointer        # Free up x29 as a general-purpose register
            -fstrict-aliasing           # Enables type-based alias analysis
            -fno-math-errno             # math.h functions don't set errno
            -fno-trapping-math          # Remove FP exception checks
            -freciprocal-math           # a/b → a * (1/b) where safe
            -ffp-contract=fast          # Fuse FP ops into FMAs
        >
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            -Wl,--gc-sections           # Dead-strip unreferenced sections
            -Wl,-s                      # Strip debug symbols from .so
            -Wl,-z,now                  # Resolve all PLT symbols at load time
            -Wl,-z,relro                # Mark relocation table read-only after init
            $<$<PLATFORM_ID:Android>:
                -Wl,--hash-style=gnu    # Smaller/faster hash table vs sysv
            >
        >
    )

    # ── Android 32-bit (arm-v7a) ─────────────────────────────────────────────
    if(GEODE_TARGET_PLATFORM STREQUAL "Android32")
        target_compile_options(${PROJECT_NAME} PRIVATE
            -march=armv7-a
            -mfpu=neon-vfpv4
            -mfloat-abi=softfp
            -mthumb
        )

    # ── Android 64-bit (arm64-v8a) ───────────────────────────────────────────
    elseif(GEODE_TARGET_PLATFORM STREQUAL "Android64")
        # ─────────────────────────────────────────────────────────────────────
        # -mcpu=cortex-a720
        # ─────────────────────────────────────────────────────────────────────
        # Sets the full ISA + micro-architectural tuning for Cortex-A720.
        # What this enables automatically:
        #   • ARMv9.2-A ISA (sve2, i8mm, bf16, dotprod, crc, crypto)
        #   • FEAT_LRCPC2 (rcpc2) — release-consistent PC-relative atomics
        #     → LDAPR instruction for cheaper acquire loads in hot loops
        #   • LSE (Large System Extensions, ARMv8.1) — LDADD, SWP, CASAL
        #     → atomic fetch-add / compare-swap as single instructions
        #   • Cortex-A720 pipeline scheduling (5-wide OOO, 128-bit SIMD)
        #
        # What this does NOT enable:
        #   • FEAT_LRCPC3 (rcpc3) — that is ARMv9.4 / Cortex-A725+.
        #     Using -march=armv9-a+rcpc3 CRASHES on SM8750 with SIGILL.
        #     -mcpu=cortex-a720 is safe because A720 is ARMv9.2, not 9.4.
        #
        # -mbranch-protection=none
        # ─────────────────────────────────────────────────────────────────────
        # Disables BTI (Branch Target Identification) and PAC (Pointer
        # Authentication Code) prologues.  Every hooked function normally
        # receives a HINT #34 (BTI c) instruction as its first byte, and
        # PAC inserts PACIBSP / AUTIBSP around returns.  With our ThinLTO
        # inlining, most small functions become fully inlined and these
        # prologues are wasted instruction bytes in the hot path.
        #
        # -flto=thin
        # ─────────────────────────────────────────────────────────────────────
        # ThinLTO (LLVM): cross-module inlining with parallel compilation.
        # Unlike full LTO, ThinLTO imports only the summary IR for each TU,
        # allowing the linker to inline across .cpp file boundaries (e.g.
        # LockFreeQueue::try_push into DispatcherHook without a function call).
        # Requires LLD (included with Android NDK r23+).
        target_compile_options(${PROJECT_NAME} PRIVATE
            -mcpu=cortex-a720           # ARMv9.2 + A720 micro-arch tuning
            -mbranch-protection=none    # Strip BTI/PAC prologues
            -flto=thin                  # Enable ThinLTO per-TU IR emission
        )
        target_link_options(${PROJECT_NAME} PRIVATE
            -flto=thin                  # ThinLTO link phase (requires lld)
            # Android NDK r23+ uses lld by default; no -fuse-ld= needed.
        )

    endif()

elseif(MSVC)
    # ── Windows / Desktop (MSVC) ─────────────────────────────────────────────
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            /O2 /Ob3 /Oi /Ot /GL /Gy /fp:fast /arch:AVX2
        >
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            /LTCG /OPT:REF /OPT:ICF /DEBUG:NONE
        >
    )
endif()

setup_geode_mod(${PROJECT_NAME})
