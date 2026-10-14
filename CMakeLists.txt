cmake_minimum_required(VERSION 3.22)
project(StreamRack VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

# Point at a local JUCE checkout with -DSTREAMRACK_JUCE_DIR=..., otherwise it is downloaded.
set(STREAMRACK_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
if(STREAMRACK_JUCE_DIR)
    add_subdirectory(${STREAMRACK_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ENGINE_SOURCES
    Source/Engine/Params.cpp
    Source/Engine/PluginSlot.cpp
    Source/Engine/PluginHost.cpp
    Source/Engine/ChannelStrip.cpp
    Source/Engine/RackProcessor.cpp)

set(COMMON_DEFINITIONS
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

set(COMMON_MODULES
    juce::juce_audio_utils
    juce::juce_cryptography
    juce::juce_audio_processors
    juce::juce_dsp)

# ---- App ----
juce_add_gui_app(StreamRack
    PRODUCT_NAME "StreamRack"
    COMPANY_NAME "StreamRack"
    BUNDLE_ID "org.streamrack.app"
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "StreamRack needs audio input access to receive Dante channels.")

target_sources(StreamRack PRIVATE
    ${ENGINE_SOURCES}
    Source/Main.cpp
    Source/UI/MainComponent.cpp
    Source/UI/StripComponent.cpp
    Source/UI/BusPanel.cpp
    Source/UI/DetailPanel.cpp
    Source/UI/EqCurve.cpp
    Source/UI/MasterPanel.cpp
    Source/UI/PluginUi.cpp)

target_include_directories(StreamRack PRIVATE Source)
target_compile_definitions(StreamRack PRIVATE ${COMMON_DEFINITIONS} JUCE_APPLICATION_NAME_STRING="StreamRack" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(StreamRack PRIVATE ${COMMON_MODULES}
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Link-time optimisation makes the app slightly faster but the build much slower; CI turns it off.
option(STREAMRACK_LTO "Build the app with link-time optimisation" ON)
if(STREAMRACK_LTO)
    target_link_libraries(StreamRack PRIVATE juce::juce_recommended_lto_flags)
endif()

# ---- Tests ----
juce_add_console_app(StreamRackTests PRODUCT_NAME "StreamRackTests")
target_sources(StreamRackTests PRIVATE ${ENGINE_SOURCES} Tests/EngineTests.cpp)
target_include_directories(StreamRackTests PRIVATE Source)
target_compile_definitions(StreamRackTests PRIVATE ${COMMON_DEFINITIONS})
target_link_libraries(StreamRackTests PRIVATE ${COMMON_MODULES}
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME engine COMMAND StreamRackTests)
