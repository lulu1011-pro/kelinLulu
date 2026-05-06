cmake_minimum_required(VERSION 3.20)
project(MindVault VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC UTF-8 support
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ─── 第三方库 ───
# SQLite3 (编译 amalgamation)
add_library(sqlite3 STATIC
    third_party/sqlite3.c
)
target_include_directories(sqlite3 PUBLIC third_party)
# 启用 FTS5 全文搜索
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_THREADSAFE=1
)

# Crow (header-only)
add_library(crow INTERFACE)
target_include_directories(crow INTERFACE third_party)

# nlohmann/json (header-only)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE third_party)

# ─── 主程序 ───
file(GLOB_RECURSE MINDVAULT_SOURCES src/*.cpp)
list(FILTER MINDVAULT_SOURCES EXCLUDE REGEX "src/main\\.cpp$")

add_library(mindvault_lib STATIC ${MINDVAULT_SOURCES})
target_link_libraries(mindvault_lib PUBLIC
    sqlite3
    crow
    nlohmann_json
)
target_include_directories(mindvault_lib PUBLIC src)

add_executable(mindvault src/main.cpp)
target_link_libraries(mindvault PRIVATE mindvault_lib)

# Windows 特殊设置
if(WIN32)
    target_compile_definitions(mindvault PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
    target_compile_definitions(mindvault_lib PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
endif()

# ─── 安装 ───
install(TARGETS mindvault RUNTIME DESTINATION bin)
