cmake_minimum_required(VERSION 3.21)
project(Elem_SRVB VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE AND (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 10.15))
  set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15 CACHE STRING "Minimum macOS version" FORCE)
endif()

if(APPLE)
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

set(TARGET_NAME SRVB)
set(PRODUCT_NAME "SRVB")
set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/../dist")
set(RESOURCE_DIRECTORY "")
set(SRVB_PLUGIN_FORMATS "CLAP" CACHE STRING "Plugin formats to build with clap-wrapper")
set(CLAP_SDK_ROOT "${CMAKE_SOURCE_DIR}/../external/clap" CACHE PATH "CLAP SDK root" FORCE)

option(ELEM_DEV_LOCALHOST "Run against localhost for static assets" OFF)
option(CLAP_WRAPPER_BUILD_TESTS "Build clap-wrapper tests" OFF)

add_subdirectory(elementary/runtime)
add_subdirectory(../external/clap-wrapper clap-wrapper)

if(EXISTS "${ASSETS_DIR}")
  set(RESOURCE_DIRECTORY "${ASSETS_DIR}")
else()
  message(WARNING "Asset directory ${ASSETS_DIR} does not exist; CLAP will be built without bundled DSP assets")
endif()

add_library(${TARGET_NAME}_impl STATIC
  elementary_engine.cpp
  parameters.cpp
  plugin.cpp)

target_compile_features(${TARGET_NAME}_impl
  PUBLIC
  cxx_std_17)

target_include_directories(${TARGET_NAME}_impl
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/choc/containers
  ${CMAKE_CURRENT_SOURCE_DIR}/choc/javascript
  ${CMAKE_CURRENT_SOURCE_DIR}/choc/text)

target_compile_definitions(${TARGET_NAME}_impl
  PRIVATE
  ELEM_DEV_LOCALHOST=${ELEM_DEV_LOCALHOST}
  SRVB_SOURCE_DIR="${CMAKE_SOURCE_DIR}/..")

target_link_libraries(${TARGET_NAME}_impl
  PUBLIC
  clap
  clap-wrapper-extensions
  elem::runtime)

if(UNIX AND NOT APPLE)
  find_package(Threads REQUIRED)
  target_link_libraries(${TARGET_NAME}_impl
    PUBLIC
    Threads::Threads
    ${CMAKE_DL_LIBS})
endif()

make_clapfirst_plugins(
  TARGET_NAME ${TARGET_NAME}
  IMPL_TARGET ${TARGET_NAME}_impl
  OUTPUT_NAME "${PRODUCT_NAME}"
  ENTRY_SOURCE entry.cpp
  BUNDLE_IDENTIFIER "audio.elementary.srvb"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  PLUGIN_FORMATS ${SRVB_PLUGIN_FORMATS}
  ASSET_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${TARGET_NAME}_artefacts/${CMAKE_BUILD_TYPE}"
  RESOURCE_DIRECTORY "${RESOURCE_DIRECTORY}"
  AUV2_MANUFACTURER_NAME "Elementary Audio"
  AUV2_MANUFACTURER_CODE "Elem"
  AUV2_SUBTYPE_CODE "Srvb"
  AUV2_INSTRUMENT_TYPE "aufx")
