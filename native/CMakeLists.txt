cmake_minimum_required(VERSION 3.15)
project(Elem_SRVB VERSION 0.1.0)

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

option(ELEM_DEV_LOCALHOST "Run against localhost for static assets" OFF)

add_subdirectory(elementary/runtime)

add_library(dsp_engine STATIC DspEngine.cpp)
set_target_properties(dsp_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(dsp_engine PUBLIC cxx_std_17)
target_include_directories(dsp_engine PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/choc/javascript)
target_link_libraries(dsp_engine PUBLIC runtime)

include(cmake/Clap.cmake)
