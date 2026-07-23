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

    std::wstring utf8ToWide(const std::string &s)
    {
        if (s.empty())
            return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
        return w;
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
    bool parseFile(const std::string &path)
    {
        FILE *f = fopen(path.c_str(), "rb");
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
                g_table[key] = utf8ToWide(unescape(val));
        }
        fclose(f);
        return true;
    }
}

void i18n_bi::load(const std::string &lang)
{
    g_table.clear();
    g_lang = lang.empty() ? "en" : lang;

    if (g_lang == "en")
        return;  // inline fallbacks

    std::string file = g_lang + ".lang";

    // Next to the exe first.
    std::string exe = paths_bi::exePath();
    size_t slash = exe.find_last_of("\\/");
    bool loaded = false;
    if (slash != std::string::npos)
        loaded = parseFile(exe.substr(0, slash + 1) + file);

    if (!loaded)
        loaded = parseFile(paths_bi::inDataDir(file.c_str()));

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
