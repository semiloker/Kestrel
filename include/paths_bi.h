#ifndef PATHS_BI_H
#define PATHS_BI_H

#include <string>

namespace paths_bi
{
    const std::string &dataDir();

    std::string inDataDir(const char *fileName);

    const std::string &exePath();
}

#endif
