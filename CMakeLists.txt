cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(CTest)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core Network)
  if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_wrap_opengl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_wrap_opengl_libs)
      list(FILTER _wrap_opengl_libs EXCLUDE REGEX "AGL")
      set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_wrap_opengl_libs}")
    endif()
  endif()
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/ffmpeg-pipe.cpp
    src/ffmpeg-pipe.hpp
    src/plugin-main.cpp
    src/radio-dock.cpp
    src/radio-dock.hpp
    src/radio-output.cpp
    src/radio-output.hpp
    src/radio-settings-data.cpp
    src/radio-settings.cpp
    src/radio-settings.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(BUILD_TESTING)
  add_executable(radio-settings-tests)
  set(_radio_settings_test_rpath "$<TARGET_FILE_DIR:Qt6::Core>")
  if(APPLE)
    list(APPEND _radio_settings_test_rpath "$<TARGET_FILE_DIR:Qt6::Core>/../../..")
  endif()
  set_target_properties(
    radio-settings-tests
    PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON BUILD_RPATH "${_radio_settings_test_rpath}"
  )
  target_sources(radio-settings-tests PRIVATE src/radio-settings.cpp tests/radio-settings-tests.cpp)
  target_include_directories(radio-settings-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(radio-settings-tests PRIVATE Qt6::Core)
  if(APPLE)
    get_filename_component(_qt_framework_rpath "${Qt6_DIR}/../.." ABSOLUTE)
    target_link_options(radio-settings-tests PRIVATE "LINKER:-rpath,${_qt_framework_rpath}")
    set_target_properties(
      radio-settings-tests
      PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME NO
        XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS ""
    )
  endif()
  add_test(NAME radio-settings COMMAND $<TARGET_FILE:radio-settings-tests>)
  if(WIN32)
    set_tests_properties(
      radio-settings
      PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
  endif()
endif()
