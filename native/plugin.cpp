#include "elementary_engine.hpp"
#include "parameters.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <clap/clap.h>

namespace
{
std::string gPluginPath;

const char* kFeatures[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_REVERB,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t kDescriptor{
    CLAP_VERSION_INIT,
    "audio.elementary.srvb",
    "SRVB",
    "Elementary Audio",
    "https://www.elementary.audio",
    "",
    "",
    "0.1.0",
    "A small Elementary-powered stereo reverb.",
    kFeatures,
};

struct SrvbPlugin
{
    clap_plugin_t plugin{};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    ParameterSet parameters;
    ElementaryEngine engine;
    std::atomic<bool> callbackRequested{false};

    void requestMainThreadCallback ()
    {
        if (!callbackRequested.exchange (true, std::memory_order_acq_rel))
            host->request_callback (host);
    }
};

SrvbPlugin* fromPlugin (const clap_plugin_t* plugin)
{
    return static_cast<SrvbPlugin*> (plugin->plugin_data);
}

void processEvent (SrvbPlugin& plugin, const clap_event_header_t* header)
{
    if (header == nullptr || header->space_id != CLAP_CORE_EVENT_SPACE_ID)
        return;

    if (header->type != CLAP_EVENT_PARAM_VALUE)
        return;

    const auto* event = reinterpret_cast<const clap_event_param_value_t*> (header);
    if (plugin.parameters.setValue (event->param_id, event->value))
    {
        plugin.engine.markStateDirty ();
        plugin.requestMainThreadCallback ();
    }
}

uint32_t audioPortsCount (const clap_plugin_t*, bool)
{
    return 1;
}

bool audioPortsGet (const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || info == nullptr)
        return false;

    std::memset (info, 0, sizeof (*info));
    info->id = 0;
    std::snprintf (info->name, sizeof (info->name), "%s", isInput ? "Stereo In" : "Stereo Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t kAudioPorts{
    audioPortsCount,
    audioPortsGet,
};

uint32_t paramsCount (const clap_plugin_t*)
{
    return static_cast<uint32_t> (ParameterSet::count);
}

bool paramsGetInfo (const clap_plugin_t* plugin, uint32_t paramIndex, clap_param_info_t* paramInfo)
{
    if (paramInfo == nullptr)
        return false;

    return fromPlugin (plugin)->parameters.getInfo (paramIndex, *paramInfo);
}

bool paramsGetValue (const clap_plugin_t* plugin, clap_id paramId, double* outValue)
{
    if (outValue == nullptr)
        return false;

    return fromPlugin (plugin)->parameters.getValue (paramId, *outValue);
}

bool paramsValueToText (const clap_plugin_t* plugin, clap_id paramId, double value, char* outBuffer,
                        uint32_t outBufferCapacity)
{
    return fromPlugin (plugin)->parameters.valueToText (paramId, value, outBuffer, outBufferCapacity);
}

bool paramsTextToValue (const clap_plugin_t* plugin, clap_id paramId, const char* text, double* outValue)
{
    if (outValue == nullptr)
        return false;

    return fromPlugin (plugin)->parameters.textToValue (paramId, text, *outValue);
}

void paramsFlush (const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    auto& srvb = *fromPlugin (plugin);
    if (in == nullptr)
        return;

    const auto eventCount = in->size (in);
    for (uint32_t i = 0; i < eventCount; ++i)
        processEvent (srvb, in->get (in, i));
}

const clap_plugin_params_t kParams{
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush,
};

bool writeAll (const clap_ostream_t* stream, const std::string& payload)
{
    const char* cursor = payload.data ();
    auto remaining = static_cast<uint64_t> (payload.size ());

    while (remaining > 0)
    {
        const auto written = stream->write (stream, cursor, remaining);
        if (written <= 0)
            return false;

        cursor += written;
        remaining -= static_cast<uint64_t> (written);
    }

    return true;
}

bool stateSave (const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (stream == nullptr)
        return false;

    auto& srvb = *fromPlugin (plugin);
    return writeAll (stream, srvb.parameters.toJson (0.0));
}

bool stateLoad (const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (stream == nullptr)
        return false;

    std::string payload;
    std::array<char, 4096> buffer{};
    while (true)
    {
        const auto bytesRead = stream->read (stream, buffer.data (), buffer.size ());
        if (bytesRead < 0)
            return false;

        if (bytesRead == 0)
            break;

        payload.append (buffer.data (), static_cast<size_t> (bytesRead));
    }

    auto& srvb = *fromPlugin (plugin);
    if (!srvb.parameters.loadJson (payload))
        return false;

    srvb.engine.markStateDirty ();
    srvb.requestMainThreadCallback ();

    if (srvb.hostParams != nullptr)
        srvb.hostParams->rescan (srvb.host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);

    return true;
}

const clap_plugin_state_t kState{
    stateSave,
    stateLoad,
};

bool pluginInit (const clap_plugin_t* plugin)
{
    auto& srvb = *fromPlugin (plugin);
    srvb.hostParams = static_cast<const clap_host_params_t*> (srvb.host->get_extension (srvb.host, CLAP_EXT_PARAMS));
    return true;
}

void pluginDestroy (const clap_plugin_t* plugin)
{
    delete fromPlugin (plugin);
}

bool pluginActivate (const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFramesCount)
{
    auto& srvb = *fromPlugin (plugin);
    srvb.engine.activate (sampleRate, maxFramesCount);
    srvb.requestMainThreadCallback ();
    return true;
}

void pluginDeactivate (const clap_plugin_t* plugin)
{
    fromPlugin (plugin)->engine.deactivate ();
}

bool pluginStartProcessing (const clap_plugin_t*)
{
    return true;
}

void pluginStopProcessing (const clap_plugin_t*) {}

void pluginReset (const clap_plugin_t*) {}

clap_process_status pluginProcess (const clap_plugin_t* plugin, const clap_process_t* process)
{
    auto& srvb = *fromPlugin (plugin);

    if (process != nullptr && process->in_events != nullptr)
    {
        const auto eventCount = process->in_events->size (process->in_events);
        for (uint32_t i = 0; i < eventCount; ++i)
            processEvent (srvb, process->in_events->get (process->in_events, i));
    }

    srvb.engine.process (process);
    return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension (const clap_plugin_t*, const char* id)
{
    if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0)
        return &kAudioPorts;

    if (std::strcmp (id, CLAP_EXT_PARAMS) == 0)
        return &kParams;

    if (std::strcmp (id, CLAP_EXT_STATE) == 0)
        return &kState;

    return nullptr;
}

void pluginOnMainThread (const clap_plugin_t* plugin)
{
    auto& srvb = *fromPlugin (plugin);
    srvb.callbackRequested.store (false, std::memory_order_release);
    srvb.engine.initializeIfNeeded (srvb.parameters);
    srvb.engine.dispatchStateIfNeeded (srvb.parameters);
}

clap_plugin_t* createPlugin (const clap_host_t* host)
{
    auto* srvb = new SrvbPlugin ();
    srvb->host = host;
    srvb->engine.setPluginPath (gPluginPath.c_str ());
    srvb->plugin.desc = &kDescriptor;
    srvb->plugin.plugin_data = srvb;
    srvb->plugin.init = pluginInit;
    srvb->plugin.destroy = pluginDestroy;
    srvb->plugin.activate = pluginActivate;
    srvb->plugin.deactivate = pluginDeactivate;
    srvb->plugin.start_processing = pluginStartProcessing;
    srvb->plugin.stop_processing = pluginStopProcessing;
    srvb->plugin.reset = pluginReset;
    srvb->plugin.process = pluginProcess;
    srvb->plugin.get_extension = pluginGetExtension;
    srvb->plugin.on_main_thread = pluginOnMainThread;
    return &srvb->plugin;
}

uint32_t factoryGetPluginCount (const clap_plugin_factory_t*)
{
    return 1;
}

const clap_plugin_descriptor_t* factoryGetPluginDescriptor (const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t* factoryCreatePlugin (const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    if (host == nullptr || !clap_version_is_compatible (host->clap_version))
        return nullptr;

    if (std::strcmp (pluginId, kDescriptor.id) != 0)
        return nullptr;

    return createPlugin (host);
}

const clap_plugin_factory_t kPluginFactory{
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin,
};
} // namespace

bool srvbEntryInit (const char* pluginPath)
{
    gPluginPath = pluginPath != nullptr ? pluginPath : "";
    return true;
}

void srvbEntryDeinit ()
{
    gPluginPath.clear ();
}

const void* srvbEntryGetFactory (const char* factoryId)
{
    if (std::strcmp (factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &kPluginFactory;

    return nullptr;
}
