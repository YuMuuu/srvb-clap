#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <elem/Runtime.h>
#include <choc_javascript.h>

#include "parameters.hpp"

class ElementaryEngine
{
public:
    void setPluginPath (const char* pluginPath);
    void activate (double sampleRate, uint32_t maxFrames);
    void deactivate ();
    void initializeIfNeeded (ParameterSet& parameters);
    void dispatchStateIfNeeded (ParameterSet& parameters);
    void markStateDirty ();

    void process (const clap_process_t* process);

private:
    std::filesystem::path resolveDspEntryPath () const;
    std::string makeStatePayload (const ParameterSet& parameters) const;
    void initializeJavaScript (ParameterSet& parameters);
    void passthrough (const clap_process_t* process) const;

    std::filesystem::path pluginPath_;
    double sampleRate_ = 44100.0;
    uint32_t maxFrames_ = 0;
    std::atomic<bool> needsInitialization_{false};
    std::atomic<bool> needsStateDispatch_{true};
    std::shared_ptr<elem::Runtime<float>> runtime_;
    std::optional<choc::javascript::Context> jsContext_;
    std::vector<float> scratchInput_;
};
