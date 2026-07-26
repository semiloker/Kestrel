#ifndef CSV_BI_H
#define CSV_BI_H

#include <string>

namespace csv_bi
{
    inline std::string escape(const std::string &value)
    {
        if (value.find_first_of(",\"\r\n") == std::string::npos)
            return value;

        std::string escaped;
        escaped.reserve(value.size() + 2);
        escaped.push_back('"');
        for (char c : value)
        {
            if (c == '"')
                escaped.push_back('"');
            escaped.push_back(c);
        }
        escaped.push_back('"');
        return escaped;
    }

    inline bool nextField(const std::string &line, size_t *position,
                          std::string *value)
    {
        if (!position || !value || *position > line.size())
            return false;

        size_t cursor = *position;
        value->clear();

        if (cursor < line.size() && line[cursor] == '"')
        {
            ++cursor;
            while (cursor < line.size())
            {
                char c = line[cursor++];
                if (c != '"')
                {
                    value->push_back(c);
                    continue;
                }

                if (cursor < line.size() && line[cursor] == '"')
                {
                    value->push_back('"');
                    ++cursor;
                    continue;
                }

                if (cursor == line.size())
                {
                    *position = line.size() + 1;
                    return true;
                }
                if (line[cursor] != ',')
                    return false;

                *position = cursor + 1;
                return true;
            }
            return false;
        }

        size_t comma = line.find(',', cursor);
        size_t end = comma == std::string::npos ? line.size() : comma;
        size_t quote = line.find('"', cursor);
        if (quote != std::string::npos && quote < end)
            return false;

        *value = line.substr(cursor, end - cursor);
        *position = comma == std::string::npos ? line.size() + 1 : comma + 1;
        return true;
    }
}

#endif
