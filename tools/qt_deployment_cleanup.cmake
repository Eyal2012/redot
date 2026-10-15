# Only explicit Basic controls are imported in production. The import scanner's
# before/after inventories confirm these former optional styles/Effects are unused.
# Move stale generated deployment artifacts to a recoverable project checkpoint;
# never touch the Qt SDK, application data, recordings, or historical builds.
file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}/.." project_root)
file(REAL_PATH "${DEPLOY_DIRECTORY}" deploy_root)
cmake_path(IS_PREFIX project_root "${deploy_root}" NORMALIZE in_project)
if(NOT in_project OR NOT deploy_root MATCHES "/build/.+/(Debug|Release|RelWithDebInfo|MinSizeRel)$")
  message(FATAL_ERROR "Refusing deployment cleanup outside an owned project build/configuration")
endif()
set(retired
  Qt6QuickControls2FluentWinUI3StyleImpl.dll Qt6QuickControls2Fusion.dll
  Qt6QuickControls2FusionStyleImpl.dll Qt6QuickControls2Imagine.dll
  Qt6QuickControls2ImagineStyleImpl.dll Qt6QuickControls2Material.dll
  Qt6QuickControls2MaterialStyleImpl.dll Qt6QuickControls2Universal.dll
  Qt6QuickControls2UniversalStyleImpl.dll Qt6QuickControls2WindowsStyleImpl.dll
  Qt6QuickEffects.dll
  qml/QtQuick/Controls/FluentWinUI3 qml/QtQuick/Controls/Fusion
  qml/QtQuick/Controls/Imagine qml/QtQuick/Controls/Material
  qml/QtQuick/Controls/Universal qml/QtQuick/Controls/Windows qml/QtQuick/Effects)
if(deploy_root MATCHES "/Debug$")
  string(REPLACE ".dll" "d.dll" retired "${retired}")
endif()
string(TIMESTAMP stamp "%Y%m%d-%H%M%S")
set(archive "${project_root}/build/checkpoints/retired-qt-${stamp}")
set(index 0)
while(EXISTS "${archive}-${index}")
  math(EXPR index "${index}+1")
endwhile()
set(archive "${archive}-${index}")
foreach(relative IN LISTS retired)
  set(source "${deploy_root}/${relative}")
  cmake_path(IS_PREFIX deploy_root "${source}" NORMALIZE contained)
  if(NOT contained)
    message(FATAL_ERROR "Invalid deployment cleanup target")
  endif()
  if(EXISTS "${source}")
    get_filename_component(parent "${archive}/${relative}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    file(RENAME "${source}" "${archive}/${relative}" RESULT result)
    if(NOT result STREQUAL "0")
      message(FATAL_ERROR "Unable to retire stale Qt deployment: ${source}: ${result}")
    endif()
    file(APPEND "${archive}/manifest.txt" "${source}\n")
  endif()
endforeach()
