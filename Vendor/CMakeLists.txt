# Vendor CMakeLists.txt

# IDE folder for all vendor targets
set(CMAKE_FOLDER "Vendor")

# SDL3 --------------
set(SDL_SHARED OFF)
set(SDL_STATIC ON)
set(SDL_TEST_LIBRARY OFF)
set(SDL_TESTS OFF)
set(SDL_EXAMPLES OFF)
set(SDL_INSTALL OFF)

# Only the windowing/video subsystem is used
set(SDL_AUDIO OFF)
set(SDL_JOYSTICK OFF)
set(SDL_HAPTIC OFF)
set(SDL_HIDAPI OFF)
set(SDL_SENSOR OFF)
set(SDL_CAMERA OFF)
set(SDL_POWER OFF)
set(SDL_DIALOG OFF)
set(SDL_RENDER OFF)
set(SDL_GPU OFF)

add_subdirectory(SDL SYSTEM)
# --------------

# spdlog --------------
set(SPDLOG_BUILD_EXAMPLE OFF)
set(SPDLOG_BUILD_EXAMPLE_HO OFF)
set(SPDLOG_BUILD_TESTS OFF)
set(SPDLOG_BUILD_TESTS_HO OFF)
set(SPDLOG_BUILD_BENCH OFF)
set(SPDLOG_BUILD_WARNINGS OFF)
set(SPDLOG_ENABLE_PCH OFF)
set(SPDLOG_INSTALL OFF)

add_subdirectory(spdlog SYSTEM)
# --------------

# Vulkan --------------
# --------------