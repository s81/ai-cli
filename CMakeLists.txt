cmake_minimum_required(VERSION 3.21)
project(ai-cli VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ftxui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# ── Core library (no FTXUI — keeps test binary small) ──────────────────
add_library(ai-cli-core STATIC
    src/session.cpp
    src/session_store.cpp
)
target_include_directories(ai-cli-core PUBLIC src)
target_link_libraries(ai-cli-core PUBLIC nlohmann_json::nlohmann_json)

# ── PTY implementation (platform-selected) ─────────────────────────────
add_library(ai-cli-pty STATIC)
if(WIN32)
    target_sources(ai-cli-pty PRIVATE src/pty/pty_win.cpp)
else()
    target_sources(ai-cli-pty PRIVATE src/pty/pty_unix.cpp)
endif()
target_include_directories(ai-cli-pty PUBLIC src)
if(WIN32)
    target_link_libraries(ai-cli-pty PRIVATE kernel32)
endif()
if(APPLE)
    target_compile_definitions(ai-cli-pty PRIVATE _DARWIN_C_SOURCE)
endif()

# ── Main executable ────────────────────────────────────────────────────
add_executable(ai-cli
    src/main.cpp
    src/app.cpp
    src/ui/session_list.cpp
    src/ui/terminal_view.cpp
)
target_link_libraries(ai-cli PRIVATE
    ai-cli-core
    ai-cli-pty
    ftxui::screen
    ftxui::dom
    ftxui::component
)

# ── Tests ──────────────────────────────────────────────────────────────
enable_testing()
add_executable(ai-cli-tests
    tests/test_session.cpp
    tests/test_session_store.cpp
)
if(NOT WIN32)
    target_sources(ai-cli-tests PRIVATE tests/test_pty_unix.cpp)
endif()
target_link_libraries(ai-cli-tests PRIVATE
    ai-cli-core
    ai-cli-pty
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME unit-tests COMMAND ai-cli-tests)
