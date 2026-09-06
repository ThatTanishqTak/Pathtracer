# Root CMakeLists.txt

cmake_minimum_required(VERSION 3.30)
project(Pathtracer)

# Convert CMake architecture name to more readable architecture name
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" ARCH_NAME)
if(ARCH_NAME MATCHES "amd64|x86_64")
    set(ARCH_NAME "x86_64")
elseif(ARCH_NAME MATCHES "i[3-6]86|x86")
    set(ARCH_NAME "x86_32")
elseif(ARCH_NAME MATCHES "aarch64|arm64")
    set(ARCH_NAME "arm64")
endif()

# Output directory
set(OUTPUT_BASE "${CMAKE_SOURCE_DIR}/bin/$<CONFIG>-${CMAKE_SYSTEM_NAME}-${ARCH_NAME}/${PROJECT_NAME}")

# Subdirectories
add_subdirectory(Engine)
add_subdirectory(Vendor)
add_subdirectory(Sandbox)