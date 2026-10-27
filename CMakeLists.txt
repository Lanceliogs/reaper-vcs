cmake_minimum_required(VERSION 4.0)

project(reaper-vcs)

set(CMAKE_CXX_STANDARD 17)

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)

include(FetchContent)

# libwavpack for flac support
FetchContent_Declare(
    wavpack
    GIT_REPOSITORY https://github.com/dbry/WavPack
    GIT_TAG        5.9.0 # or whichever release/tag you need
)

# Optional: Disable building WavPack CLI tools, tests, or docs if you only want the library
set(WAVPACK_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(WAVPACK_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(WAVPACK_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(wavpack)

add_executable(analyse
    src/analyse.cpp src/hash.cpp src/wvra.cpp src/wavpack_utils.cpp
)
target_link_libraries(analyse PRIVATE wavpack OpenSSL::Crypto z)

add_executable(compress
    src/compress.cpp src/hash.cpp src/wvra.cpp src/wavpack_utils.cpp
)
target_link_libraries(compress PRIVATE wavpack OpenSSL::Crypto z)
