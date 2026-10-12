cmake_minimum_required(VERSION 3.21)
project(FalloutBabylon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

if(MSVC)
    add_compile_options(/utf-8 /W3 /permissive-)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Core5Compat LinguistTools)

# ---------------------------------------------------------------- Core (no GUI)
add_library(babylon_core STATIC
    src/core/Lzss.cpp
    src/core/DatArchive.cpp
    src/core/DatWriter.cpp
    src/core/TextCodec.cpp
    src/core/MsgFile.cpp
    src/core/VirtualFileSystem.cpp
    src/core/Stemmer.cpp
    src/core/TermMatcher.cpp
    src/core/Glossary.cpp
    src/core/Project.cpp
    src/core/ProjectStore.cpp
    src/core/Statistics.cpp)
target_include_directories(babylon_core PUBLIC src)
target_link_libraries(babylon_core PUBLIC Qt6::Core)

option(BABYLON_BUILD_APP "Build the GUI application" ON)
if(BABYLON_BUILD_APP)
# ---------------------------------------------------------------- Scintilla
set(SCI ${CMAKE_CURRENT_SOURCE_DIR}/third_party/scintilla)
file(GLOB SCI_CORE_SOURCES CONFIGURE_DEPENDS ${SCI}/src/*.cxx)
add_library(scintilla_qt STATIC
    ${SCI_CORE_SOURCES}
    ${SCI}/qt/ScintillaEditBase/PlatQt.cpp
    ${SCI}/qt/ScintillaEditBase/ScintillaQt.cpp
    ${SCI}/qt/ScintillaEditBase/ScintillaEditBase.cpp
    ${SCI}/qt/ScintillaEditBase/PlatQt.h
    ${SCI}/qt/ScintillaEditBase/ScintillaQt.h
    ${SCI}/qt/ScintillaEditBase/ScintillaEditBase.h)
target_include_directories(scintilla_qt PUBLIC
    ${SCI}/include ${SCI}/src ${SCI}/qt/ScintillaEditBase)
# EXPORT_IMPORT_API empty: Scintilla is linked statically.
target_compile_definitions(scintilla_qt PUBLIC SCINTILLA_QT=1 EXPORT_IMPORT_API=)
target_compile_definitions(scintilla_qt PRIVATE _CRT_SECURE_NO_WARNINGS $<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>)
target_link_libraries(scintilla_qt PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Core5Compat)
set_target_properties(scintilla_qt PROPERTIES AUTOMOC ON)

# ---------------------------------------------------------------- Application
set(UI_SOURCES
    src/ui/Theme.cpp
    src/ui/SciEdit.cpp
    src/ui/MsgTableModel.cpp
    src/ui/TranslationEditor.cpp
    src/ui/ProjectTree.cpp
    src/ui/GlossaryPanel.cpp
    src/ui/StatsPanel.cpp
    src/ui/TodoPanel.cpp
    src/ui/ProjectDialog.cpp
    src/ui/MainWindow.cpp)

set(TS_FILES
    i18n/babylon_fr.ts
    i18n/babylon_es.ts
    i18n/babylon_de.ts
    i18n/babylon_it.ts
    i18n/babylon_pt.ts
    i18n/babylon_pl.ts
    i18n/babylon_cs.ts
    i18n/babylon_ru.ts
    i18n/babylon_zh.ts)

add_library(babylon_ui STATIC ${UI_SOURCES})
target_link_libraries(babylon_ui PUBLIC babylon_core scintilla_qt Qt6::Widgets)

qt_add_executable(FalloutBabylon WIN32 src/main.cpp resources/app.rc resources/icons.qrc)
set_source_files_properties(resources/app.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico)
qt_add_translations(FalloutBabylon TS_FILES ${TS_FILES} RESOURCE_PREFIX "/i18n")
target_link_libraries(FalloutBabylon PRIVATE babylon_ui)

# Offscreen smoke test of the whole window; writes screenshots when BABYLON_SHOT_DIR is set.
add_executable(babylon_gui_smoke tests/gui_smoke.cpp)
target_link_libraries(babylon_gui_smoke PRIVATE babylon_ui Qt6::Test)

endif()

# ---------------------------------------------------------------- Tests
option(BABYLON_BUILD_TESTS "Build unit tests" ON)
if(BABYLON_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(babylon_tests tests/tst_core.cpp)
    target_link_libraries(babylon_tests PRIVATE babylon_core Qt6::Test)
    target_compile_definitions(babylon_tests PRIVATE
        BABYLON_SAMPLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME core COMMAND babylon_tests)
endif()
