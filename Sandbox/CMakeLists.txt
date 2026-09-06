# Sandbox CMakeLists.txt

project(Sandbox)
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

# Creating the executable
add_executable(${TARGET_NAME} ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(${TARGET_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
)

# Linking executable against the engine
target_link_libraries(${TARGET_NAME} PRIVATE Engine)

# Sandbox properties
set_target_properties(${TARGET_NAME} PROPERTIES
    # Set output directory
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_BASE}"

    # Set C++ standard
    CMAKE_CXX_STANDARD 26
    CMAKE_CXX_STANDARD_REQUIRED ON
    CMAKE_CXX_EXTENSIONS OFF
)