#include "ClapEditor.h"
#include "DspEngine.h"

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>

namespace
{
static_assert (std::atomic<double>::is_always_lock_free);

// CLAP wiring follows free-audio/clap/src/plugin-template.c.
constexpr const char* features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB,
                                    CLAP_PLUGIN_FEATURE_STEREO, nullptr};
const clap_plugin_descriptor_t descriptor = {
    CLAP_VERSION, "audio.elementary.srvb",    "SRVB",  "Elementary Audio", "https://www.elementary.audio", "", "",
    "0.1.0",      "Elementary stereo reverb", features};

struct Parameter
{
    clap_id id;
    const char* key;
    const char* name;
};

// IDs are explicit and remain stable when display order changes.
constexpr std::array<Parameter, 4> parameters = {
    {{0, "size", "Size"}, {1, "decay", "Decay"}, {2, "mod", "Mod"}, {3, "mix", "Mix"}}};

size_t parameterIndex (clap_id id)
{
    for (size_t i = 0; i < parameters.size (); ++i)
        if (parameters[i].id == id)
            return i;
    return parameters.size ();
}

size_t parameterIndex (const std::string& key)
{
    for (size_t i = 0; i < parameters.size (); ++i)
        if (parameters[i].key == key)
            return i;
    return parameters.size ();
}

constexpr uint32_t editorWidth = 720;
constexpr uint32_t editorHeight = 444;

const char* editorApi ()
{
#if defined(__APPLE__)
    return CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32)
    return CLAP_WINDOW_API_WIN32;
#else
    return "";
#endif
}

class Plugin
{
public:
    Plugin (const clap_host_t* pluginHost, std::filesystem::path sourcePath);
    clap_plugin_t plugin{};

    static Plugin& self (const clap_plugin_t* plugin)
    {
        return *static_cast<Plugin*> (plugin->plugin_data);
    }

    bool init ()
    {
        hostLog = static_cast<const clap_host_log_t*> (host->get_extension (host, CLAP_EXT_LOG));
        hostParams = static_cast<const clap_host_params_t*> (host->get_extension (host, CLAP_EXT_PARAMS));
        try
        {
#if !ELEM_DEV_LOCALHOST
            std::ifstream file (dspPath, std::ios::binary);
            if (!file)
                throw std::runtime_error ("Cannot open bundled dsp.main.js");
            source.assign (std::istreambuf_iterator<char> (file), std::istreambuf_iterator<char> ());
            if (file.bad () || source.empty ())
                throw std::runtime_error ("Cannot read bundled dsp.main.js");
#endif
            engine = std::make_unique<DspEngine> (DspEngine::Callbacks{[this] () -> std::optional<std::string>
                                                                       {
                                                                           if (source.empty ())
                                                                               return std::nullopt;
                                                                           return source;
                                                                       },
                                                                       [this] (const std::string& script)
                                                                       {
                                                                           if (editor)
                                                                               editor->evaluateJavascript (script);
                                                                       },
                                                                       [this] (const std::string& message)
                                                                       { log (CLAP_LOG_INFO, message.c_str ()); }});
            syncParameters ();
            return true;
        }
        catch (const std::exception& error)
        {
            log (CLAP_LOG_ERROR, error.what ());
            return false;
        }
    }

    void syncParameters ()
    {
        for (size_t i = 0; i < parameters.size (); ++i)
            engine->setParameter (parameters[i].key, values[i].load ());
    }

    void receiveEvents (const clap_input_events_t* events)
    {
        if (!events)
            return;
        bool changed = false;
        for (uint32_t i = 0; i < events->size (events); ++i)
        {
            const auto* header = events->get (events, i);
            if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE ||
                header->size < sizeof (clap_event_param_value_t))
                continue;
            const auto& event = *reinterpret_cast<const clap_event_param_value_t*> (header);
            const auto index = parameterIndex (event.param_id);
            if (index == parameters.size () || !std::isfinite (event.value) || event.note_id != -1 ||
                event.port_index != -1 || event.channel != -1 || event.key != -1)
                continue;
            values[index].store (std::clamp (event.value, 0.0, 1.0));
            changed = true;
        }
        // Preserve Elementary's main-thread JavaScript control path. Event sample
        // offsets are not applied synchronously to the audio graph in this template.
        if (changed && !dirty.exchange (true))
            host->request_callback (host);
    }

    void flushParameters (const clap_input_events_t* input, const clap_output_events_t* output)
    {
        receiveEvents (input);
        sendParameterChanges (output);
    }

    void sendParameterChanges (const clap_output_events_t* output)
    {
        if (!output)
            return;
        for (size_t i = 0; i < parameters.size (); ++i)
        {
            if (!editorChanges[i].exchange (false))
                continue;
            const clap_event_param_value_t event{
                {sizeof (clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0},
                parameters[i].id,
                nullptr,
                -1,
                -1,
                -1,
                -1,
                values[i].load ()};
            if (!output->try_push (output, &event.header))
                editorChanges[i].store (true);
        }
    }

    void setParameterFromEditor (const std::string& key, double value)
    {
        const auto index = parameterIndex (key);
        if (index == parameters.size () || !std::isfinite (value))
            return;
        const auto normalized = std::clamp (value, 0.0, 1.0);
        values[index].store (normalized);
        engine->setParameter (parameters[index].key, normalized);
        engine->dispatchStateChange ();
        editorChanges[index].store (true);
        if (hostParams)
            hostParams->request_flush (host);
    }

    void reloadJavaScript (std::optional<std::string> newSource)
    {
        if (newSource && !newSource->empty ())
            source = std::move (*newSource);
        if (active)
            engine->reloadJavaScript ();
        engine->dispatchStateChange ();
    }

    void update ()
    {
        if (!dirty.exchange (false))
            return;
        syncParameters ();
        if (active)
            engine->dispatchStateChange ();
    }

    void log (clap_log_severity severity, const char* message) const
    {
        if (hostLog)
            hostLog->log (host, severity, message);
    }

    const clap_host_t* const host;
    const std::filesystem::path dspPath;
    const clap_host_log_t* hostLog = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    std::string source;
    std::unique_ptr<DspEngine> engine;
    std::unique_ptr<ClapEditor> editor;
    // Parameter values cross the audio/main-thread boundary; DSP state itself
    // stays in DspEngine and is updated only by the main-thread callback.
    std::array<std::atomic<double>, parameters.size ()> values{};
    std::array<std::atomic<bool>, parameters.size ()> editorChanges{};
    std::atomic<bool> dirty{false};
    bool active = false;
    uint32_t maxFrames = 0;
};

const clap_plugin_audio_ports_t audioPorts = {
    [] (const clap_plugin_t*, bool) -> uint32_t { return 1; },
    [] (const clap_plugin_t*, uint32_t index, bool input, clap_audio_port_info_t* info) -> bool
    {
        if (index != 0)
            return false;
        *info = {};
        info->id = 0;
        std::snprintf (info->name, sizeof (info->name), "%s", input ? "Input" : "Output");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }};

const clap_plugin_params_t params = {
    [] (const clap_plugin_t*) -> uint32_t { return parameters.size (); },
    [] (const clap_plugin_t*, uint32_t index, clap_param_info_t* info) -> bool
    {
        if (index >= parameters.size ())
            return false;
        *info = {};
        info->id = parameters[index].id;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        std::snprintf (info->name, sizeof (info->name), "%s", parameters[index].name);
        info->min_value = 0;
        info->max_value = 1;
        info->default_value = 0.5;
        return true;
    },
    [] (const clap_plugin_t* plugin, clap_id id, double* value) -> bool
    {
        const auto index = parameterIndex (id);
        if (index == parameters.size ())
            return false;
        *value = Plugin::self (plugin).values[index].load ();
        return true;
    },
    [] (const clap_plugin_t*, clap_id id, double value, char* text, uint32_t capacity) -> bool
    {
        if (parameterIndex (id) == parameters.size () || !std::isfinite (value) || capacity == 0)
            return false;
        const auto length = std::snprintf (text, capacity, "%.3f", value);
        return length >= 0 && static_cast<uint32_t> (length) < capacity;
    },
    [] (const clap_plugin_t*, clap_id id, const char* text, double* value) -> bool
    {
        if (parameterIndex (id) == parameters.size ())
            return false;
        char* end = nullptr;
        const auto parsed = std::strtod (text, &end);
        if (end == text || *end != '\0' || !std::isfinite (parsed) || parsed < 0 || parsed > 1)
            return false;
        *value = parsed;
        return true;
    },
    [] (const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out)
    { Plugin::self (plugin).flushParameters (in, out); }};

const clap_plugin_state_t state = {
    [] (const clap_plugin_t* plugin, const clap_ostream_t* stream) -> bool
    {
        try
        {
            auto& p = Plugin::self (plugin);
            p.syncParameters ();
            const auto serialized = p.engine->saveState ();
            size_t offset = 0;
            while (offset < serialized.size ())
            {
                const auto count = stream->write (stream, serialized.data () + offset, serialized.size () - offset);
                if (count <= 0 || static_cast<uint64_t> (count) > serialized.size () - offset)
                    return false;
                offset += static_cast<size_t> (count);
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    },
    [] (const clap_plugin_t* plugin, const clap_istream_t* stream) -> bool
    {
        try
        {
            std::string serialized;
            std::array<char, 1024> buffer{};
            for (;;)
            {
                const auto count = stream->read (stream, buffer.data (), buffer.size ());
                if (count < 0 || count > static_cast<int64_t> (buffer.size ()))
                    return false;
                if (count == 0)
                    break;
                serialized.append (buffer.data (), static_cast<size_t> (count));
                if (serialized.size () > 64 * 1024)
                    return false;
            }
            const auto parsed = elem::js::parseJSON (serialized);
            if (!parsed.isObject ())
                return false;
            std::array<double, parameters.size ()> restored{};
            for (size_t i = 0; i < parameters.size (); ++i)
            {
                const auto value = parsed.getWithDefault (parameters[i].key, elem::js::Value (0.5));
                if (!value.isNumber ())
                    return false;
                restored[i] = static_cast<elem::js::Number> (value);
                if (!std::isfinite (restored[i]) || restored[i] < 0 || restored[i] > 1)
                    return false;
            }
            auto& p = Plugin::self (plugin);
            for (size_t i = 0; i < parameters.size (); ++i)
                p.values[i].store (restored[i]);
            p.dirty.store (true);
            p.update ();
            if (p.hostParams)
                p.hostParams->rescan (p.host, CLAP_PARAM_RESCAN_VALUES);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }};

const clap_plugin_latency_t latency = {[] (const clap_plugin_t*) -> uint32_t { return 0; }};
// Decay can reach unity. Keep the reverb running instead of guessing a finite tail.
const clap_plugin_tail_t tail = {[] (const clap_plugin_t*) -> uint32_t { return INT32_MAX; }};

const clap_plugin_gui_t gui = {[] (const clap_plugin_t*, const char* api, bool floating) -> bool
                               { return !floating && api && std::strcmp (api, editorApi ()) == 0; },
                               [] (const clap_plugin_t*, const char** api, bool* floating) -> bool
                               {
                                   if (!api || !floating || !*editorApi ())
                                       return false;
                                   *api = editorApi ();
                                   *floating = false;
                                   return true;
                               },
                               [] (const clap_plugin_t* plugin, const char* api, bool floating) -> bool
                               {
                                   auto& p = Plugin::self (plugin);
                                   if (p.editor || floating || !api || std::strcmp (api, editorApi ()) != 0)
                                       return false;
                                   try
                                   {
                                       p.editor = std::make_unique<ClapEditor> (
                                           p.dspPath.parent_path (),
                                           ClapEditor::Callbacks{[instance = &p]
                                                                 { instance->engine->dispatchStateChange (); },
                                                                 [instance = &p] (std::optional<std::string> source)
                                                                 { instance->reloadJavaScript (std::move (source)); },
                                                                 [instance = &p] (const std::string& key, double value)
                                                                 { instance->setParameterFromEditor (key, value); }});
                                       return true;
                                   }
                                   catch (const std::exception& error)
                                   {
                                       p.log (CLAP_LOG_ERROR, error.what ());
                                       return false;
                                   }
                               },
                               [] (const clap_plugin_t* plugin) { Plugin::self (plugin).editor.reset (); },
                               [] (const clap_plugin_t*, double) -> bool { return false; },
                               [] (const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) -> bool
                               {
                                   if (!Plugin::self (plugin).editor || !width || !height)
                                       return false;
                                   *width = editorWidth;
                                   *height = editorHeight;
                                   return true;
                               },
                               [] (const clap_plugin_t*) -> bool { return false; },
                               [] (const clap_plugin_t*, clap_gui_resize_hints_t*) -> bool { return false; },
                               [] (const clap_plugin_t*, uint32_t*, uint32_t*) -> bool { return false; },
                               [] (const clap_plugin_t* plugin, uint32_t width, uint32_t height) -> bool
                               {
                                   auto& editor = Plugin::self (plugin).editor;
                                   if (!editor || width != editorWidth || height != editorHeight)
                                       return false;
                                   editor->setSize (width, height);
                                   return true;
                               },
                               [] (const clap_plugin_t* plugin, const clap_window_t* parent) -> bool
                               {
                                   auto& editor = Plugin::self (plugin).editor;
                                   if (!editor || !parent || !editor->setParent (*parent))
                                       return false;
                                   editor->setSize (editorWidth, editorHeight);
                                   return true;
                               },
                               [] (const clap_plugin_t*, const clap_window_t*) -> bool { return false; },
                               [] (const clap_plugin_t*, const char*) {},
                               [] (const clap_plugin_t* plugin) -> bool
                               {
                                   auto& editor = Plugin::self (plugin).editor;
                                   if (!editor)
                                       return false;
                                   editor->setVisible (true);
                                   return true;
                               },
                               [] (const clap_plugin_t* plugin) -> bool
                               {
                                   auto& editor = Plugin::self (plugin).editor;
                                   if (!editor)
                                       return false;
                                   editor->setVisible (false);
                                   return true;
                               }};

const void* CLAP_ABI getExtension (const clap_plugin_t*, const char* id)
{
    if (!std::strcmp (id, CLAP_EXT_AUDIO_PORTS))
        return &audioPorts;
    if (!std::strcmp (id, CLAP_EXT_PARAMS))
        return &params;
    if (!std::strcmp (id, CLAP_EXT_STATE))
        return &state;
    if (!std::strcmp (id, CLAP_EXT_LATENCY))
        return &latency;
    if (!std::strcmp (id, CLAP_EXT_TAIL))
        return &tail;
    if (!std::strcmp (id, CLAP_EXT_GUI))
        return &gui;
    return nullptr;
}

Plugin::Plugin (const clap_host_t* pluginHost, std::filesystem::path sourcePath)
    : host (pluginHost), dspPath (std::move (sourcePath))
{
    for (auto& value : values)
        value.store (0.5);
    for (auto& changed : editorChanges)
        changed.store (false);
    plugin.desc = &descriptor;
    plugin.plugin_data = this;
    plugin.init = [] (const clap_plugin_t* plugin) { return self (plugin).init (); };
    plugin.destroy = [] (const clap_plugin_t* plugin) { delete &self (plugin); };
    plugin.activate = [] (const clap_plugin_t* plugin, double rate, uint32_t, uint32_t maxFrames) -> bool
    {
        auto& p = self (plugin);
        if (!std::isfinite (rate) || rate <= 0 || maxFrames == 0 || maxFrames > INT32_MAX)
            return false;
        try
        {
            p.syncParameters ();
            p.engine->initialize (rate, static_cast<int> (maxFrames));
            p.engine->dispatchStateChange ();
            p.maxFrames = maxFrames;
            p.active = true;
            return true;
        }
        catch (const std::exception& error)
        {
            p.log (CLAP_LOG_ERROR, error.what ());
            return false;
        }
    };
    plugin.deactivate = [] (const clap_plugin_t* plugin)
    {
        auto& p = self (plugin);
        p.active = false;
        p.engine->release ();
    };
    plugin.start_processing = [] (const clap_plugin_t*) { return true; };
    plugin.stop_processing = [] (const clap_plugin_t*) {};
    plugin.reset = [] (const clap_plugin_t* plugin) { self (plugin).engine->reset (); };
    plugin.process = [] (const clap_plugin_t* plugin, const clap_process_t* process) -> clap_process_status
    {
        auto& p = self (plugin);
        if (process->frames_count > p.maxFrames || process->audio_inputs_count != 1 ||
            process->audio_outputs_count != 1)
            return CLAP_PROCESS_ERROR;
        const auto& input = process->audio_inputs[0];
        auto& output = process->audio_outputs[0];
        if (input.channel_count != 2 || output.channel_count != 2 || !input.data32 || !output.data32)
            return CLAP_PROCESS_ERROR;
        const float* inputs[] = {input.data32[0], input.data32[1]};
        float* outputs[] = {output.data32[0], output.data32[1]};
        for (auto* channel : outputs)
            std::fill_n (channel, process->frames_count, 0.0f);
        output.constant_mask = 0;
        p.receiveEvents (process->in_events);
        p.sendParameterChanges (process->out_events);
        p.engine->process (inputs, 2, outputs, 2, process->frames_count);
        return CLAP_PROCESS_CONTINUE;
    };
    plugin.get_extension = getExtension;
    plugin.on_main_thread = [] (const clap_plugin_t* plugin)
    {
        auto& p = self (plugin);
        try
        {
            p.update ();
        }
        catch (const std::exception& error)
        {
            p.log (CLAP_LOG_ERROR, error.what ());
        }
    };
}

// Entry state is shared by factories only; each plugin receives its own path copy.
std::mutex entryMutex;
uint32_t entryCount = 0;
std::filesystem::path dspPath;

bool CLAP_ABI entryInit (const char* path)
{
    try
    {
        std::lock_guard<std::mutex> lock (entryMutex);
        if (entryCount == 0)
        {
            const auto bundle = std::filesystem::u8path (path);
#if defined(__APPLE__)
            dspPath = bundle / "Contents" / "Resources" / "dsp.main.js";
#else
            dspPath = bundle.parent_path () / (bundle.stem ().u8string () + ".resources") / "dsp.main.js";
#endif
        }
        ++entryCount;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void CLAP_ABI entryDeinit ()
{
    std::lock_guard<std::mutex> lock (entryMutex);
    if (entryCount > 0 && --entryCount == 0)
        dspPath.clear ();
}

const clap_plugin_factory_t factory = {
    [] (const clap_plugin_factory_t*) -> uint32_t { return 1; },
    [] (const clap_plugin_factory_t*, uint32_t index) -> const clap_plugin_descriptor_t*
    { return index == 0 ? &descriptor : nullptr; },
    [] (const clap_plugin_factory_t*, const clap_host_t* host, const char* id) -> const clap_plugin_t*
    {
        if (!clap_version_is_compatible (host->clap_version) || std::strcmp (id, descriptor.id))
            return nullptr;
        try
        {
            std::lock_guard<std::mutex> lock (entryMutex);
            return &(new Plugin (host, dspPath))->plugin;
        }
        catch (...)
        {
            return nullptr;
        }
    }};

const void* CLAP_ABI getFactory (const char* id)
{
    return !std::strcmp (id, CLAP_PLUGIN_FACTORY_ID) ? &factory : nullptr;
}
} // namespace

bool srvbClapEntryInit (const char* pluginPath)
{
    return entryInit (pluginPath);
}

void srvbClapEntryDeinit ()
{
    entryDeinit ();
}

const void* srvbClapEntryGetFactory (const char* factoryId)
{
    return getFactory (factoryId);
}
