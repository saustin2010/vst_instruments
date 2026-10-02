cmake_minimum_required(VERSION 3.5)

# Common directives
include(${CMAKE_SOURCE_DIR}/common.cmake)

# Project name and version
project(Helm VERSION 0.9.1)

# JUCE project settings
juce_add_plugin(${PROJECT_NAME}
    COMPANY_NAME "Helm"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE HELM
    PLUGIN_CODE HELM
    FORMATS VST3 Standalone
#    FORMATS Standalone
    PRODUCT_NAME "Helm"
)

# Function to get all .cpp and .h files recursively in a directory
function(get_all_files_in_directory dir resultCpp resultH)
    file(GLOB_RECURSE new_filesCpp ${dir}/*.cpp)
    file(GLOB_RECURSE new_filesH ${dir}/*.h)
    set(${resultCpp} ${new_filesCpp} PARENT_SCOPE)
    set(${resultH} ${new_filesH} PARENT_SCOPE)
endfunction()

# Function to add sources and include directories to a target
function(add_sources_and_includes target)
    foreach(dir ${ARGN})
        get_all_files_in_directory(${CMAKE_CURRENT_SOURCE_DIR}/${dir} DIR_FILES_CPP DIR_FILES_H)
        target_sources(${target} PRIVATE ${DIR_FILES_CPP})
        target_sources(${target} PRIVATE ${DIR_FILES_H})
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
    endforeach()
endfunction()

# List of directories to include
set(DIRECTORIES
    ../mopo
    ../JuceLibraryCode
    plugin
    common
    synthesis
    editor_sections
    editor_components
    look_and_feel
    standalone
)

# Add sources and include directories
add_sources_and_includes(${PROJECT_NAME} ${DIRECTORIES})

# add external include directories
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/JuceLibraryCode)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/concurrentqueue)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/mopo/src)

# Links to JUCE dependencies
target_link_libraries(${PROJECT_NAME}
    PRIVATE
#        juce::juce_analytics
#        juce::juce_animation
#        juce::juce_audio_basics
#        juce::juce_audio_devices
#        juce::juce_audio_formats
#        juce::juce_audio_plugin_client
#        juce::juce_audio_processors
        juce::juce_audio_utils
#        juce::juce_box2d
#        juce::juce_core
#        juce::juce_cryptography
#        juce::juce_data_structures
        juce::juce_dsp
#        juce::juce_events
#        juce::juce_graphics
#        juce::juce_gui_basics
#        juce::juce_gui_extra
#        juce::juce_midi_ci
        juce::juce_opengl
#        juce::juce_osc
#        juce::juce_product_unlocking
#        juce::juce_video
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# JUCE settings
target_compile_definitions(${PROJECT_NAME}
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
        JUCE_MODAL_LOOPS_PERMITTED=1
)
