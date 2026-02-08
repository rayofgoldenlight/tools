cmake_minimum_required(VERSION 3.12)
project(mp3_analyzer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find mpg123
find_path(MPG123_INCLUDE_DIR mpg123.h)
find_library(MPG123_LIBRARY NAMES mpg123)

if(NOT MPG123_INCLUDE_DIR OR NOT MPG123_LIBRARY)
    message(FATAL_ERROR "libmpg123 not found. Please install libmpg123-dev (Linux) or brew install mpg123 (macOS).")
endif()

add_executable(mp3_analyzer
    src/main.c
    src/audio_decoder.c
    src/feature_extractor.c
    src/psychoacoustics.c
    src/grading.c
    src/rhythm.c
    src/harmony.c
    src/melody.c
    src/structure.c
    src/production.c
    src/geniusgrading.c
)

target_include_directories(mp3_analyzer PRIVATE
    ${MPG123_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)


target_link_libraries(mp3_analyzer
    ${MPG123_LIBRARY}
    m
    shlwapi
)