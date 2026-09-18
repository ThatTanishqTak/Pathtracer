# Engine CMakeLists.txt

set(TARGET_NAME Engine)

# Collect the source files
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
)

# Collect the public header files
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.hpp"
)

# Collect the private header files
file(GLOB_RECURSE INTERNALS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Internal/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Internal/*.hpp"
)

# Collect the Slang sources
file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.slang"
)

file(GLOB_RECURSE SHADER_MODULES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Modules/*.slang"
)

# Create the static library
add_library(${TARGET_NAME} STATIC ${SOURCES} ${HEADERS} ${INTERNALS})

# IDE source layout
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source"   PREFIX "Source"   FILES ${SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Include"  PREFIX "Include"  FILES ${HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Internal" PREFIX "Internal" FILES ${INTERNALS})

# Shader compilation --------------
set(SHADER_OUTPUT_DIRECTORY "${OUTPUT_BASE}/Shaders")
set(SHADER_BINARIES)

foreach(SHADER_SOURCE IN LISTS SHADER_SOURCES)
    get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME)
    set(SHADER_BINARY "${SHADER_OUTPUT_DIRECTORY}/${SHADER_NAME}.spv")

    add_custom_command(OUTPUT "${SHADER_BINARY}" COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIRECTORY}"
        COMMAND Vulkan::slangc -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name $<IF:$<CONFIG:Debug>,-g2,-O2> -I "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Modules" -depfile "${SHADER_BINARY}.d" -o "${SHADER_BINARY}" "${SHADER_SOURCE}"
        DEPENDS "${SHADER_SOURCE}" ${SHADER_MODULES}
        DEPFILE "${SHADER_BINARY}.d"
        COMMENT "Compiling shader ${SHADER_NAME}"
        VERBATIM
    )

    list(APPEND SHADER_BINARIES "${SHADER_BINARY}")
endforeach()

# Every target that links Engine builds the shaders, so Sandbox and later Editor both get them
add_custom_target(${TARGET_NAME}Shaders DEPENDS ${SHADER_BINARIES} SOURCES ${SHADER_SOURCES} ${SHADER_MODULES})
add_dependencies(${TARGET_NAME} ${TARGET_NAME}Shaders)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Shaders" PREFIX "Shaders" FILES ${SHADER_SOURCES} ${SHADER_MODULES})
# --------------

# Include directories, only Include is visible to sandbox
target_include_directories(${TARGET_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Internal"
)

# C++ standard
target_compile_features(${TARGET_NAME} PUBLIC cxx_std_23)

# Warnings
target_compile_options(${TARGET_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

# Unlocks the internal headers
target_compile_definitions(${TARGET_NAME} PRIVATE PT_ENGINE_BUILD VK_NO_PROTOTYPES VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1 $<$<CONFIG:Debug>:PT_DEBUG>)

# Public so that both Sanbox and Editor can see the same glm configuration
target_compile_definitions(${TARGET_NAME} PUBLIC GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)

# Third-party libraries
target_link_libraries(${TARGET_NAME}
    PUBLIC    
        glm::glm
    PRIVATE 
        SDL3::SDL3
        Vulkan::Headers
        volk::volk
        GPUOpen::VulkanMemoryAllocator
        spdlog
)

# Engine properties
set_target_properties(${TARGET_NAME} PROPERTIES 
    # Set output directory
    ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_BASE}"

    # Set C++ standard
    CXX_EXTENSIONS OFF
)