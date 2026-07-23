#ifndef I18N_BI_H
#define I18N_BI_H

#include <string>

// Minimal localization (FEAT-08). English strings live inline as the fallback
// argument to tr(), so "en" needs no file. To translate, drop a UTF-8
// "<code>.lang" file (key=translated text, one per line, # comments) next to
// kestrel.exe or in the data dir, and set ui.language=<code>.
namespace i18n_bi
{
    // Loads <lang>.lang. "en" or a missing file => inline English is used.
    void load(const std::string &lang);

    const std::string &language();

    // Translation for key, or fallback if the active language lacks it. The
    // returned pointer is stable until the next load() (called once at startup).
    const wchar_t *tr(const char *key, const wchar_t *fallback);
}

#endif
