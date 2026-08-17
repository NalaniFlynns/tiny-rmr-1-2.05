cmake_minimum_required(VERSION 3.16)
project(fwsec LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(FWSEC_SOURCES
    fwsec_container.cpp
    fwsec_sha256.cpp
    fwsec_blake2b.cpp
    fwsec_aead.cpp
    fwsec_argon2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mlkem/cbd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mlkem/fips202.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mlkem/indcpa.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mlkem/kem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mlkem/ntt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mlkem/poly.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mlkem/polyvec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mlkem/randombytes.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mlkem/reduce.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mlkem/symmetric-shake.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mlkem/verify.c
)

add_library(fwsec STATIC ${FWSEC_SOURCES})
target_include_directories(fwsec PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/mlkem
)
target_link_libraries(fwsec PRIVATE bcrypt setupapi hid hidparse)
target_compile_definitions(fwsec PRIVATE _CRT_SECURE_NO_WARNINGS)

if(WIN32)
    target_sources(fwsec PRIVATE fwsec_webauthn.cpp fwsec_ctap2.cpp)
endif()

# Self tests (build when this directory is the top-level project)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_executable(fwsec_container_test fwsec_container_test.cpp)
    target_link_libraries(fwsec_container_test PRIVATE fwsec)
    add_test(NAME fwsec_container COMMAND fwsec_container_test)
endif()

