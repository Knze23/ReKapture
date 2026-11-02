cmake_minimum_required(VERSION 3.28)

# Solo usamos RelWithDebInfo: coincide con la compilacion de OBS que ya tienes.
set(CMAKE_CONFIGURATION_TYPES "RelWithDebInfo" CACHE STRING "Configuraciones disponibles" FORCE)

project(ReKapture VERSION 0.6.0 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ---------------------------------------------------------------------------
# Rutas de OBS (puedes cambiarlas con -DOBS_SOURCE_DIR=...)
# ---------------------------------------------------------------------------
set(OBS_SOURCE_DIR "D:/Dev/obs-studio" CACHE PATH "Carpeta del codigo fuente de OBS")
set(OBS_BUILD_DIR  "${OBS_SOURCE_DIR}/build_x64" CACHE PATH "Carpeta de compilacion de OBS")
set(OBS_CONFIG     "RelWithDebInfo")
set(OBS_RUNDIR     "${OBS_BUILD_DIR}/rundir/${OBS_CONFIG}")

foreach(_required
        "${OBS_SOURCE_DIR}/libobs/obs.h"
        "${OBS_BUILD_DIR}/libobs/${OBS_CONFIG}/obs.lib"
        "${OBS_RUNDIR}/bin/64bit/obs.dll")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "No se encuentra: ${_required}\n"
                            "Comprueba OBS_SOURCE_DIR y que OBS este compilado en ${OBS_CONFIG}.")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Qt 6: usamos el Qt precompilado que OBS descargo en .deps
# ---------------------------------------------------------------------------
file(GLOB _qt_dirs   "${OBS_SOURCE_DIR}/.deps/obs-deps-qt6-*-x64")
file(GLOB _deps_dirs "${OBS_SOURCE_DIR}/.deps/obs-deps-2*-x64")
list(SORT _qt_dirs   ORDER DESCENDING)
list(SORT _deps_dirs ORDER DESCENDING)
if(NOT _qt_dirs)
    message(FATAL_ERROR "No hay Qt6 en ${OBS_SOURCE_DIR}/.deps (obs-deps-qt6-*-x64).")
endif()
list(GET _qt_dirs 0 OBS_QT_DIR)
list(APPEND CMAKE_PREFIX_PATH "${OBS_QT_DIR}")
if(_deps_dirs)
    list(GET _deps_dirs 0 OBS_DEPS_DIR)
    list(APPEND CMAKE_PREFIX_PATH "${OBS_DEPS_DIR}")
endif()
message(STATUS "Qt6 de OBS: ${OBS_QT_DIR}")

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# ---------------------------------------------------------------------------
# libobs como biblioteca importada
# ---------------------------------------------------------------------------
find_path(OBS_CONFIG_INCLUDE_DIR obsconfig.h
          PATHS "${OBS_BUILD_DIR}/config" "${OBS_BUILD_DIR}/libobs"
          NO_DEFAULT_PATH)
set(_obs_includes "${OBS_SOURCE_DIR}/libobs")
if(OBS_CONFIG_INCLUDE_DIR)
    list(APPEND _obs_includes "${OBS_CONFIG_INCLUDE_DIR}")
    message(STATUS "obsconfig.h en: ${OBS_CONFIG_INCLUDE_DIR}")
else()
    message(STATUS "obsconfig.h no encontrado (puede no hacer falta)")
endif()

add_library(OBS::libobs SHARED IMPORTED)
set_target_properties(OBS::libobs PROPERTIES
    IMPORTED_IMPLIB   "${OBS_BUILD_DIR}/libobs/${OBS_CONFIG}/obs.lib"
    IMPORTED_LOCATION "${OBS_RUNDIR}/bin/64bit/obs.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${_obs_includes}")

# ---------------------------------------------------------------------------
# Aplicacion
# ---------------------------------------------------------------------------
# Icono del ejecutable e informacion de version (recurso de Windows)
set(REKAPTURE_VERSION "${PROJECT_VERSION}")
set(REKAPTURE_VER_MAJOR "${PROJECT_VERSION_MAJOR}")
set(REKAPTURE_VER_MINOR "${PROJECT_VERSION_MINOR}")
set(REKAPTURE_VER_PATCH "${PROJECT_VERSION_PATCH}")
set(REKAPTURE_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/rekapture.ico")
configure_file(resources/rekapture.rc.in "${CMAKE_BINARY_DIR}/rekapture.rc" @ONLY)

add_executable(ReKapture WIN32
    src/main.cpp
    resources/resources.qrc
    ${CMAKE_BINARY_DIR}/rekapture.rc
    src/log/Logger.h
    src/log/Logger.cpp
    src/obs/ObsEngine.h
    src/obs/ObsEngine.cpp
    src/audio/SoundCues.h
    src/audio/SoundCues.cpp
    src/capture/GameNaming.h
    src/capture/GameNaming.cpp
    src/config/AppPaths.h
    src/config/AppPaths.cpp
    src/config/AppSettings.h
    src/config/AppSettings.cpp
    src/hotkeys/HotkeyManager.h
    src/hotkeys/HotkeyManager.cpp
    src/recording/AudioFileWriter.h
    src/recording/AudioFileWriter.cpp
    src/recording/EncoderCatalog.h
    src/recording/EncoderCatalog.cpp
    src/recording/RecordingController.h
    src/recording/RecordingController.cpp
    src/library/ShellMedia.h
    src/library/ShellMedia.cpp
    src/library/VideoLibrary.h
    src/library/VideoLibrary.cpp
    src/ui/ActivityOverlay.h
    src/ui/ActivityOverlay.cpp
    src/ui/LogPage.h
    src/ui/LogPage.cpp
    src/ui/EffectsDialog.h
    src/ui/EffectsDialog.cpp
    src/ui/PropertiesPanel.h
    src/ui/PropertiesPanel.cpp
    src/ui/LibraryPage.h
    src/ui/LibraryPage.cpp
    src/ui/LevelMeter.h
    src/ui/LevelMeter.cpp
    src/ui/PreviewWidget.h
    src/ui/PreviewWidget.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)

target_link_libraries(ReKapture PRIVATE Qt6::Widgets OBS::libobs shell32 ole32 gdi32 user32 mfplat mfreadwrite mfuuid)
target_compile_definitions(ReKapture PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(ReKapture PRIVATE /W4 /utf-8 /permissive-)
endif()

# El ejecutable queda en build/deploy/bin/64bit, con la misma estructura que OBS
set(REKAPTURE_DEPLOY_DIR "${CMAKE_BINARY_DIR}/deploy")
set_target_properties(ReKapture PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${REKAPTURE_DEPLOY_DIR}/bin/64bit>"
    VS_DEBUGGER_WORKING_DIRECTORY "${REKAPTURE_DEPLOY_DIR}/bin/64bit")

# Copia el runtime de OBS (DLL, Qt, modulos, datos) junto al ejecutable
add_custom_command(TARGET ReKapture POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${OBS_RUNDIR}" "${REKAPTURE_DEPLOY_DIR}"
    COMMENT "Copiando el runtime de OBS a ${REKAPTURE_DEPLOY_DIR}")
