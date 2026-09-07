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

# Create the static library
add_library(${TARGET_NAME} STATIC ${SOURCES} ${HEADERS} ${INTERNALS})

# IDE source layout
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source"   PREFIX "Source"   FILES ${SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Include"  PREFIX "Include"  FILES ${HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Internal" PREFIX "Internal" FILES ${INTERNALS})

# Include directories, only Include is visible to consumers
target_include_directories(${TARGET_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Internal"
)

# C++ standard
target_compile_features(${TARGET_NAME} PUBLIC cxx_std_23)

# Unlocks the internal headers, never set on consumers
target_compile_definitions(${TARGET_NAME} PRIVATE PT_ENGINE_BUILD)

# Third-party libraries
target_link_libraries(${TARGET_NAME}
    PRIVATE 
        glfw
        spdlog
)

# Engine properties
set_target_properties(${TARGET_NAME} PROPERTIES 
    # Set output directory
    ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_BASE}"

    # Set C++ standard
    CXX_EXTENSIONS OFF
)