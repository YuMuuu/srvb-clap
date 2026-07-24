#include "parameters.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <elem/JSON.h>

namespace
{
constexpr clap_id kParamSize = 1000;
constexpr clap_id kParamDecay = 1001;
constexpr clap_id kParamMod = 1002;
constexpr clap_id kParamMix = 1003;

const std::array<ParameterDefinition, ParameterSet::count> kDefinitions{{
    {kParamSize, "size", "Size", 0.0, 1.0, 0.5},
    {kParamDecay, "decay", "Decay", 0.0, 1.0, 0.5},
    {kParamMod, "mod", "Mod", 0.0, 1.0, 0.5},
    {kParamMix, "mix", "Mix", 0.0, 1.0, 0.5},
}};

double clampValue (const ParameterDefinition& definition, double value)
{
    return std::max (definition.minValue, std::min (definition.maxValue, value));
}

void copyClapString (char* destination, size_t destinationSize, const char* source)
{
    std::snprintf (destination, destinationSize, "%s", source);
}
} // namespace

const std::array<ParameterDefinition, ParameterSet::count>& ParameterSet::definitions ()
{
    return kDefinitions;
}

ParameterSet::ParameterSet ()
{
    for (size_t i = 0; i < kDefinitions.size (); ++i)
        values_[i].store (kDefinitions[i].defaultValue, std::memory_order_relaxed);
}

bool ParameterSet::getInfo (uint32_t index, clap_param_info_t& info) const
{
    if (index >= kDefinitions.size ())
        return false;

    const auto& definition = kDefinitions[index];
    std::memset (&info, 0, sizeof (info));
    info.id = definition.id;
    info.flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS;
    info.min_value = definition.minValue;
    info.max_value = definition.maxValue;
    info.default_value = definition.defaultValue;
    copyClapString (info.name, sizeof (info.name), definition.name);
    info.module[0] = '\0';
    info.cookie = const_cast<ParameterDefinition*> (&definition);
    return true;
}

bool ParameterSet::getValue (clap_id id, double& value) const
{
    const auto* definition = findById (id);
    if (definition == nullptr)
        return false;

    const auto index = static_cast<size_t> (definition - kDefinitions.data ());
    value = values_[index].load (std::memory_order_relaxed);
    return true;
}

bool ParameterSet::setValue (clap_id id, double value)
{
    const auto* definition = findById (id);
    if (definition == nullptr)
        return false;

    const auto index = static_cast<size_t> (definition - kDefinitions.data ());
    values_[index].store (clampValue (*definition, value), std::memory_order_relaxed);
    dirty_.store (true, std::memory_order_release);
    return true;
}

bool ParameterSet::valueToText (clap_id id, double value, char* output, uint32_t outputSize) const
{
    if (findById (id) == nullptr || output == nullptr || outputSize == 0)
        return false;

    std::snprintf (output, outputSize, "%.17g", value);
    return true;
}

bool ParameterSet::textToValue (clap_id id, const char* text, double& value) const
{
    const auto* definition = findById (id);
    if (definition == nullptr || text == nullptr)
        return false;

    char* end = nullptr;
    const auto parsed = std::strtod (text, &end);
    if (end == text)
        return false;

    value = clampValue (*definition, parsed);
    return true;
}

bool ParameterSet::consumeDirty ()
{
    return dirty_.exchange (false, std::memory_order_acq_rel);
}

std::string ParameterSet::toJson (double sampleRate) const
{
    std::ostringstream json;
    json << "{\"sampleRate\":" << sampleRate;

    for (size_t i = 0; i < kDefinitions.size (); ++i)
        json << ",\"" << kDefinitions[i].key << "\":" << values_[i].load (std::memory_order_relaxed);

    json << "}";
    return json.str ();
}

bool ParameterSet::loadJson (const std::string& json)
{
    try
    {
        const auto parsed = elem::js::parseJSON (json);
        if (!parsed.isObject ())
            return false;

        const auto object = parsed.getObject ();
        bool changed = false;
        for (const auto& definition : kDefinitions)
        {
            const auto it = object.find (definition.key);
            if (it == object.end () || !it->second.isNumber ())
                continue;

            setValue (definition.id, static_cast<double> (it->second));
            changed = true;
        }

        return changed;
    }
    catch (...)
    {
        return false;
    }
}

const ParameterDefinition* ParameterSet::findById (clap_id id) const
{
    for (const auto& definition : kDefinitions)
    {
        if (definition.id == id)
            return &definition;
    }

    return nullptr;
}
