#ifndef I18N_BI_H
#define I18N_BI_H

#include <cwchar>
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

    // tr() for strings that go on to a printf-style formatter. Language files are
    // user-writable and Kestrel may be elevated, so a translation whose conversions
    // differ from the fallback's is ignored rather than handed to the formatter.
    const wchar_t *trf(const char *key, const wchar_t *fallback);

    // Advances s past the next conversion and stores it in spec. %% is literal text
    // and is skipped. A lone trailing % comes back as a conversion so it never passes.
    inline bool nextFormatSpec(const wchar_t *&s, std::wstring &spec)
    {
        while (*s)
        {
            if (*s != L'%')
            {
                ++s;
                continue;
            }

            const wchar_t *end = s + 1;
            while (*end && wcschr(L"-+ #0123456789.*hlLwIjzt", *end))
                ++end;
            if (*end)
                ++end;

            spec.assign(s, end);
            s = end;
            if (spec != L"%%")
                return true;
        }
        return false;
    }

    // True when candidate has exactly reference's conversions, in the same order.
    inline bool formatCompatible(const wchar_t *candidate, const wchar_t *reference)
    {
        std::wstring a, b;
        for (;;)
        {
            bool moreA = nextFormatSpec(candidate, a);
            bool moreB = nextFormatSpec(reference, b);
            if (moreA != moreB)
                return false;
            if (!moreA)
                return true;
            if (a != b)
                return false;
        }
    }
}

#endif
