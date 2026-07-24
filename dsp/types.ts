import type {PluginState} from '../src/shared/parameters';

export type JsonPrimitive = string | number | boolean | null;
export type JsonValue = JsonPrimitive | JsonValue[] | JsonObject;
export type JsonObject = {
  [key: string]: JsonValue;
};

export type DspState = {
  sampleRate: number;
} & PluginState;

export type HydrationData = Record<string, JsonObject>;

export type HydratedNode = {
  symbol: '__ELEM_NODE__';
  kind: '__HYDRATED__';
  hash: number;
  props: JsonObject;
  generation: {
    current: number;
  };
};

export function parseDspState(serialized: string): DspState {
  return JSON.parse(serialized) as DspState;
}
