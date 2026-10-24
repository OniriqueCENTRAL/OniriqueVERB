cmake_minimum_required(VERSION 3.22)

# Mac : binaire universel (Intel + Apple Silicon), à définir avant project()
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(OniriqueReverb VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE : la copie partagée de ONIRIQUE PLUGINS/_juce si elle est là, sinon téléchargée au premier configure.
if(NOT FETCHCONTENT_SOURCE_DIR_JUCE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../_juce/8.0.6/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_JUCE "${CMAKE_CURRENT_SOURCE_DIR}/../_juce/8.0.6")
endif()
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# Logo et polices de la charte ONIRIQUE, intégrés au binaire.
juce_add_binary_data(OniriqueReverbData SOURCES
    Source/GUI/Assets/sigil.png
    Source/GUI/Assets/Unbounded800.ttf
    Source/GUI/Assets/GrenzeGotisch600.ttf
    Source/GUI/Assets/HankenGrotesk600.ttf
    Source/GUI/Assets/JetBrainsMono500.ttf
    Source/GUI/Assets/JetBrainsMono600.ttf)
set_target_properties(OniriqueReverbData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# AU (Logic, GarageBand) uniquement sur Mac
set(ONIRIQUE_FORMATS VST3)
if(APPLE)
    list(APPEND ONIRIQUE_FORMATS AU)
endif()

# Version d'essai locale, pour régler le son dans un DAW avant de publier : un autre nom et un autre
# identifiant (elle s'installe à côté de la vraie sans la remplacer), et sans licence.
#   cmake -B build-test -DONIRIQUE_TEST_BUILD=ON
option(ONIRIQUE_TEST_BUILD "Version d'essai locale : ONIRIQUE VERB TEST, sans licence" OFF)
if(ONIRIQUE_TEST_BUILD)
    set(ONIRIQUE_REVERB_NAME "ONIRIQUE VERB TEST")
    set(ONIRIQUE_REVERB_CODE Orvt)
else()
    set(ONIRIQUE_REVERB_NAME "ONIRIQUE VERB")
    set(ONIRIQUE_REVERB_CODE Orv1)
endif()

juce_add_plugin(OniriqueReverb
    COMPANY_NAME                Onirique
    PLUGIN_MANUFACTURER_CODE    Onrq
    PLUGIN_CODE                 ${ONIRIQUE_REVERB_CODE}
    FORMATS                     ${ONIRIQUE_FORMATS}
    AU_MAIN_TYPE                kAudioUnitType_Effect
    PRODUCT_NAME                "${ONIRIQUE_REVERB_NAME}"
    DESCRIPTION                 "Reverb pour la voix"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Reverb
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(OniriqueReverb PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(OniriqueReverb PRIVATE Source)

# Licence ONIRIQUE : le dossier License/ est le même dans tous les plugins (voir License/README.md).
include(License/OniriqueLicense.cmake)
onirique_add_license(OniriqueReverb)

target_compile_definitions(OniriqueReverb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    ONIRIQUE_TEST_BUILD=$<BOOL:${ONIRIQUE_TEST_BUILD}>)

# Mac : rendu Metal qui ne redessine que les zones animées (voir VOCAL SPACER)
if(APPLE)
    target_compile_definitions(OniriqueReverb PUBLIC JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1)
endif()

if(MSVC)
    target_compile_options(OniriqueReverb PRIVATE /utf-8)
endif()

target_link_libraries(OniriqueReverb
    PRIVATE
        OniriqueReverbData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
#  Outil de développement : rend l'interface dans une image PNG, sans DAW.
#      cmake --build build --config Release --target OniriqueReverbSnapshot
# ---------------------------------------------------------------------------
add_executable(OniriqueReverbSnapshot EXCLUDE_FROM_ALL Tools/Snapshot.cpp)
target_include_directories(OniriqueReverbSnapshot PRIVATE
    Source
    $<TARGET_PROPERTY:OniriqueReverb,INCLUDE_DIRECTORIES>)
target_compile_definitions(OniriqueReverbSnapshot PRIVATE
    $<TARGET_PROPERTY:OniriqueReverb,COMPILE_DEFINITIONS>)
target_link_libraries(OniriqueReverbSnapshot PRIVATE OniriqueReverb)

# ---------------------------------------------------------------------------
#  Banc d'essai du moteur (durées de queue, INFINITE, stabilité, CLEAN, PULSE, DUCKING) ;
#  avec un fichier .wav en argument, il écrit aussi la voix passée dans chaque mode.
#      cmake --build build --config Release --target OniriqueReverbEngineTest
# ---------------------------------------------------------------------------
juce_add_console_app(OniriqueReverbEngineTest PRODUCT_NAME "OniriqueReverbEngineTest")
set_target_properties(OniriqueReverbEngineTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(OniriqueReverbEngineTest PRIVATE Tools/EngineTest.cpp)
target_include_directories(OniriqueReverbEngineTest PRIVATE Source)
target_compile_definitions(OniriqueReverbEngineTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(OniriqueReverbEngineTest PRIVATE juce::juce_audio_formats juce::juce_recommended_config_flags)

# ---------------------------------------------------------------------------
#  Outil de développement : petit hôte VST3 qui charge le fichier .vst3 comme un DAW
#  et vérifie nom, paramètres, latence, mono / stéréo, sauvegarde, fenêtre.
#      cmake --build build --config Release --target OniriqueReverbHostTest
# ---------------------------------------------------------------------------
juce_add_console_app(OniriqueReverbHostTest PRODUCT_NAME "OniriqueReverbHostTest")
set_target_properties(OniriqueReverbHostTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(OniriqueReverbHostTest PRIVATE Tools/HostTest.cpp)
target_compile_definitions(OniriqueReverbHostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(OniriqueReverbHostTest PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)

if(MSVC)
    target_compile_options(OniriqueReverbSnapshot PRIVATE /utf-8)
    target_compile_options(OniriqueReverbEngineTest PRIVATE /utf-8)
    target_compile_options(OniriqueReverbHostTest PRIVATE /utf-8)
endif()
