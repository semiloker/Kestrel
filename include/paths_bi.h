#ifndef PATHS_BI_H
#define PATHS_BI_H

#include <cstdio>
#include <string>

namespace paths_bi
{
    // Opens `path` for append, refusing anything that is not a plain on-disk
    // file: reparse points, non-disk handles and hardlinked files are all
    // rejected, so a symlink planted in the data directory cannot redirect a
    // write. Sets *emptyOut when the file did not exist or is zero length, so
    // callers know to emit a header. Returns NULL on any failure.
    FILE *openRegularAppend(const std::wstring &path, bool *emptyOut);

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
