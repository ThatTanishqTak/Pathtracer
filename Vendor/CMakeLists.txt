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
set(SPDLOG_USE_STD_FORMAT ON)
set(SPDLOG_BUILD_EXAMPLE_HO OFF)
set(SPDLOG_BUILD_TESTS OFF)
set(SPDLOG_BUILD_TESTS_HO OFF)
set(SPDLOG_BUILD_BENCH OFF)
set(SPDLOG_BUILD_WARNINGS OFF)
set(SPDLOG_ENABLE_PCH OFF)
set(SPDLOG_INSTALL OFF)

add_subdirectory(spdlog SYSTEM)
# --------------

# glm --------------
set(GLM_BUILD_LIBRARY OFF)
set(GLM_BUILD_TESTS OFF)
set(GLM_BUILD_INSTALL OFF)

add_subdirectory(glm SYSTEM)
# --------------

# nlohmann/json --------------
set(JSON_BuildTests OFF)
set(JSON_Install OFF)
set(JSON_ImplicitConversions OFF)

add_subdirectory(json SYSTEM)
# --------------

# Vulkan --------------
find_package(Vulkan 1.4 REQUIRED GLOBAL)
find_program(Vulkan_SLANGC_EXECUTABLE NAMES slangc
    HINTS
        "$ENV{VULKAN_SDK}/bin"
        "$ENV{VULKAN_SDK}/Bin"
        "${Vulkan_INCLUDE_DIR}/../bin"
        "${Vulkan_INCLUDE_DIR}/../Bin"
    DOC "Slang compiler shipped with the Vulkan SDK"
)
mark_as_advanced(Vulkan_SLANGC_EXECUTABLE)

if(NOT Vulkan_SLANGC_EXECUTABLE)
    message(FATAL_ERROR "slangc was not found. Install a Vulkan 1.4 SDK that bundles Slang and make sure VULKAN_SDK points at it, or set Vulkan_SLANGC_EXECUTABLE to the slangc binary.")
endif()

add_executable(Vulkan::slangc IMPORTED GLOBAL)
set_property(TARGET Vulkan::slangc PROPERTY IMPORTED_LOCATION "${Vulkan_SLANGC_EXECUTABLE}")

message(STATUS "Found slangc: ${Vulkan_SLANGC_EXECUTABLE}")

# volk: runtime Vulkan function loader
set(VOLK_PULL_IN_VULKAN ON)
add_subdirectory(volk SYSTEM)

# VMA: Vulkan memory allocator, header-only interface target
add_subdirectory(VulkanMemoryAllocator SYSTEM)
# --------------

# Dear ImGui --------------
set(IMGUI_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/imgui")

add_library(imgui STATIC
    "${IMGUI_DIRECTORY}/imgui.cpp"
    "${IMGUI_DIRECTORY}/imgui_demo.cpp"
    "${IMGUI_DIRECTORY}/imgui_draw.cpp"
    "${IMGUI_DIRECTORY}/imgui_tables.cpp"
    "${IMGUI_DIRECTORY}/imgui_widgets.cpp"
    "${IMGUI_DIRECTORY}/backends/imgui_impl_sdl3.cpp"
    "${IMGUI_DIRECTORY}/backends/imgui_impl_vulkan.cpp"
)

target_include_directories(imgui SYSTEM PUBLIC "${IMGUI_DIRECTORY}" "${IMGUI_DIRECTORY}/backends")
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(imgui PUBLIC SDL3::SDL3 volk::volk)

add_library(imgui::imgui ALIAS imgui)
# --------------