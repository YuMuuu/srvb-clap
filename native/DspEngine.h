#pragma once

#include <choc_javascript.h>
#include <elem/Runtime.h>

#include <functional>
#include <optional>
#include <string>

class DspEngine
{
public:
    struct Callbacks
    {
        std::function<std::optional<std::string> ()> loadJavaScript;
        std::function<void (const std::string&)> evaluateEditorScript;
        std::function<void (const std::string&)> log;
    };

    explicit DspEngine (Callbacks callbacks);
    DspEngine (const DspEngine&) = delete;
    DspEngine& operator= (const DspEngine&) = delete;
    DspEngine (DspEngine&&) = delete;
    DspEngine& operator= (DspEngine&&) = delete;

    // Control methods run on the main thread, preserving the existing Elementary
    // state -> JavaScript -> instruction batch path. The host owns scheduling.
    void setParameter (const std::string& id, double value);
    void initialize (double sampleRate, int maxBlockSize);
    void release ();
    void reloadJavaScript ();
    void dispatchStateChange ();
    void dispatchError (const std::string& name, const std::string& message);
    std::string saveState () const;
    bool loadState (const std::string& serialized);

    // Audio-thread entry. The caller supplies distinct input/output buffers and
    // clears outputs before calling. Runtime replacement must not overlap process.
    void process (const float** inputs, size_t numInputs, float** outputs, size_t numOutputs, size_t numSamples);
    void reset ();

private:
    const Callbacks callbacks;
    // Keep Elementary's mutable runtime and parameter state local to this owner.
    elem::js::Object state;
    double sampleRate = 0;
    std::unique_ptr<elem::Runtime<float>> runtime;
    choc::javascript::Context jsContext;
};
