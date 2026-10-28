cmake_minimum_required(VERSION 3.20)
project(audiovcs VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)

# Default install prefix: %LOCALAPPDATA%/audiovcs on Windows, ~/.local on Linux
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(WIN32)
        set(CMAKE_INSTALL_PREFIX "$ENV{LOCALAPPDATA}/audiovcs"
            CACHE PATH "Install prefix" FORCE)
    else()
        set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local"
            CACHE PATH "Install prefix" FORCE)
    endif()
endif()

# Static linking: embed the C/C++ runtime so the exe has zero DLL dependencies
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)

include(FetchContent)

FetchContent_Declare(
    wavpack
    GIT_REPOSITORY https://github.com/dbry/WavPack
    GIT_TAG        5.9.0
)

set(WAVPACK_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(WAVPACK_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(WAVPACK_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(wavpack)

add_subdirectory(core)

option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    add_subdirectory(test)
endif()
