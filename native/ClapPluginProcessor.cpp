#include "ClapPluginProcessor.h"

#include <clap/helpers/plugin.hxx>

namespace
{
const char* const clapPluginFeatures[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_REVERB,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};
}

ClapPluginProcessor::ClapPluginProcessor(const clap_host* host)
    : Plugin(descriptor(), host)
{
}

const clap_plugin_descriptor* ClapPluginProcessor::descriptor()
{
    static const clap_plugin_descriptor desc {
        CLAP_VERSION,
        "audio.elementary.srvb",
        "SRVB",
        "Elementary Audio",
        "https://www.elementary.audio",
        "",
        "",
        "0.1.0",
        "",
        clapPluginFeatures,
    };

    return &desc;
}

bool ClapPluginProcessor::init() noexcept { return false; }
bool ClapPluginProcessor::activate(double, uint32_t, uint32_t) noexcept { return false; }
void ClapPluginProcessor::deactivate() noexcept {}
bool ClapPluginProcessor::startProcessing() noexcept { return false; }
void ClapPluginProcessor::stopProcessing() noexcept {}
clap_process_status ClapPluginProcessor::process(const clap_process*) noexcept { return CLAP_PROCESS_ERROR; }
void ClapPluginProcessor::reset() noexcept {}
void ClapPluginProcessor::onMainThread() noexcept {}
const void* ClapPluginProcessor::extension(const char*) noexcept { return nullptr; }
bool ClapPluginProcessor::enableDraftExtensions() const noexcept { return false; }

bool ClapPluginProcessor::implementsAudioPorts() const noexcept { return false; }
uint32_t ClapPluginProcessor::audioPortsCount(bool) const noexcept { return 0; }
bool ClapPluginProcessor::audioPortsInfo(uint32_t, bool, clap_audio_port_info*) const noexcept { return false; }

bool ClapPluginProcessor::implementsParams() const noexcept { return false; }
uint32_t ClapPluginProcessor::paramsCount() const noexcept { return 0; }
bool ClapPluginProcessor::paramsInfo(uint32_t, clap_param_info*) const noexcept { return false; }
bool ClapPluginProcessor::paramsValue(clap_id, double*) noexcept { return false; }
bool ClapPluginProcessor::paramsValueToText(clap_id, double, char*, uint32_t) noexcept { return false; }
bool ClapPluginProcessor::paramsTextToValue(clap_id, const char*, double*) noexcept { return false; }
void ClapPluginProcessor::paramsFlush(const clap_input_events*, const clap_output_events*) noexcept {}

bool ClapPluginProcessor::implementsState() const noexcept { return false; }
bool ClapPluginProcessor::stateSave(const clap_ostream*) noexcept { return false; }
bool ClapPluginProcessor::stateLoad(const clap_istream*) noexcept { return false; }

bool ClapPluginProcessor::implementsGui() const noexcept { return false; }
bool ClapPluginProcessor::guiIsApiSupported(const char*, bool) noexcept { return false; }
bool ClapPluginProcessor::guiGetPreferredApi(const char**, bool*) noexcept { return false; }
bool ClapPluginProcessor::guiCreate(const char*, bool) noexcept { return false; }
void ClapPluginProcessor::guiDestroy() noexcept {}
bool ClapPluginProcessor::guiSetScale(double) noexcept { return false; }
bool ClapPluginProcessor::guiShow() noexcept { return false; }
bool ClapPluginProcessor::guiHide() noexcept { return false; }
bool ClapPluginProcessor::guiGetSize(uint32_t*, uint32_t*) noexcept { return false; }
bool ClapPluginProcessor::guiCanResize() const noexcept { return false; }
bool ClapPluginProcessor::guiGetResizeHints(clap_gui_resize_hints_t*) noexcept { return false; }
bool ClapPluginProcessor::guiAdjustSize(uint32_t*, uint32_t*) noexcept { return false; }
bool ClapPluginProcessor::guiSetSize(uint32_t, uint32_t) noexcept { return false; }
void ClapPluginProcessor::guiSuggestTitle(const char*) noexcept {}
bool ClapPluginProcessor::guiSetParent(const clap_window*) noexcept { return false; }
bool ClapPluginProcessor::guiSetTransient(const clap_window*) noexcept { return false; }

bool ClapPluginProcessor::implementsWebview() const noexcept { return false; }
int32_t ClapPluginProcessor::webviewGetUri(char*, uint32_t) const noexcept { return 0; }
bool ClapPluginProcessor::webviewGetResource(const char*,
                                             char*,
                                             uint32_t,
                                             const clap_ostream_t*)
{
    return false;
}
bool ClapPluginProcessor::webviewReceive(const void*, uint32_t) const noexcept { return false; }
