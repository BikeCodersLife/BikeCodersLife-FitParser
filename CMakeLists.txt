cmake_minimum_required(VERSION 3.15)
project(fit-parser VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Garmin FIT SDK path
set(FIT_SDK_PATH "${CMAKE_SOURCE_DIR}/third_party/fit-sdk/cpp")

# Check if FIT SDK exists
if(NOT EXISTS "${FIT_SDK_PATH}")
    message(FATAL_ERROR "Garmin FIT SDK not found at ${FIT_SDK_PATH}. Please download it from https://developer.garmin.com/fit/download/")
endif()

# Include FIT SDK headers
include_directories(${FIT_SDK_PATH})

# FIT SDK source files
file(GLOB FIT_SDK_SOURCES "${FIT_SDK_PATH}/*.cpp")

# Shared sources (parser + writer, reused by both executables)
set(LIB_SOURCES
    src/fit_parser.cpp
    src/fit_writer.cpp
    src/json_output.cpp
)

# fit-parser executable
add_executable(fit-parser src/main.cpp ${LIB_SOURCES} ${FIT_SDK_SOURCES})

# gps-stripper executable (strips GPS lat/lon from start/end trim zones)
add_executable(gps-stripper
    src/main_stripper.cpp
    src/gps_stripper.cpp
    ${LIB_SOURCES}
    ${FIT_SDK_SOURCES}
)

# Compiler options (applied to both executables)
foreach(TARGET fit-parser gps-stripper)
    target_compile_options(${TARGET} PRIVATE
        -O3                 # Maximum optimization
        -Wall               # All warnings
        -Wextra             # Extra warnings
        -pedantic           # Strict ISO C++
    )
endforeach()

# Static linking for portability (Linux only)
if(UNIX AND NOT APPLE)
    foreach(TARGET fit-parser gps-stripper)
        target_link_options(${TARGET} PRIVATE
            -static-libgcc
            -static-libstdc++
        )
    endforeach()
endif()

# Install targets
install(TARGETS fit-parser gps-stripper DESTINATION bin)

# Enable testing
enable_testing()

# Add tests subdirectory if it exists
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
