# --- Detect if the plugin is built out of tree or not ---
if(CMAKE_PROJECT_NAME STREQUAL "obs-studio")
  set(BUILD_OUT_OF_TREE OFF)
else()
  set(BUILD_OUT_OF_TREE ON)
  cmake_minimum_required(VERSION 3.16...3.26)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${PROJECT_NAME} MODULE)

target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs)

if(BUILD_OUT_OF_TREE)
  find_package(libobs REQUIRED)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
else()
  target_link_libraries(${PROJECT_NAME} PRIVATE OBS::frontend-api)
endif()

# Qt — QPainter renders the label overlay; Network drives the feedback POST.
find_package(Qt6 COMPONENTS Widgets Core Gui Network REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt::Core Qt::Widgets Qt::Gui Qt::Network)

# Shared StreamUP UI design system (obs-streamup-ui-gallery), vendored under
# shared/obs-streamup-ui/ and compiled directly in (no shared sub-target). We
# only need the in-app feedback dialog here.
set(STREAMUP_UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shared/obs-streamup-ui")
target_include_directories(${PROJECT_NAME} PRIVATE
  "${STREAMUP_UI_DIR}/include"
  "${STREAMUP_UI_DIR}/include/streamup/ui"
)
target_sources(${PROJECT_NAME} PRIVATE
  "${STREAMUP_UI_DIR}/src/feedback.cpp"                       # shared feedback dialog
  "${STREAMUP_UI_DIR}/src/switch-button.cpp"                  # pulled in by feedback.cpp
  "${STREAMUP_UI_DIR}/include/streamup/ui/switch-button.hpp"  # Q_OBJECT -> AUTOMOC
  "${STREAMUP_UI_DIR}/include/streamup/ui/ios-checkbox.hpp"   # Q_OBJECT -> AUTOMOC
)

# Extra Qt linkage on older Qt versions for *nix
if((OS_LINUX OR OS_FREEBSD OR OS_OPENBSD) AND Qt_VERSION VERSION_LESS "6.9.0")
  target_link_libraries(${PROJECT_NAME} PRIVATE Qt::GuiPrivate)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(${PROJECT_NAME} PRIVATE
  $<$<C_COMPILER_ID:Clang,AppleClang>:
    -Wno-quoted-include-in-framework-header
    -Wno-comma
  >
)

set_target_properties(${PROJECT_NAME} PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC ON
  AUTOUIC ON
  AUTORCC ON
)

# Version header
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/version.h.in
  ${CMAKE_CURRENT_SOURCE_DIR}/version.h
)

# Windows installer script template
if(OS_WINDOWS)
  configure_file(
    cmake/windows/resources/installer-Windows.iss.in
    "${CMAKE_CURRENT_BINARY_DIR}/installer-Windows.generated.iss"
  )
endif()

# Sources
target_sources(${PROJECT_NAME} PRIVATE
  streamup-test-card.cpp
  test-card-filter.cpp
  test-card-filter.hpp
  test-card-overlay.cpp
  test-card-overlay.hpp
  version.h
)

# Install / properties depending on build context
if(BUILD_OUT_OF_TREE)
  set_target_properties_plugin(${PROJECT_NAME}
    PROPERTIES
      OUTPUT_NAME ${_name}
  )
else()
  set_target_properties_obs(${PROJECT_NAME}
    PROPERTIES
      FOLDER "plugins/streamup"
      PREFIX ""
  )
endif()
