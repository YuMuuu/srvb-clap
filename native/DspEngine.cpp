#include "DspEngine.h"

#include <choc_javascript_QuickJS.h>
#include <utility>

namespace
{
std::string callbackScript (const std::string& name, const std::string& argument)
{
    return "(function() { if (typeof globalThis." + name + " !== 'function') return false; globalThis." + name + "(" +
           argument + "); return true; })();";
}
} // namespace

DspEngine::DspEngine (Callbacks engineCallbacks)
    : callbacks (std::move (engineCallbacks)), jsContext (choc::javascript::createQuickJSContext ())
{
}

void DspEngine::setParameter (const std::string& id, double value)
{
    state.insert_or_assign (id, elem::js::Number (value));
}

void DspEngine::initialize (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    runtime = std::make_unique<elem::Runtime<float>> (sampleRate, maxBlockSize);
    reloadJavaScript ();
}

void DspEngine::process (const float** inputs, size_t numInputs, float** outputs, size_t numOutputs, size_t numSamples)
{
    if (runtime)
        runtime->process (inputs, numInputs, outputs, numOutputs, numSamples, nullptr);
}

void DspEngine::reloadJavaScript ()
{
    jsContext = choc::javascript::createQuickJSContext ();

    // Install some native interop functions in our JavaScript environment
    jsContext.registerFunction ("__postNativeMessage__",
                                [this] (choc::javascript::ArgumentList args)
                                {
                                    auto const batch = elem::js::parseJSON (args[0]->toString ());
                                    auto const rc = runtime->applyInstructions (batch);

                                    if (rc != elem::ReturnCode::Ok ())
                                    {
                                        dispatchError ("Runtime Error", elem::ReturnCode::describe (rc));
                                    }

                                    return choc::value::Value ();
                                });

    jsContext.registerFunction ("__log__",
                                [this] (choc::javascript::ArgumentList args)
                                {
                                    auto values = choc::value::createEmptyArray ();
                                    for (size_t i = 0; i < args.numArgs; ++i)
                                        values.addArrayElement (*args[i]);
                                    callbacks.log (choc::json::toString (values));
                                    return choc::value::Value ();
                                });

    // A simple shim to write various console operations to our native __log__ handler
    jsContext.evaluate (R"shim(
(function() {
  if (typeof globalThis.console === 'undefined') {
    globalThis.console = {
      log(...args) {
        __log__('[embedded:log]', ...args);
      },
      warn(...args) {
          __log__('[embedded:warn]', ...args);
      },
      error(...args) {
          __log__('[embedded:error]', ...args);
      }
    };
  }
})();
    )shim");

    const auto source = callbacks.loadJavaScript ();
    if (!source)
        return;
    jsContext.evaluate (*source);

    jsContext.evaluate (
        callbackScript ("__receiveHydrationData__", elem::js::serialize (elem::js::serialize (runtime->snapshot ()))));
}

void DspEngine::dispatchStateChange ()
{
    auto localState = state;
    localState.insert_or_assign ("sampleRate", sampleRate);
    const auto script =
        callbackScript ("__receiveStateChange__", elem::js::serialize (elem::js::serialize (localState)));
    callbacks.evaluateEditorScript (script);
    jsContext.evaluate (script);
}

void DspEngine::dispatchError (const std::string& name, const std::string& message)
{
    const auto error = "(function() { let e = new Error(" + elem::js::serialize (message) +
                       "); e.name = " + elem::js::serialize (name) + "; return e; })()";
    const auto script = callbackScript ("__receiveError__", error);
    callbacks.evaluateEditorScript (script);
    jsContext.evaluate (script);
}

std::string DspEngine::saveState () const
{
    return elem::js::serialize (state);
}

bool DspEngine::loadState (const std::string& serialized)
{
    try
    {
        const auto parsed = elem::js::parseJSON (serialized);
        for (const auto& entry : parsed.getObject ())
        {
            if (state.find (entry.first) != state.end ())
                state.insert_or_assign (entry.first, entry.second);
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}
