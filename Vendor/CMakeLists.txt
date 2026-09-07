# Vendor CMakeLists.txt

# IDE folder for all vendor targets
set(CMAKE_FOLDER "Vendor")

# glfw --------------
set(GLFW_BUILD_DOCS OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_INSTALL OFF)

add_subdirectory(glfw SYSTEM)
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