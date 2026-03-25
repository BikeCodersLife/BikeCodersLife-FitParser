cmake_minimum_required(VERSION 3.15)
project(fit-parser VERSION 2.0.0 LANGUAGES CXX)

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

# --- pugixml (XML parser, fetched via FetchContent) ---
include(FetchContent)
FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        v1.14
)
FetchContent_MakeAvailable(pugixml)

# Application source files
set(APP_SOURCES
    src/main.cpp
    src/fit_parser.cpp
    src/json_output.cpp
    src/gpx_parser.cpp
    src/tcx_parser.cpp
    src/fit_writer.cpp
)

# Create executable
add_executable(fit-parser ${APP_SOURCES} ${FIT_SDK_SOURCES})

# Link pugixml
target_link_libraries(fit-parser PRIVATE pugixml::pugixml)

# Compiler options
target_compile_options(fit-parser PRIVATE
    -O3                 # Maximum optimization
    -Wall               # All warnings
    -Wextra             # Extra warnings
    -pedantic           # Strict ISO C++
)

# Static linking for portability (Linux only)
if(UNIX AND NOT APPLE)
    target_link_options(fit-parser PRIVATE
        -static-libgcc
        -static-libstdc++
    )
endif()

# Install target
install(TARGETS fit-parser DESTINATION bin)

# Enable testing
enable_testing()

# Add tests subdirectory if it exists
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
