# Common commands to be included in all CMakeLists.txt via include(${CMAKE_SOURCE_DIR}/common.cmake)

# Specify default build type as Debug (faster)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# For Release builds, optimize for size (-Os) and strip debug symbols (-s) :
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Os -s")
