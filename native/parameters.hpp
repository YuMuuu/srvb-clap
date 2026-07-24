#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <string>

#include <clap/clap.h>

struct ParameterDefinition
{
    clap_id id;
    const char* key;
    const char* name;
    double minValue;
    double maxValue;
    double defaultValue;
};

class ParameterSet
{
public:
    static constexpr size_t count = 4;
    static const std::array<ParameterDefinition, count>& definitions ();

    ParameterSet ();

    bool getInfo (uint32_t index, clap_param_info_t& info) const;
    bool getValue (clap_id id, double& value) const;
    bool setValue (clap_id id, double value);
    bool valueToText (clap_id id, double value, char* output, uint32_t outputSize) const;
    bool textToValue (clap_id id, const char* text, double& value) const;

    bool consumeDirty ();
    std::string toJson (double sampleRate) const;
    bool loadJson (const std::string& json);

private:
    const ParameterDefinition* findById (clap_id id) const;

    std::array<std::atomic<double>, count> values_;
    std::atomic<bool> dirty_{true};
};
