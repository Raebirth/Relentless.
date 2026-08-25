cmake_minimum_required(VERSION 3.21)
project(Relentless VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# The GitHub Action downloads the SDK and exposes it via GEODE_SDK.
# We map it to CMAKE_PREFIX_PATH so find_package can locate GeodeConfig.cmake
if(DEFINED ENV{GEODE_SDK})
    message(STATUS "GEODE_SDK environment variable detected: $ENV{GEODE_SDK}")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{GEODE_SDK}")
    set(Geode_DIR "$ENV{GEODE_SDK}")
endif()

# Include Geode SDK
find_package(Geode REQUIRED)


include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    message(STATUS "LTO / IPO is ENABLED for Release builds.")
else()
    message(WARNING "IPO not supported: ${ipo_error}")
endif()


add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    # Note: src/LockFreeQueue.hpp is header-only and does not need to be compiled
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # Clang (Android NDK) & GCC
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            -O3                         # Aggressive loop unrolling and auto-vectorization
            -fvisibility=hidden          # Shrinks ELF dynamic symbol table; resolves calls locally
            -fvisibility-inlines-hidden  # Strips inline symbols from exports
            -fno-rtti                    # Eliminates RTTI type descriptors and vtable overhead
            -fno-exceptions              # Removes .eh_frame & unwind tables (GD does not catch C++ throws)
            -fomit-frame-pointer         # Frees up a general-purpose register
            -fstrict-aliasing            # Enables aggressive load/store reordering
            
            
            -fno-math-errno              # Avoids setting errno on sqrt/pow
            -fno-trapping-math           # Disables floating-point hardware traps
            -freciprocal-math            # Turns x / y into x * (1/y) where applicable
            -ffp-contract=fast           # Generates FMA (Fused Multiply-Add) instructions
        >
    )

    # Linker flags for Release
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            -Wl,--gc-sections            # Strips unused code & dead data sections
            -Wl,-s                       # Strips all debug symbol tables
            -Wl,-z,now                   # Immediate binding (eliminates runtime PLT lookup stubs)
            -Wl,-z,relro                 # Read-only relocations after load
            $<$<PLATFORM_ID:Android>:
                -Wl,--hash-style=gnu     # O(1) symbol lookup in dynamic linker
            >
        >
    )

    
    if(GEODE_TARGET_PLATFORM STREQUAL "Android32")
        target_compile_options(${PROJECT_NAME} PRIVATE
            -march=armv7-a
            -mfpu=neon-vfpv4             # Enables NEON SIMD & VFPv4 hardware vector units
            -mfloat-abi=softfp           # Matches Android 32 ABI standard
            -mthumb                      # Generates compact 16/32-bit Thumb-2 instructions
        )
    elseif(GEODE_TARGET_PLATFORM STREQUAL "Android64")
        target_compile_options(${PROJECT_NAME} PRIVATE
            -march=armv8-a+crc+crypto    # Enables 64-bit ARMv8 with CRC/crypto extensions
            -mbranch-protection=none     # Avoids PAC/BTI instruction overhead in hot loops
        )
    endif()

elseif(MSVC)
    # MSVC (Windows Builds)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            /O2                          # Maximize speed
            /Ob3                         # Aggressive inline expansion
            /Oi                          # Enable intrinsic functions
            /Ot                          # Favor fast code over small code
            /GL                          # Whole Program Optimization
            /Gy                          # Function-level linking (enables COMDAT folding)
            /GR-                         # Disable RTTI
            /fp:fast                     # Fast floating-point model
            /arch:AVX2                   # Generate AVX2 SIMD instructions (x86_64)
        >
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            /LTCG                        # Link-Time Code Generation
            /OPT:REF                     # Eliminate unreferenced functions
            /OPT:ICF                     # Identical COMDAT folding
            /DEBUG:NONE                  # Strip all debug info from DLL
        >
    )
endif()

setup_geode_mod(${PROJECT_NAME})
