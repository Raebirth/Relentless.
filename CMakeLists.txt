cmake_minimum_required(VERSION 3.21)
project(Relentless VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SDK de Geode
find_package(Geode REQUIRED)

# Link-Time Optimization (LTO / IPO)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# Archivos de código fuente
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.hpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

# Flags de optimización por compilador
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # Android NDK / Clang
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            -O3
            -fvisibility=hidden
            -fvisibility-inlines-hidden
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

    if(GEODE_TARGET_PLATFORM STREQUAL "Android64")
        target_compile_options(${PROJECT_NAME} PRIVATE
            -march=armv8-a+crc+crypto
            -mbranch-protection=none
        )
    endif()

elseif(MSVC)
    # Windows MSVC
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            /O2
            /Ob3
            /Oi
            /Ot
            /GL
            /Gy
            /fp:fast
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
