cmake_minimum_required(VERSION 3.16)
project(openyounkoolite VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

add_compile_definitions(
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    add_compile_options(/W3 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wno-unused-parameter)
endif()

set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

file(GLOB LUA_SOURCES "${THIRD_PARTY_DIR}/lua/*.c")

set(SOURCES
    src/main.cpp
    src/app.cpp
    src/log.cpp
    src/config.cpp
    src/modules.cpp
    src/mc_runtime.cpp
    src/lua_manager.cpp
    src/http_server.cpp
)

option(BUILD_NO_CONSOLE "build as Windows subsystem (no console window)" OFF)
if(BUILD_NO_CONSOLE)
    add_executable(openyounkoolite WIN32 ${SOURCES} ${LUA_SOURCES})
else()
    add_executable(openyounkoolite ${SOURCES} ${LUA_SOURCES})
endif()

target_include_directories(openyounkoolite PRIVATE
    ${THIRD_PARTY_DIR}
    ${THIRD_PARTY_DIR}/lua
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(openyounkoolite PRIVATE ws2_32 psapi)

add_custom_command(TARGET openyounkoolite POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:openyounkoolite>/scripts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts
            $<TARGET_FILE_DIR:openyounkoolite>/scripts
)

install(TARGETS openyounkoolite RUNTIME DESTINATION .)
install(DIRECTORY scripts/ DESTINATION scripts)
