#include "i18n_bi.h"
#include "paths_bi.h"
#include "logger_bi.h"

#include <windows.h>
#include <map>
#include <cstdio>

namespace
{
    std::map<std::string, std::wstring> g_table;
    std::string g_lang = "en";

    bool validLanguageTag(const std::string &tag)
    {
        if (tag.empty() || tag.size() > 32)
            return false;

        for (unsigned char c : tag)
        {
            bool allowed = (c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') ||
                           c == '-' || c == '_';
            if (!allowed)
                return false;
        }
        return true;
    }

    void trim(std::string &s)
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) { s.clear(); return; }
        size_t e = s.find_last_not_of(" \t\r\n");
        s = s.substr(b, e - b + 1);
    }

    // \n -> newline, \\ -> \  (values are single-line in the file but multi-line
    // in the UI, e.g. command-link buttons).
    std::string unescape(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\\' && i + 1 < s.size())
            {
                char n = s[++i];
                out += (n == 'n') ? '\n' : n;
            }
            else
                out += s[i];
        }
        return out;
    }

    // <code>.lang next to the exe (portable) first, then the data dir.
    bool parseFile(const std::wstring &path)
    {
        FILE *f = _wfopen(path.c_str(), L"rb");
        if (!f)
            return false;

        char line[1024];
        while (fgets(line, sizeof(line), f))
        {
            std::string s(line);
            trim(s);
            if (s.empty() || s[0] == '#')
                continue;
            size_t eq = s.find('=');
            if (eq == std::string::npos)
                continue;
            std::string key = s.substr(0, eq);
            std::string val = s.substr(eq + 1);
            trim(key);
            trim(val);
            if (!key.empty())
                g_table[key] = paths_bi::utf8ToWide(unescape(val));
        }
        fclose(f);
        return true;
    }
}

void i18n_bi::load(const std::string &lang)
{
    g_table.clear();
    g_lang = lang.empty() ? "en" : lang;

    if (!validLanguageTag(g_lang))
    {
        log_bi::write("i18n: rejected invalid language tag");
        g_lang = "en";
    }

    if (g_lang == "en")
        return;  // inline fallbacks

    std::string file = g_lang + ".lang";
    std::wstring wideFile = paths_bi::utf8ToWide(file);
    if (wideFile.empty())
    {
        log_bi::write("i18n: language tag is not valid UTF-8");
        g_lang = "en";
        return;
    }

    // Next to the exe first.
    std::wstring exe = paths_bi::exePathWide();
    size_t slash = exe.find_last_of(L"\\/");
    bool loaded = false;
    if (slash != std::wstring::npos)
        loaded = parseFile(exe.substr(0, slash + 1) + wideFile);

    if (!loaded)
        loaded = parseFile(paths_bi::inDataDirWide(wideFile.c_str()));

    if (loaded)
        log_bi::write("i18n: loaded language '%s' (%u strings)",
                      g_lang.c_str(), (unsigned)g_table.size());
    else
        log_bi::write("i18n: no '%s' file found, using English", file.c_str());
}

const std::string &i18n_bi::language()
{
    return g_lang;
}

const wchar_t *i18n_bi::tr(const char *key, const wchar_t *fallback)
{
    std::map<std::string, std::wstring>::const_iterator it = g_table.find(key);
    if (it != g_table.end() && !it->second.empty())
        return it->second.c_str();
    return fallback;
}
