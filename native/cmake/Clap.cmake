add_subdirectory(clap)
add_subdirectory(clap-wrapper)

set(SRVB_RESOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../public")
if(NOT EXISTS "${SRVB_RESOURCE_DIRECTORY}/dsp.main.js")
  message(FATAL_ERROR "Run pnpm run build-dsp before configuring the CLAP target.")
endif()

add_library(srvb_clap_impl STATIC ClapPlugin.cpp)
set_target_properties(srvb_clap_impl PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(srvb_clap_impl PUBLIC cxx_std_17)
target_link_libraries(srvb_clap_impl PUBLIC clap PRIVATE dsp_engine)

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

if(APPLE)
  add_custom_command(TARGET SRVB_clap POST_BUILD
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:SRVB_clap>"
    VERBATIM)
  add_custom_command(TARGET SRVB_vst3 POST_BUILD
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:SRVB_vst3>"
    VERBATIM)
endif()
