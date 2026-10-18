cmake_minimum_required(VERSION 3.20)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PMP3_VERSION LIMIT_COUNT 1)
project(PodcastMP3 VERSION ${PMP3_VERSION} LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# MinGW's threading headers need the GNU dialect (-std=gnu++17).
set(CMAKE_CXX_EXTENSIONS ON)

option(PMP3_BUILD_TESTS "Build unit tests" ON)

add_library(pmp3core STATIC
    src/core/util.cpp
    src/core/childproc.cpp
    src/core/settings.cpp
    src/core/media.cpp
    src/core/job.cpp
    src/core/host.cpp)
target_include_directories(pmp3core PUBLIC third_party)

if(WIN32)
    target_compile_definitions(pmp3core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    target_link_libraries(pmp3core PUBLIC shell32 ole32 advapi32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(pmp3core PUBLIC Threads::Threads)
endif()

if(MSVC)
    # Visual Studio: the linker embeds app.manifest (the .rc files skip it there).
    set(PMP3_MSVC_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/app.manifest)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /DPMP3_NO_RC_MANIFEST")
    target_compile_options(pmp3core PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8;/permissive->")
else()
    target_compile_options(pmp3core PUBLIC -Wall -Wextra)
endif()

if(MINGW)
    # Static runtime: the .exe files run without extra MinGW DLLs.
    set(PMP3_MINGW_STATIC -static -static-libgcc -static-libstdc++)
    set(PMP3_MINGW_LINK ${PMP3_MINGW_STATIC} -municode)
endif()

# Command line tool + Edge native messaging host
add_executable(podcastmp3-host src/host/main.cpp)
target_link_libraries(podcastmp3-host PRIVATE pmp3core)
if(WIN32)
    target_sources(podcastmp3-host PRIVATE src/host/host.rc ${PMP3_MSVC_MANIFEST})
    target_link_options(podcastmp3-host PRIVATE ${PMP3_MINGW_LINK})
endif()

# Windows desktop window (job progress + settings)
if(WIN32)
    add_executable(PodcastMP3 WIN32 src/gui/main_win.cpp src/gui/gui.rc ${PMP3_MSVC_MANIFEST})
    target_link_libraries(PodcastMP3 PRIVATE pmp3core comctl32 uxtheme)
    target_link_options(PodcastMP3 PRIVATE ${PMP3_MINGW_LINK})
endif()

if(PMP3_BUILD_TESTS)
    enable_testing()
    add_executable(pmp3tests tests/test_core.cpp)
    target_link_libraries(pmp3tests PRIVATE pmp3core)
    if(WIN32)
        target_link_options(pmp3tests PRIVATE ${PMP3_MINGW_STATIC})
    endif()
    add_test(NAME core COMMAND pmp3tests)
endif()
