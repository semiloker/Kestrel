#ifndef PATHS_BI_H
#define PATHS_BI_H

#include <string>

namespace paths_bi
{
    std::string wideToUtf8(const std::wstring &text);
    std::wstring utf8ToWide(const std::string &text);

    const std::wstring &dataDirWide();
    const std::string &dataDir();

    std::wstring inDataDirWide(const wchar_t *fileName);
    std::string inDataDir(const char *fileName);

    const std::wstring &exePathWide();
    const std::string &exePath();
}

#endif
