cmake_minimum_required(VERSION 3.24)

project(VulkanTutorial LANGUAGES CXX)

# forces c++20 globally for all targets, including dependencies
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(
    FETCHCONTENT_BASE_DIR
    "${CMAKE_SOURCE_DIR}/external"
    CACHE PATH ""
    FORCE
)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# vulkan-hpp and vulkan_raii.hpp are included with the installed vulkan sdk
find_package(Vulkan REQUIRED)

# add_subdirectory("${CMAKE_SOURCE_DIR}/external/glfw")
file(GLOB_RECURSE SOURCES "src/*.cpp")

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
)

FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG release
)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)

FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.4.0
)

FetchContent_MakeAvailable(glfw glm tinyobjloader stb vma)

add_executable(${PROJECT_NAME} ${SOURCES})
# allows designated initializers
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/external/stb-src") # stb is comprised of single-header libraries and does not provide a normal CMake target to link against
target_link_libraries(${PROJECT_NAME} Vulkan::Vulkan glfw glm::glm tinyobjloader GPUOpen::VulkanMemoryAllocator)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# enforce 64-bit CPU architecte to avoid accidentally mixing incompatible build architectures
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "Configuring for 64-bit architecture.")
else()
    message(FATAL_ERROR "Expected 64-bit architecture, but detected 32-bit. Please configure for 64-bit.")
endif()
# cmake -S . -B build -G "Visual Studio 17 2022" -A x64
