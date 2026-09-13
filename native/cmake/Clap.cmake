add_subdirectory(clap)
add_subdirectory(clap-wrapper)

if(NOT ELEM_DEV_LOCALHOST)
  set(SRVB_RESOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../dist")
  if(NOT EXISTS "${SRVB_RESOURCE_DIRECTORY}/dsp.main.js")
    message(FATAL_ERROR "Build the JavaScript assets before configuring the CLAP target.")
  endif()
  if(NOT EXISTS "${SRVB_RESOURCE_DIRECTORY}/index.html")
    message(FATAL_ERROR "Run pnpm run build-ui before configuring the release CLAP target.")
  endif()
endif()

add_library(srvb_clap_impl STATIC ClapEditor.cpp ClapPlugin.cpp)
set_target_properties(srvb_clap_impl PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(srvb_clap_impl PUBLIC cxx_std_17)
target_compile_definitions(srvb_clap_impl PRIVATE ELEM_DEV_LOCALHOST=$<BOOL:${ELEM_DEV_LOCALHOST}>)
target_include_directories(srvb_clap_impl PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/choc/gui
  ${CMAKE_CURRENT_SOURCE_DIR}/choc/text)
target_link_libraries(srvb_clap_impl PUBLIC clap PRIVATE dsp_engine)

if(APPLE)
  target_link_libraries(srvb_clap_impl PRIVATE "-framework WebKit")
endif()

set(SRVB_CLAP_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/SRVB_artefacts/${CMAKE_BUILD_TYPE}")
make_clapfirst_plugins(
  TARGET_NAME SRVB
  IMPL_TARGET srvb_clap_impl
  OUTPUT_NAME SRVB
  ENTRY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ClapEntry.cpp"
  BUNDLE_IDENTIFIER "audio.elementary.srvb"
  BUNDLE_VERSION "${PROJECT_VERSION}"
  COPY_AFTER_BUILD FALSE
  PLUGIN_FORMATS CLAP VST3
  WINDOWS_FOLDER_VST3 TRUE
  ASSET_OUTPUT_DIRECTORY "${SRVB_CLAP_OUTPUT_DIRECTORY}"
  RESOURCE_DIRECTORY "${SRVB_RESOURCE_DIRECTORY}")

if(WIN32 AND NOT ELEM_DEV_LOCALHOST)
  foreach(plugin_target IN ITEMS SRVB_clap SRVB_vst3)
    add_custom_command(TARGET ${plugin_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${SRVB_RESOURCE_DIRECTORY}"
        "$<TARGET_FILE_DIR:${plugin_target}>/SRVB.resources"
      VERBATIM)
  endforeach()
endif()

if(APPLE)
  add_custom_command(TARGET SRVB_clap POST_BUILD
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:SRVB_clap>"
    VERBATIM)
  add_custom_command(TARGET SRVB_vst3 POST_BUILD
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:SRVB_vst3>"
    VERBATIM)
endif()
