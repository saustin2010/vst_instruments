cmake_minimum_required(VERSION 3.5)

# Common directives
include(${CMAKE_SOURCE_DIR}/common.cmake)

# Project name
project(Helm)

# name of the library folder (you can do a symlink if your library folder is outside of the project)
set(LIB_DIR ${CMAKE_SOURCE_DIR}/libs) # TODO: you must include JUCE in the libs subdirectory (in my case, libs is a symbolic link to an outside directory).

# path to JUCE library
add_subdirectory(${LIB_DIR}/JUCE)

# path to source code with CMakeLists.txt
add_subdirectory(src)
