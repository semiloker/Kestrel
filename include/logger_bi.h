#ifndef LOGGER_BI_H
#define LOGGER_BI_H

namespace log_bi
{
    void init();

    void write(const char *fmt, ...);

    void writeErr(unsigned long code, const char *fmt, ...);

    void shutdown();
}

#endif
