add_library(audiovcs_lib STATIC
    src/wav.cpp
    src/codec.cpp
    src/hash.cpp
    src/store.cpp
    src/pointer.cpp
    src/filter.cpp
)

target_include_directories(audiovcs_lib PUBLIC include)
target_link_libraries(audiovcs_lib PUBLIC wavpack OpenSSL::Crypto)

# OpenSSL static on Windows needs these system libs
if(WIN32)
    target_link_libraries(audiovcs_lib PUBLIC ws2_32 crypt32)
endif()

add_executable(audiovcs src/main.cpp)
target_link_libraries(audiovcs PRIVATE audiovcs_lib)

install(TARGETS audiovcs RUNTIME DESTINATION bin)
