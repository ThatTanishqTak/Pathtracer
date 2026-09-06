# CMakeLists.txt

project(Engine)
set(TARGET_NAME ${PROJECT_NAME})

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

# Include directories
target_include_directories(${TARGET_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Internal"
)

# Linking third-party libraries
target_link_libraries(${TARGET_NAME} PRIVATE 
    glfw
)

# Engine properties
set_target_properties(${TARGET_NAME} PROPERTIES 
    # Set output directory
    ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_BASE}"

    # Set C++ standard
    CMAKE_CXX_STANDARD 26
    CMAKE_CXX_STANDARD_REQUIRED ON
    CMAKE_CXX_EXTENSIONS OFF
)