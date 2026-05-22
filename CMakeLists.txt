cmake_minimum_required(VERSION 3.22)
project(OndesMartinot VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── JUCE via FetchContent ────────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_MakeAvailable(JUCE)

# ─── Plugin target ────────────────────────────────────────────────────────────
juce_add_plugin(OndesMartinot
    COMPANY_NAME            "YourName"
    PLUGIN_MANUFACTURER_CODE Mnfr
    PLUGIN_CODE             Ondm
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE          # автокопирование в ~/Library/Audio/Plug-Ins/VST3
    FORMATS                 VST3 Standalone
    PRODUCT_NAME            "Ondes Martinot"
)

# Генерируем JuceHeader.h (нужен для #include <JuceHeader.h> в исходниках)
juce_generate_juce_header(OndesMartinot)

# ─── Sources ─────────────────────────────────────────────────────────────────
target_sources(OndesMartinot
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

# ─── Defines ─────────────────────────────────────────────────────────────────
target_compile_definitions(OndesMartinot
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

# ─── Link ─────────────────────────────────────────────────────────────────────
target_link_libraries(OndesMartinot
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
