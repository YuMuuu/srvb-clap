#include <clap/clap.h>

extern bool srvbEntryInit (const char* pluginPath);
extern void srvbEntryDeinit ();
extern const void* srvbEntryGetFactory (const char* factoryId);

extern "C"
{
    const CLAP_EXPORT clap_plugin_entry_t clap_entry{
        CLAP_VERSION,
        srvbEntryInit,
        srvbEntryDeinit,
        srvbEntryGetFactory,
    };
}
