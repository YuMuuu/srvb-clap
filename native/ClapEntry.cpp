#include <clap/clap.h>

extern bool srvbClapEntryInit (const char* pluginPath);
extern void srvbClapEntryDeinit ();
extern const void* srvbClapEntryGetFactory (const char* factoryId);

extern "C"
{
    const CLAP_EXPORT clap_plugin_entry_t clap_entry{
        CLAP_VERSION,
        srvbClapEntryInit,
        srvbClapEntryDeinit,
        srvbClapEntryGetFactory,
    };
}
