#include "PluginProcessor.h"
#include "WebViewEditor.h"

//==============================================================================
// A quick helper for locating bundled asset files
juce::File getAssetsDirectory ()
{
#if JUCE_MAC
    auto assetsDir = juce::File::getSpecialLocation (juce::File::SpecialLocationType::currentApplicationFile)
                         .getChildFile ("Contents/Resources/dist");
#elif JUCE_WINDOWS
    auto assetsDir =
        juce::File::getSpecialLocation (
            juce::File::SpecialLocationType::currentExecutableFile) // Plugin.vst3/Contents/<arch>/Plugin.vst3
            .getParentDirectory ()                                  // Plugin.vst3/Contents/<arch>/
            .getParentDirectory ()                                  // Plugin.vst3/Contents/
            .getChildFile ("Resources/dist");
#else
#error "We only support Mac and Windows here yet."
#endif

    return assetsDir;
}

//==============================================================================
EffectsPluginProcessor::EffectsPluginProcessor ()
    : AudioProcessor (BusesProperties ()
                          .withInput ("Input", juce::AudioChannelSet::stereo (), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo (), true)),
      engine ({[] () -> std::optional<std::string>
               {
#if ELEM_DEV_LOCALHOST
                   return juce::URL ("http://localhost:5173/dsp.main.js").readEntireTextStream ().toStdString ();
#else
                   const auto file = getAssetsDirectory ().getChildFile ("dsp.main.js");
                   if (!file.existsAsFile ())
                       return std::nullopt;
                   return file.loadFileAsString ().toStdString ();
#endif
               },
               [this] (const std::string& script)
               {
                   if (auto* editor = static_cast<WebViewEditor*> (getActiveEditor ()))
                       editor->getWebViewPtr ()->evaluateJavascript (script);
               },
               [this] (const std::string& serialized)
               {
                   if (auto* editor = static_cast<WebViewEditor*> (getActiveEditor ()))
                   {
                       editor->getWebViewPtr ()->evaluateJavascript ("console.log(...JSON.parse(" +
                                                                     elem::js::serialize (serialized) + "));");
                   }
                   else
                   {
                       DBG (serialized);
                   }
               }})
{
    // Initialize parameters from the manifest file
#if ELEM_DEV_LOCALHOST
    auto manifestFile = juce::URL ("http://localhost:5173/manifest.json");
    auto manifestFileContents = manifestFile.readEntireTextStream ().toStdString ();
#else
    auto manifestFile = getAssetsDirectory ().getChildFile ("manifest.json");

    if (!manifestFile.existsAsFile ())
        return;

    auto manifestFileContents = manifestFile.loadFileAsString ().toStdString ();
#endif

    auto manifest = elem::js::parseJSON (manifestFileContents);

    if (!manifest.isObject ())
        return;

    auto parameters = manifest.getWithDefault ("parameters", elem::js::Array ());

    for (size_t i = 0; i < parameters.size (); ++i)
    {
        auto descrip = parameters[i];

        if (!descrip.isObject ())
            continue;

        auto paramId = descrip.getWithDefault ("paramId", elem::js::String ("unknown"));
        auto name = descrip.getWithDefault ("name", elem::js::String ("Unknown"));
        auto minValue = descrip.getWithDefault ("min", elem::js::Number (0));
        auto maxValue = descrip.getWithDefault ("max", elem::js::Number (1));
        auto defValue = descrip.getWithDefault ("defaultValue", elem::js::Number (0));

        auto* p =
            new juce::AudioParameterFloat (juce::ParameterID (paramId, 1), name,
                                           {static_cast<float> (minValue), static_cast<float> (maxValue)}, defValue);

        p->addListener (this);
        addParameter (p);

        // Push a new ParameterReadout onto the list to represent this parameter
        paramReadouts.emplace_back (ParameterReadout{static_cast<float> (defValue), false});

        // Update our state object with the default parameter value
        engine.setParameter (paramId, defValue);
    }
}

EffectsPluginProcessor::~EffectsPluginProcessor ()
{
    for (auto& p : getParameters ())
    {
        p->removeListener (this);
    }
}

//==============================================================================
juce::AudioProcessorEditor* EffectsPluginProcessor::createEditor ()
{
    return new WebViewEditor (this, getAssetsDirectory (), 800, 704);
}

bool EffectsPluginProcessor::hasEditor () const
{
    return true;
}

//==============================================================================
const juce::String EffectsPluginProcessor::getName () const
{
    return JucePlugin_Name;
}

bool EffectsPluginProcessor::acceptsMidi () const
{
    return false;
}

bool EffectsPluginProcessor::producesMidi () const
{
    return false;
}

bool EffectsPluginProcessor::isMidiEffect () const
{
    return false;
}

double EffectsPluginProcessor::getTailLengthSeconds () const
{
    return 0.0;
}

//==============================================================================
int EffectsPluginProcessor::getNumPrograms ()
{
    return 1; // NB: some hosts don't cope very well if you tell them there are 0 programs,
              // so this should be at least 1, even if you're not really implementing programs.
}

int EffectsPluginProcessor::getCurrentProgram ()
{
    return 0;
}

void EffectsPluginProcessor::setCurrentProgram (int /* index */) {}
const juce::String EffectsPluginProcessor::getProgramName (int /* index */)
{
    return {};
}
void EffectsPluginProcessor::changeProgramName (int /* index */, const juce::String& /* newName */) {}

//==============================================================================
void EffectsPluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Some hosts call `prepareToPlay` on the real-time thread, some call it on the main thread.
    // To address the discrepancy, we check whether anything has changed since our last known
    // call. If it has, we flag for initialization of the Elementary engine and runtime, then
    // trigger an async update.
    //
    // JUCE will synchronously handle the async update if it understands
    // that we're already on the main thread.
    if (sampleRate != lastKnownSampleRate || samplesPerBlock != lastKnownBlockSize)
    {
        lastKnownSampleRate = sampleRate;
        lastKnownBlockSize = samplesPerBlock;

        shouldInitialize.store (true);
    }

    // Now that the environment is set up, push our current state
    triggerAsyncUpdate ();
}

void EffectsPluginProcessor::releaseResources ()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool EffectsPluginProcessor::isBusesLayoutSupported (const AudioProcessor::BusesLayout& layouts) const
{
    return true;
}

void EffectsPluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /* midiMessages */)
{
    // Copy the input so that our input and output buffers are distinct
    scratchBuffer.makeCopyOf (buffer, true);

    // Clear the output buffer to prevent any garbage if our runtime isn't ready
    buffer.clear ();

    engine.process (const_cast<const float**> (scratchBuffer.getArrayOfWritePointers ()), getTotalNumInputChannels (),
                    const_cast<float**> (buffer.getArrayOfWritePointers ()), buffer.getNumChannels (),
                    buffer.getNumSamples ());
}

void EffectsPluginProcessor::parameterValueChanged (int parameterIndex, float newValue)
{
    // Mark the updated parameter value in the dirty list
    auto& pr = *std::next (paramReadouts.begin (), parameterIndex);

    pr.store ({newValue, true});
    triggerAsyncUpdate ();
}

void EffectsPluginProcessor::parameterGestureChanged (int, bool)
{
    // Not implemented
}

//==============================================================================
void EffectsPluginProcessor::handleAsyncUpdate ()
{
    // First things first, we check the flag to identify if we should initialize the Elementary
    // runtime and engine.
    if (shouldInitialize.exchange (false))
    {
        // TODO: This is definitely not thread-safe! It could delete a Runtime instance while
        // the real-time thread is using it. Depends on when the host will call prepareToPlay.
        engine.initialize (lastKnownSampleRate, lastKnownBlockSize);
    }

    // Next we iterate over the current parameter values to update our local state
    // object, which we in turn dispatch into the JavaScript engine
    auto& params = getParameters ();

    // Reduce over the changed parameters to resolve our updated processor state
    for (size_t i = 0; i < paramReadouts.size (); ++i)
    {
        // We atomically exchange an arbitrary value with a dirty flag false, because
        // we know that the next time we exchange, if the dirty flag is still false, the
        // value can be considered arbitrary. Only when we exchange and find the dirty flag
        // true do we consider the value as having been written by the processor since
        // we last looked.
        auto& current = *std::next (paramReadouts.begin (), i);
        auto pr = current.exchange ({0.0f, false});

        if (pr.dirty)
        {
            if (auto* pf = dynamic_cast<juce::AudioParameterFloat*> (params[i]))
            {
                auto paramId = pf->paramID.toStdString ();
                engine.setParameter (paramId, pr.value);
            }
        }
    }

    dispatchStateChange ();
}

void EffectsPluginProcessor::initJavaScriptEngine ()
{
    engine.reloadJavaScript ();
}

void EffectsPluginProcessor::dispatchStateChange ()
{
    engine.dispatchStateChange ();
}

void EffectsPluginProcessor::dispatchError (const std::string& name, const std::string& message)
{
    engine.dispatchError (name, message);
}

void EffectsPluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto serialized = engine.saveState ();
    destData.replaceAll (serialized.data (), serialized.size ());
}

void EffectsPluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data != nullptr && sizeInBytes > 0)
        engine.loadState (std::string (static_cast<const char*> (data), static_cast<size_t> (sizeInBytes)));
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter ()
{
    return new EffectsPluginProcessor ();
}
