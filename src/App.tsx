import { useStore as useZustandStore } from 'zustand'
import { createStore } from 'zustand/vanilla'

import Interface from './Interface'
import {defaultPluginState, type ParamId} from './shared/parameters';

const store = createStore<PluginState>(() => ({...defaultPluginState}));
const useStore = () => useZustandStore(store);

const errorStore = createStore<{ error: PluginError | null }>(() => ({ error: null }));
const useErrorStore = () => useZustandStore(errorStore);

function requestParamValueUpdate(paramId: ParamId, value: number) {
  if (typeof globalThis.__postNativeMessage__ === 'function') {
    globalThis.__postNativeMessage__("setParameterValue", {
      paramId,
      value,
    });
  }
}

if (import.meta.env.DEV && import.meta.hot) {
  const reloadDsp = async () => {
    console.log('Sending reload dsp message');

    if (typeof globalThis.__postNativeMessage__ === 'function') {
      try {
        const response = await fetch('/dsp.main.js');
        if (!response.ok) throw new Error(`Failed to reload DSP: ${response.status}`);
        const source = await response.text();
        globalThis.__postNativeMessage__('reload', {source});
      } catch (error) {
        console.error(error);
      }
    }
  };
  import.meta.hot.on('reload-dsp', reloadDsp);
  void reloadDsp();
}

globalThis.__receiveStateChange__ = function(state: string) {
  store.setState(JSON.parse(state) as PluginState);
};

globalThis.__receiveError__ = (err: PluginError) => {
  errorStore.setState({ error: err });
};

export default function App() {
  const state = useStore();
  const {error} = useErrorStore();

  return (
    <Interface
      state={state}
      error={error}
      requestParamValueUpdate={requestParamValueUpdate}
      resetErrorState={() => errorStore.setState({ error: null })} />
  );
}
