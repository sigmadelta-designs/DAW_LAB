cmake_minimum_required(VERSION 3.22)

project(DAW_LAB VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The audio thread and the trace solver both want optimised code.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(DawLab
    COMPANY_NAME "VstFactory"
    BUNDLE_ID com.vstfactory.dawlab
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "DAW_LAB routes audio input through hosted plugins."
    PRODUCT_NAME "DAW_LAB")

# The SerDes stages, shared by the host (which lists them as built-ins) and the plugins.
set(DAWLAB_STAGE_SOURCES
    Source/Builtin/BitCompare.cpp
    Source/Builtin/DiffFfe.cpp
    Source/Builtin/EyeAnalyzer.cpp
    Source/Builtin/EyeScope.cpp
    Source/Builtin/EyeScopeEditor.cpp
    Source/Builtin/FfeEditor.cpp
    Source/Builtin/GeneratorEditor.cpp
    Source/Builtin/LossyChannel.cpp
    Source/Builtin/LossyEditor.cpp
    Source/Builtin/NoiseInjector.cpp
    Source/Builtin/NoiseEditor.cpp
    Source/Builtin/SignalGenerator.cpp
    Source/Builtin/ReceiverEditor.cpp
    Source/Builtin/SerdesReceiver.cpp
    Source/Builtin/SimpleParamEditor.cpp
    Source/Builtin/SParamChannel.cpp
    Source/Builtin/SParamEditor.cpp
    Source/Builtin/SParamSynthesis.cpp
    Source/Builtin/Touchstone.cpp
    Source/Builtin/TraceBoard.cpp
    Source/Builtin/TraceChannel.cpp
    Source/Builtin/TraceReflections.cpp
    Source/Builtin/TraceEditor.cpp
    Source/Builtin/TraceRouter.cpp
    Source/Builtin/SymbolProbe.cpp)

target_sources(DawLab
    PRIVATE
        Source/Main.cpp
        Source/ChainView.cpp
        Source/Builtin/BuiltInFormat.cpp
        ${DAWLAB_STAGE_SOURCES}
        Source/HostEngine.cpp
        Source/MainComponent.cpp)

# AU is a macOS/iOS format; there is nothing to host on Windows or Linux.
if(APPLE)
    set(DAWLAB_HOST_FORMAT_DEFS JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1)
else()
    set(DAWLAB_HOST_FORMAT_DEFS JUCE_PLUGINHOST_VST3=1)
endif()

target_compile_definitions(DawLab
    PRIVATE
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:DawLab,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:DawLab,JUCE_VERSION>"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        ${DAWLAB_HOST_FORMAT_DEFS})

target_link_libraries(DawLab
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- The stages as plugins (VST3 + AU) -------------------------------------------------------------------
#
# One plugin per stage. They find each other through SharedLink (one link per host process), so a chain of
# them in a DAW behaves like the built-in chain: line rate and modulation are set on the Signal Generator.
# Each is installed to ~/Library/Audio/Plug-Ins after building on macOS (COPY_PLUGIN_AFTER_BUILD, as
# elsewhere in this family); on Windows that lands in the common VST3 folder instead. Plugin codes
# Dl01-Dl08 under manufacturer code Vstf. AU is macOS/iOS only, so Windows (and Linux) only get VST3.
if(APPLE)
    set(DAWLAB_STAGE_FORMATS AU VST3)
else()
    set(DAWLAB_STAGE_FORMATS VST3)
endif()

function(dawlab_add_stage target product code header class)
    juce_add_plugin(${target}
        COMPANY_NAME "VstFactory"
        BUNDLE_ID com.vstfactory.dawlab.${target}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD TRUE
        PLUGIN_MANUFACTURER_CODE Vstf
        PLUGIN_CODE ${code}
        FORMATS ${DAWLAB_STAGE_FORMATS}
        PRODUCT_NAME "${product}")

    target_sources(${target} PRIVATE Source/Plugin/PluginEntry.cpp ${DAWLAB_STAGE_SOURCES})

    target_include_directories(${target} PRIVATE Source)

    target_compile_definitions(${target}
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            DAWLAB_STAGE_HEADER="${header}"
            DAWLAB_STAGE_CLASS=${class}
            ${ARGN})

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_processors
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endfunction()

dawlab_add_stage(DawLabGenerator "DAW_LAB Signal Generator" Dl01 "Builtin/SignalGenerator.h" SignalGenerator "DAWLAB_STAGE_ARGS=,true")
dawlab_add_stage(DawLabFfe       "DAW_LAB FFE"              Dl02 "Builtin/DiffFfe.h"         DiffFfe)
dawlab_add_stage(DawLabLossy     "DAW_LAB Lossy Channel"    Dl03 "Builtin/LossyChannel.h"    LossyChannel)
dawlab_add_stage(DawLabTrace     "DAW_LAB Trace Channel"    Dl04 "Builtin/TraceChannel.h"    TraceChannel)
dawlab_add_stage(DawLabSParam    "DAW_LAB S-Param Channel"  Dl05 "Builtin/SParamChannel.h"   SParamChannel)
dawlab_add_stage(DawLabNoise     "DAW_LAB Noise Injector"   Dl06 "Builtin/NoiseInjector.h"   NoiseInjector)
dawlab_add_stage(DawLabReceiver  "DAW_LAB Receiver"         Dl07 "Builtin/SerdesReceiver.h"  SerdesReceiver)
dawlab_add_stage(DawLabEyeScope  "DAW_LAB Eye Scope"        Dl08 "Builtin/EyeScope.h"        EyeScope)
