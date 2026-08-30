cmake_minimum_required(VERSION 3.21)
project(Relentless VERSION 1.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# GEODE SDK DISCOVERY
# ---------------------------------------------------------------------------
if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# ---------------------------------------------------------------------------
# 1. LINK-TIME OPTIMIZATION (LTO / IPO)
# ---------------------------------------------------------------------------
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    message(STATUS "LTO / IPO is ENABLED for Release builds.")
else()
    message(WARNING "IPO not supported: ${ipo_error}")
endif()

# ---------------------------------------------------------------------------
# 2. SOURCE & TARGET DEFINITIONS
# ---------------------------------------------------------------------------
add_library(${PROJECT_NAME} SHARED
    src/main.cpp
)

# ---------------------------------------------------------------------------
# 3. PLATFORM-SPECIFIC SYSTEM LIBRARIES
# ---------------------------------------------------------------------------
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm)
endif()

# ---------------------------------------------------------------------------
# 4. AGGRESSIVE COMPILER & LINKER FLAGS
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            -O3
            -fvisibility=hidden
            -fvisibility-inlines-hidden
            -fno-exceptions
            -fomit-frame-pointer
            -fstrict-aliasing
            -fno-math-errno
            -fno-trapping-math
            -freciprocal-math
            -ffp-contract=fast
        >
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            -Wl,--gc-sections
            -Wl,-s
            -Wl,-z,now
            -Wl,-z,relro
            $<$<PLATFORM_ID:Android>:
                -Wl,--hash-style=gnu
            >
        >
    )

    if(GEODE_TARGET_PLATFORM STREQUAL "Android32")
        target_compile_options(${PROJECT_NAME} PRIVATE
            -march=armv7-a
            -mfpu=neon-vfpv4
            -mfloat-abi=softfp
            -mthumb
        )
    elseif(GEODE_TARGET_PLATFORM STREQUAL "Android64")
        # rcpc3 removido para evitar crashes SIGILL en Snapdragon 8 Elite
        target_compile_options(${PROJECT_NAME} PRIVATE
            -march=armv8-a+crc+crypto
            -mbranch-protection=none
        )
    endif()

elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            /O2
            /Ob3
            /Oi
            /Ot
            /GL
            /Gy
            /fp:fast
            /arch:AVX2
        >
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            /LTCG
            /OPT:REF
            /OPT:ICF
            /DEBUG:NONE
        >
    )
endif()

setup_geode_mod(${PROJECT_NAME})
