#pragma once

#include <clap/helpers/plugin.hh>

class ClapPluginProcessor
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                   clap::helpers::CheckingLevel::Maximal>
{
public:
    explicit ClapPluginProcessor(const clap_host* host);
    ~ClapPluginProcessor() override = default;

protected:
    bool init() noexcept override;
    bool activate(double sampleRate, uint32_t minFrameCount, uint32_t maxFrameCount) noexcept override;
    void deactivate() noexcept override;
    bool startProcessing() noexcept override;
    void stopProcessing() noexcept override;
    clap_process_status process(const clap_process* process) noexcept override;
    void reset() noexcept override;
    void onMainThread() noexcept override;
    const void* extension(const char* id) noexcept override;
    bool enableDraftExtensions() const noexcept override;

    bool implementsAudioPorts() const noexcept override;
    uint32_t audioPortsCount(bool isInput) const noexcept override;
    bool audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info* info) const noexcept override;

    bool implementsParams() const noexcept override;
    uint32_t paramsCount() const noexcept override;
    bool paramsInfo(uint32_t paramIndex, clap_param_info* info) const noexcept override;
    bool paramsValue(clap_id paramId, double* value) noexcept override;
    bool paramsValueToText(clap_id paramId, double value, char* display, uint32_t size) noexcept override;
    bool paramsTextToValue(clap_id paramId, const char* display, double* value) noexcept override;
    void paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept override;

    bool implementsState() const noexcept override;
    bool stateSave(const clap_ostream* stream) noexcept override;
    bool stateLoad(const clap_istream* stream) noexcept override;

    bool implementsGui() const noexcept override;
    bool guiIsApiSupported(const char* api, bool isFloating) noexcept override;
    bool guiGetPreferredApi(const char** api, bool* isFloating) noexcept override;
    bool guiCreate(const char* api, bool isFloating) noexcept override;
    void guiDestroy() noexcept override;
    bool guiSetScale(double scale) noexcept override;
    bool guiShow() noexcept override;
    bool guiHide() noexcept override;
    bool guiGetSize(uint32_t* width, uint32_t* height) noexcept override;
    bool guiCanResize() const noexcept override;
    bool guiGetResizeHints(clap_gui_resize_hints_t* hints) noexcept override;
    bool guiAdjustSize(uint32_t* width, uint32_t* height) noexcept override;
    bool guiSetSize(uint32_t width, uint32_t height) noexcept override;
    void guiSuggestTitle(const char* title) noexcept override;
    bool guiSetParent(const clap_window* window) noexcept override;
    bool guiSetTransient(const clap_window* window) noexcept override;

    bool implementsWebview() const noexcept override;
    int32_t webviewGetUri(char* uri, uint32_t uriCapacity) const noexcept override;
    bool webviewGetResource(const char* path,
                            char* mime,
                            uint32_t mimeCapacity,
                            const clap_ostream_t* dataStream) override;
    bool webviewReceive(const void* buffer, uint32_t size) const noexcept override;

private:
    static const clap_plugin_descriptor* descriptor();
};
