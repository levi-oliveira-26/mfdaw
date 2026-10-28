cmake_minimum_required(VERSION 3.22)
project(MFDaw VERSION 0.1.0 LANGUAGES CXX)
enable_testing()
if(MSVC)
    add_compile_options(/bigobj /Zm200)
endif()
# ---------------------------------------------------------------------------
# Padrão de linguagem: C++20 obrigatório
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# ThreadSanitizer (CI): cmake -DMFDAW_ENABLE_TSAN=ON ...
# Aplicado globalmente (antes do JUCE) para que módulos e app usem a mesma flag.
# ---------------------------------------------------------------------------
option(MFDAW_ENABLE_TSAN "Compila com -fsanitize=thread (apenas Clang/GCC)" OFF)
if(MFDAW_ENABLE_TSAN AND NOT MSVC)
    add_compile_options(-fsanitize=thread -g -O1)
    add_link_options(-fsanitize=thread)
endif()

# ---------------------------------------------------------------------------
# JUCE via FetchContent (fixado em 8.0.4; funciona também com 7.x)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Alvo: plugin com formatos Standalone (DAW rodando como app) + VST3
# O Editor/Processor é o "host" da UI; no Standalone o JUCE fornece o main().
# ---------------------------------------------------------------------------
juce_add_plugin(MFDaw
    COMPANY_NAME                "MF"
    PRODUCT_NAME                "M.F. DAW"
    PLUGIN_MANUFACTURER_CODE    MfDw
    PLUGIN_CODE                 MfDa
    FORMATS                     Standalone VST3
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "A M.F. DAW precisa do microfone para gravar sua voz.")

target_sources(MFDaw PRIVATE
    Source/Core/ProjectState.cpp
    Source/Engine/MainProcessor.cpp
    Source/UI/StepGridComponent.cpp
    Source/UI/MainEditor.cpp)

target_include_directories(MFDaw PRIVATE Source)

target_compile_definitions(MFDaw PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# MSVC: fontes em UTF-8 (strings com acentos na UI)
target_compile_options(MFDaw PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

add_executable(MFDawTests
    Source/Tests/RunStepSequencerTests.cpp
    Source/Tests/StepSequencerTests.cpp
    Source/Core/ProjectState.cpp
    Source/Engine/MainProcessor.cpp
    Source/UI/StepGridComponent.cpp
    Source/UI/MainEditor.cpp)

target_include_directories(MFDawTests PRIVATE Source)
target_compile_options(MFDawTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_compile_definitions(MFDawTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(MFDawTests PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags)

add_test(NAME MFDaw_Rhythm_Tests COMMAND MFDawTests)

target_link_libraries(MFDaw
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
