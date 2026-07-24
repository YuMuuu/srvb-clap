#include "elementary_engine.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <sstream>

#include <choc_javascript_QuickJS.h>
#include <elem/Value.h>

#ifndef SRVB_SOURCE_DIR
#define SRVB_SOURCE_DIR ""
#endif

namespace
{
const auto* kConsoleShimScript = R"script(
(function() {
  globalThis.__postNativeMessage__ = function(payload) {
    return __postNativeMessage__(payload);
  };

  if (typeof globalThis.console === 'undefined') {
    globalThis.console = {
      log(...args) {
        return __log__('[embedded:log]', ...args);
      },
      warn(...args) {
        return __log__('[embedded:warn]', ...args);
      },
      error(...args) {
        return __log__('[embedded:error]', ...args);
      },
    };
  }
})();
)script";

std::string loadFile (const std::filesystem::path& path)
{
    std::ifstream input (path, std::ios::binary);
    if (!input)
        throw std::runtime_error ("Failed to open " + path.string ());

    std::ostringstream contents;
    contents << input.rdbuf ();
    return contents.str ();
}

void copyOrClear (float* output, const float* input, uint32_t frames)
{
    if (input != nullptr)
    {
        std::copy (input, input + frames, output);
        return;
    }

    std::fill (output, output + frames, 0.0f);
}
} // namespace

void ElementaryEngine::setPluginPath (const char* pluginPath)
{
    pluginPath_ = pluginPath != nullptr ? std::filesystem::path (pluginPath) : std::filesystem::path ();
}

void ElementaryEngine::activate (double sampleRate, uint32_t maxFrames)
{
    sampleRate_ = sampleRate;
    maxFrames_ = std::max<uint32_t> (1, maxFrames);
    scratchInput_.assign (static_cast<size_t> (maxFrames_) * 2, 0.0f);
    needsInitialization_.store (true, std::memory_order_release);
    needsStateDispatch_.store (true, std::memory_order_release);
}

void ElementaryEngine::deactivate ()
{
    jsContext_.reset ();
    std::atomic_store_explicit (&runtime_, std::shared_ptr<elem::Runtime<float>> (), std::memory_order_release);
    scratchInput_.clear ();
    needsInitialization_.store (true, std::memory_order_release);
}

void ElementaryEngine::initializeIfNeeded (ParameterSet& parameters)
{
    if (!needsInitialization_.exchange (false, std::memory_order_acq_rel))
        return;

    initializeJavaScript (parameters);
}

void ElementaryEngine::dispatchStateIfNeeded (ParameterSet& parameters)
{
    if (!jsContext_)
        return;

    const auto dirty = parameters.consumeDirty ();
    const auto requested = needsStateDispatch_.exchange (false, std::memory_order_acq_rel);

    if (!dirty && !requested)
        return;

    try
    {
        jsContext_->invoke ("__receiveStateChange__", makeStatePayload (parameters));
    }
    catch (const std::exception& e)
    {
        std::cerr << "SRVB: failed to dispatch DSP state: " << e.what () << std::endl;
    }
}

void ElementaryEngine::markStateDirty ()
{
    needsStateDispatch_.store (true, std::memory_order_release);
}

void ElementaryEngine::process (const clap_process_t* process)
{
    const auto runtime = std::atomic_load_explicit (&runtime_, std::memory_order_acquire);
    if (!runtime || process == nullptr || process->audio_outputs_count == 0 || process->audio_outputs == nullptr)
    {
        passthrough (process);
        return;
    }

    auto& output = process->audio_outputs[0];
    if (output.channel_count < 2 || output.data32 == nullptr || output.data32[0] == nullptr ||
        output.data32[1] == nullptr)
    {
        passthrough (process);
        return;
    }

    if (scratchInput_.size () < static_cast<size_t> (process->frames_count) * 2)
    {
        passthrough (process);
        return;
    }

    const float* inputLeft = nullptr;
    const float* inputRight = nullptr;
    if (process->audio_inputs_count > 0 && process->audio_inputs != nullptr)
    {
        const auto& input = process->audio_inputs[0];
        if (input.channel_count >= 2 && input.data32 != nullptr)
        {
            inputLeft = input.data32[0];
            inputRight = input.data32[1];
        }
    }

    auto* scratchLeft = scratchInput_.data ();
    auto* scratchRight = scratchInput_.data () + process->frames_count;
    copyOrClear (scratchLeft, inputLeft, process->frames_count);
    copyOrClear (scratchRight, inputRight, process->frames_count);

    const float* inputs[2]{scratchLeft, scratchRight};
    std::array<float*, 2> outputs{output.data32[0], output.data32[1]};
    std::fill (outputs[0], outputs[0] + process->frames_count, 0.0f);
    std::fill (outputs[1], outputs[1] + process->frames_count, 0.0f);

    runtime->process (inputs, 2, outputs.data (), outputs.size (), process->frames_count, nullptr);
}

std::filesystem::path ElementaryEngine::resolveDspEntryPath () const
{
#if ELEM_DEV_LOCALHOST
    return std::filesystem::path (SRVB_SOURCE_DIR) / "public" / "dsp.main.js";
#else
    const auto pluginDir = pluginPath_.empty () ? std::filesystem::current_path () : pluginPath_.parent_path ();
    const std::array<std::filesystem::path, 5> candidates{
        pluginDir.parent_path () / "Resources" / "dsp.main.js",
        pluginDir.parent_path () / "Resources" / "dist" / "dsp.main.js",
        pluginDir / "Resources" / "dsp.main.js",
        std::filesystem::current_path () / "dist" / "dsp.main.js",
        std::filesystem::current_path () / "public" / "dsp.main.js",
    };

    for (const auto& candidate : candidates)
    {
        if (std::filesystem::exists (candidate))
            return candidate;
    }

    return candidates.front ();
#endif
}

std::string ElementaryEngine::makeStatePayload (const ParameterSet& parameters) const
{
    return parameters.toJson (sampleRate_);
}

void ElementaryEngine::initializeJavaScript (ParameterSet& parameters)
{
    if (maxFrames_ == 0)
        return;

    auto nextRuntime = std::make_shared<elem::Runtime<float>> (sampleRate_, maxFrames_);
    std::atomic_store_explicit (&runtime_, nextRuntime, std::memory_order_release);

    jsContext_.emplace (choc::javascript::createQuickJSContext ());

    jsContext_->registerFunction (
        "__postNativeMessage__",
        [this] (choc::javascript::ArgumentList args)
        {
            if (args.numArgs == 0)
                return choc::value::Value ();

            const auto runtime = std::atomic_load_explicit (&runtime_, std::memory_order_acquire);
            if (!runtime)
                return choc::value::Value ();

            const auto rc = runtime->applyInstructions (elem::js::parseJSON (args[0]->toString ()));
            if (rc != elem::ReturnCode::Ok ())
                std::cerr << "SRVB: Elementary runtime error: " << elem::ReturnCode::describe (rc) << std::endl;

            return choc::value::Value ();
        });

    jsContext_->registerFunction ("__log__",
                                  [] (choc::javascript::ArgumentList args)
                                  {
                                      for (size_t i = 0; i < args.numArgs; ++i)
                                          std::cerr << args[i]->toString () << std::endl;

                                      return choc::value::Value ();
                                  });

    try
    {
        jsContext_->evaluate (kConsoleShimScript);
        jsContext_->evaluate (loadFile (resolveDspEntryPath ()));
        jsContext_->invoke ("__receiveHydrationData__", elem::js::serialize (nextRuntime->snapshot ()));
        jsContext_->invoke ("__receiveStateChange__", makeStatePayload (parameters));
        parameters.consumeDirty ();
        needsStateDispatch_.store (false, std::memory_order_release);
    }
    catch (const std::exception& e)
    {
        std::cerr << "SRVB: failed to initialize DSP JavaScript: " << e.what () << std::endl;
        jsContext_.reset ();
        std::atomic_store_explicit (&runtime_, std::shared_ptr<elem::Runtime<float>> (), std::memory_order_release);
    }
}

void ElementaryEngine::passthrough (const clap_process_t* process) const
{
    if (process == nullptr || process->audio_outputs_count == 0 || process->audio_outputs == nullptr)
        return;

    auto& output = process->audio_outputs[0];
    if (output.data32 == nullptr)
        return;

    const clap_audio_buffer_t* input = nullptr;
    if (process->audio_inputs_count > 0 && process->audio_inputs != nullptr)
        input = &process->audio_inputs[0];

    const auto channels = std::min<uint32_t> (2, output.channel_count);
    for (uint32_t channel = 0; channel < channels; ++channel)
    {
        if (output.data32[channel] == nullptr)
            continue;

        const auto* inputChannel = input != nullptr && input->channel_count > channel && input->data32 != nullptr
                                       ? input->data32[channel]
                                       : nullptr;

        copyOrClear (output.data32[channel], inputChannel, process->frames_count);
    }
}
