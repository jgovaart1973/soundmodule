cmake_minimum_required(VERSION 3.22)

project(KlankModule VERSION 0.1.0 LANGUAGES C CXX)

# C++17 for everything: JUCE 8 and sfizz both build with it (sfizz breaks on C++20's char8_t).
# Set as a cache entry too, because sfizz declares CMAKE_CXX_STANDARD as a cache variable.
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard" FORCE)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Static MSVC runtime (/MT) for every target: sfizz forces it for itself, and mixing it with the
# default /MD fails to link. Bonus: the .exe runs without installing the VC++ redistributable.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(KM_WITH_SFIZZ  "Build the SFZ sampler engine (sfizz)" ON)
option(KM_BUILD_TESTS "Build the unit tests (Catch2)" ON)
set(KM_ASIO_SDK_DIR "" CACHE PATH
    "Folder of the Steinberg ASIO SDK (the folder that contains 'common'). Empty = no ASIO, WASAPI only.")
# On Linux we only build to test the code; real audio I/O there needs ALSA/JACK headers.
option(KM_LINUX_AUDIO_IO "Enable ALSA/JACK audio I/O on Linux" OFF)

# ---------------------------------------------------------------------------
# Dependencies (downloaded automatically by CMake; pinned versions)
#   Offline? Pass -DFETCHCONTENT_SOURCE_DIR_JUCE=<path> etc.
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.17-security
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(juce)

if(KM_WITH_SFIZZ)
    set(SFIZZ_JACK    OFF CACHE BOOL "" FORCE)
    set(SFIZZ_RENDER  OFF CACHE BOOL "" FORCE)
    set(SFIZZ_SHARED  OFF CACHE BOOL "" FORCE)
    set(SFIZZ_TESTS   OFF CACHE BOOL "" FORCE)
    set(SFIZZ_DEMOS   OFF CACHE BOOL "" FORCE)
    set(SFIZZ_DEVTOOLS OFF CACHE BOOL "" FORCE)
    set(SFIZZ_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(ENABLE_LTO    OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(sfizz
        GIT_REPOSITORY https://github.com/sfztools/sfizz.git
        GIT_TAG        1.2.3
        GIT_SHALLOW    ON
        GIT_SUBMODULES_RECURSE ON)
    FetchContent_MakeAvailable(sfizz)
endif()

# ---------------------------------------------------------------------------
# km_core: everything except the window/app entry points.
# JUCE modules are linked PRIVATE and their include dirs/defines are re-exported,
# so the GUI app, the render tool and the tests can share one compiled copy.
# ---------------------------------------------------------------------------
add_library(km_core STATIC)

target_sources(km_core PRIVATE
    Source/Core/AppPaths.cpp
    Source/Engine/BuiltInSynth.cpp
    Source/Engine/SfzInstrument.cpp
    Source/Engine/PluginInstrument.cpp
    Source/Engine/InstrumentPool.cpp
    Source/Engine/Part.cpp
    Source/Engine/Effects.cpp
    Source/Engine/ModuleEngine.cpp
    Source/Midi/MidiRouter.cpp
    Source/Midi/MidiLearn.cpp
    Source/Model/Performance.cpp
    Source/Model/ProgramMap.cpp
    Source/Plugins/PluginScanner.cpp
    Source/App/AppController.cpp
    Source/Render/OfflineRenderer.cpp
    Source/UI/KmLookAndFeel.cpp
    Source/UI/LearnableSlider.cpp
    Source/UI/PartRow.cpp
    Source/UI/MasterPanel.cpp
    Source/UI/TopBar.cpp
    Source/UI/SettingsComponent.cpp
    Source/UI/MainComponent.cpp)

target_include_directories(km_core PUBLIC Source)

target_compile_definitions(km_core PUBLIC
    KM_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

if(KM_ASIO_SDK_DIR)
    message(STATUS "KlankModule: ASIO enabled, SDK at ${KM_ASIO_SDK_DIR}")
    target_compile_definitions(km_core PUBLIC JUCE_ASIO=1)
    target_include_directories(km_core PUBLIC "${KM_ASIO_SDK_DIR}/common")
else()
    message(STATUS "KlankModule: ASIO disabled (set KM_ASIO_SDK_DIR to enable). WASAPI will be used.")
    target_compile_definitions(km_core PUBLIC JUCE_ASIO=0)
endif()

if(UNIX AND NOT APPLE AND NOT KM_LINUX_AUDIO_IO)
    target_compile_definitions(km_core PUBLIC JUCE_ALSA=0 JUCE_JACK=0)
endif()

if(KM_WITH_SFIZZ)
    target_compile_definitions(km_core PUBLIC KM_WITH_SFIZZ=1)
    target_link_libraries(km_core PRIVATE sfizz::static)
    target_include_directories(km_core PRIVATE "${sfizz_SOURCE_DIR}/src")
else()
    target_compile_definitions(km_core PUBLIC KM_WITH_SFIZZ=0)
endif()

target_link_libraries(km_core
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Re-export JUCE include paths and defines to everything that links km_core.
target_compile_definitions(km_core INTERFACE $<TARGET_PROPERTY:km_core,COMPILE_DEFINITIONS>)
target_include_directories(km_core INTERFACE $<TARGET_PROPERTY:km_core,INCLUDE_DIRECTORIES>)
set_target_properties(km_core PROPERTIES
    POSITION_INDEPENDENT_CODE TRUE
    VISIBILITY_INLINES_HIDDEN TRUE
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

if(MSVC)
    target_compile_options(km_core PUBLIC /MP /utf-8)
endif()

# ---------------------------------------------------------------------------
# The application
# ---------------------------------------------------------------------------
juce_add_gui_app(KlankModule
    PRODUCT_NAME "KlankModule"
    COMPANY_NAME "KlankModule"
    VERSION ${PROJECT_VERSION}
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png")

target_sources(KlankModule PRIVATE Source/Main.cpp)
target_link_libraries(KlankModule PRIVATE km_core)

# Headless renderer: MIDI file + performance -> WAV (used for automated listening tests)
juce_add_console_app(KlankModuleRender PRODUCT_NAME "KlankModuleRender")
target_sources(KlankModuleRender PRIVATE Source/RenderMain.cpp)
target_link_libraries(KlankModuleRender PRIVATE km_core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(KM_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(Catch2)

    add_executable(km_tests
        Tests/TestPartFilter.cpp
        Tests/TestMidiQueue.cpp
        Tests/TestPerformanceJson.cpp
        Tests/TestProgramMap.cpp
        Tests/TestEngine.cpp
        Tests/TestAppController.cpp)
    target_link_libraries(km_tests PRIVATE km_core Catch2::Catch2WithMain)

    enable_testing() # not include(CTest): a dependency sets BUILD_TESTING=OFF
    include("${catch2_SOURCE_DIR}/extras/Catch.cmake")
    catch_discover_tests(km_tests)
endif()
