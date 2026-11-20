cmake_minimum_required(VERSION 3.22)
project(BreakVar VERSION 0.3.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(CTest)
option(BREAKVAR_BUILD_PLUGIN "Build the Windows VST3 effect" ON)
set(BREAKVAR_JUCE_SOURCE "" CACHE PATH "Existing JUCE 8.0.12 source directory")

add_library(BreakVarCore STATIC Source/Pattern.cpp Source/Engine.cpp Source/PatternLibrary.cpp)
target_include_directories(BreakVarCore PUBLIC Source)
set_target_properties(BreakVarCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(BreakVarCore PRIVATE /W4 /permissive-)
endif()
if(BUILD_TESTING)
    add_executable(BreakVarModelTests Tests/ModelTests.cpp)
    target_link_libraries(BreakVarModelTests PRIVATE BreakVarCore)
    add_test(NAME model COMMAND BreakVarModelTests)
    add_executable(BreakVarEngineTests Tests/EngineTests.cpp)
    target_link_libraries(BreakVarEngineTests PRIVATE BreakVarCore)
    add_test(NAME engine COMMAND BreakVarEngineTests)
    add_executable(BreakVarLibraryTests Tests/LibraryTests.cpp)
    target_link_libraries(BreakVarLibraryTests PRIVATE BreakVarCore)
    add_test(NAME library COMMAND BreakVarLibraryTests)
endif()

if(BREAKVAR_BUILD_PLUGIN)
    if(BREAKVAR_JUCE_SOURCE)
        add_subdirectory("${BREAKVAR_JUCE_SOURCE}" "${CMAKE_BINARY_DIR}/juce")
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.12 GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()
    juce_add_plugin(BreakVar
        COMPANY_NAME "Samuel Verhaest"
        BUNDLE_ID "com.samuelverhaest.breakvar"
        PLUGIN_MANUFACTURER_CODE Svrh
        PLUGIN_CODE Bkvr
        PRODUCT_NAME "SHFLR"
        DESCRIPTION "Live colour phrase rearrangement effect"
        FORMATS VST3
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Other)
    target_sources(BreakVar PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/PluginEditor.h
        Source/PresetBrowser.cpp Source/PresetBrowser.h)
    target_compile_definitions(BreakVar PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(BreakVar PRIVATE BreakVarCore juce::juce_audio_processors juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
    # The custom standalone has its own application entry point. Keep JUCE's
    # plugin-only standalone definition on shared compilation, not its consumers.
    get_target_property(sharedInterfaceDefinitions BreakVar INTERFACE_COMPILE_DEFINITIONS)
    list(FILTER sharedInterfaceDefinitions EXCLUDE REGEX "^JUCE_STANDALONE_APPLICATION=")
    set_property(TARGET BreakVar PROPERTY INTERFACE_COMPILE_DEFINITIONS "${sharedInterfaceDefinitions}")
    juce_add_gui_app(SHFLRStandalone PRODUCT_NAME "SHFLR" COMPANY_NAME "Samuel Verhaest"
        BUNDLE_ID "com.samuelverhaest.shflr.standalone")
    target_sources(SHFLRStandalone PRIVATE Source/Standalone.cpp)
    target_include_directories(SHFLRStandalone PRIVATE Source)
    target_compile_definitions(SHFLRStandalone PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        SHFLR_VERSION="${PROJECT_VERSION}")
    # Link the shared implementation without inheriting plugin-wrapper macros.
    target_link_libraries(SHFLRStandalone PRIVATE $<LINK_ONLY:BreakVar> juce::juce_audio_utils
        juce::juce_recommended_config_flags)
    if(BUILD_TESTING)
        add_executable(BreakVarProcessorTests Tests/ProcessorTests.cpp)
        target_include_directories(BreakVarProcessorTests PRIVATE Source
            $<TARGET_PROPERTY:BreakVar,INCLUDE_DIRECTORIES>)
        target_compile_definitions(BreakVarProcessorTests PRIVATE
            $<TARGET_PROPERTY:BreakVar,COMPILE_DEFINITIONS>)
        target_link_libraries(BreakVarProcessorTests PRIVATE BreakVar)
        add_test(NAME processor COMMAND BreakVarProcessorTests)
        add_executable(BreakVarEditorTests Tests/EditorTests.cpp)
        target_include_directories(BreakVarEditorTests PRIVATE Source
            $<TARGET_PROPERTY:BreakVar,INCLUDE_DIRECTORIES>)
        target_compile_definitions(BreakVarEditorTests PRIVATE
            $<TARGET_PROPERTY:BreakVar,COMPILE_DEFINITIONS>)
        target_link_libraries(BreakVarEditorTests PRIVATE BreakVar)
        add_test(NAME editor COMMAND BreakVarEditorTests)
    endif()
endif()
