# Sandbox CMakeLists.txt

set(TARGET_NAME Sandbox)

# Collect the source files
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
)

# Collect the public header files
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.hpp"
)

# Create the executable
add_executable(${TARGET_NAME} ${SOURCES} ${HEADERS})

# IDE source layout
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source"  PREFIX "Source"  FILES ${SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Include" PREFIX "Include" FILES ${HEADERS})

# Include directory
target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Include")

# Linking executable against the engine
target_link_libraries(${TARGET_NAME} PRIVATE Engine)

# Warnings
target_compile_options(${TARGET_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

# Sandbox properties
set_target_properties(${TARGET_NAME} PROPERTIES
    # Set output directory
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_BASE}"

    # Set C++ standard
    CXX_EXTENSIONS OFF

    # Set debugger working directory
    VS_DEBUGGER_WORKING_DIRECTORY "${OUTPUT_BASE}"
)