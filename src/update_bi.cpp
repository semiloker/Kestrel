#include "update_bi.h"

#include "app_identity_bi.h"
#include "autostart_bi.h"
#include "paths_bi.h"
#include "logger_bi.h"

#include <bcrypt.h>
#include <softpub.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <wintrust.h>
#include <winver.h>

#include <climits>
#include <cstring>
#include <cstdio>
#include <format>
#include <string>
#include <vector>

namespace
{
    const char *API_URL =
        "https://api.github.com/repos/semiloker/Kestrel/releases/latest";
    const char *API_PATH = "/repos/semiloker/Kestrel/releases/latest";
    const char *EXE_ASSET_NAME = "kestrel.exe";
    const char *CHECKSUM_ASSET_NAME = "kestrel.exe.sha256";

    const size_t MIN_BINARY_BYTES = 64 * 1024;
    const size_t MAX_BINARY_BYTES = 128 * 1024 * 1024;
    const size_t MAX_METADATA_BYTES = 4 * 1024 * 1024;
    const size_t MAX_CHECKSUM_BYTES = 16 * 1024;
    const unsigned MAX_REDIRECTS = 5;

    enum url_policy_bi
    {
        URL_API,
        URL_RELEASE_ASSET
    };

    struct parsed_url_bi
    {
        std::wstring host;
        std::wstring target;
        INTERNET_PORT port = 0;
    };

    struct release_asset_bi
    {
        std::string name;
        std::string url;
    };

    struct release_info_bi
    {
        std::string tag;
        std::vector<release_asset_bi> assets;
    };

    std::wstring utf8ToWide(const std::string &text)
    {
        if (text.empty())
            return std::wstring();

        int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         text.c_str(), (int)text.size(), NULL, 0);
        if (needed <= 0)
            return std::wstring();

        std::wstring result((size_t)needed, L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                text.c_str(), (int)text.size(),
                                &result[0], needed) != needed)
        {
            return std::wstring();
        }

        return result;
    }

    std::string wideToUtf8(const std::wstring &text)
    {
        if (text.empty())
            return std::string();

        int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                         (int)text.size(), NULL, 0,
                                         NULL, NULL);
        if (needed <= 0)
            return std::string();

        std::string result((size_t)needed, '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                &result[0], needed, NULL, NULL) != needed)
        {
            return std::string();
        }

        return result;
    }

    wchar_t lowerAscii(wchar_t c)
    {
        if (c >= L'A' && c <= L'Z')
            return c - L'A' + L'a';
        return c;
    }

    void lowerAscii(std::wstring *text)
    {
        for (size_t i = 0; i < text->size(); ++i)
            (*text)[i] = lowerAscii((*text)[i]);
    }

    bool crackHttpsUrl(const std::string &url, parsed_url_bi *out)
    {
        std::wstring wide = utf8ToWide(url);
        if (wide.empty())
            return false;

        URL_COMPONENTS parts = {};
        parts.dwStructSize = sizeof(parts);
        parts.dwSchemeLength = (DWORD)-1;
        parts.dwHostNameLength = (DWORD)-1;
        parts.dwUserNameLength = (DWORD)-1;
        parts.dwPasswordLength = (DWORD)-1;
        parts.dwUrlPathLength = (DWORD)-1;
        parts.dwExtraInfoLength = (DWORD)-1;

        if (!WinHttpCrackUrl(wide.c_str(), (DWORD)wide.size(), 0, &parts))
            return false;

        if (parts.nScheme != INTERNET_SCHEME_HTTPS ||
            parts.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
            parts.dwHostNameLength == 0 ||
            parts.dwUserNameLength != 0 ||
            parts.dwPasswordLength != 0)
        {
            return false;
        }

        out->host.assign(parts.lpszHostName, parts.dwHostNameLength);
        lowerAscii(&out->host);

        std::wstring path;
        if (parts.dwUrlPathLength > 0)
            path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
        else
            path = L"/";

        if (parts.dwExtraInfoLength > 0)
            path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

        out->target = path;
        out->port = parts.nPort;
        return true;
    }

    bool isRedirectHostAllowed(const std::wstring &host)
    {
        return host == L"github.com" ||
               host == L"api.github.com" ||
               host == L"objects.githubusercontent.com" ||
               host == L"release-assets.githubusercontent.com" ||
               host == L"github-releases.githubusercontent.com";
    }

    bool validateInitialUrl(const std::string &url, url_policy_bi policy,
                            const char *assetName, parsed_url_bi *out)
    {
        parsed_url_bi parsed;
        if (!crackHttpsUrl(url, &parsed))
            return false;

        if (policy == URL_API)
        {
            if (parsed.host != L"api.github.com" ||
                parsed.target != utf8ToWide(API_PATH))
            {
                return false;
            }
        }
        else
        {
            if (!assetName || parsed.host != L"github.com")
                return false;

            const std::wstring prefix =
                L"/semiloker/Kestrel/releases/download/";
            if (parsed.target.compare(0, prefix.size(), prefix) != 0)
                return false;

            std::wstring remainder = parsed.target.substr(prefix.size());
            size_t slash = remainder.find(L'/');
            if (slash == std::wstring::npos || slash == 0 ||
                remainder.find(L'/', slash + 1) != std::wstring::npos)
            {
                return false;
            }

            if (remainder.substr(slash + 1) != utf8ToWide(assetName))
                return false;
        }

        *out = parsed;
        return true;
    }

    bool isRedirectStatus(DWORD status)
    {
        return status == 301 || status == 302 || status == 303 ||
               status == 307 || status == 308;
    }

    bool queryRedirect(HINTERNET request, std::string *urlOut)
    {
        DWORD bytes = 0;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION,
                            WINHTTP_HEADER_NAME_BY_INDEX, NULL, &bytes,
                            WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(wchar_t))
            return false;

        std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &buffer[0], &bytes,
                                 WINHTTP_NO_HEADER_INDEX))
        {
            return false;
        }

        std::wstring location(&buffer[0]);
        std::string utf8 = wideToUtf8(location);
        if (utf8.empty())
            return false;

        // Relative redirects are deliberately rejected. GitHub release assets
        // currently use absolute HTTPS redirects, which lets every hop be
        // validated independently.
        if (utf8.compare(0, 8, "https://") != 0)
            return false;

        *urlOut = utf8;
        return true;
    }

    bool cancelledNow(volatile LONG *cancelled)
    {
        return cancelled &&
               InterlockedCompareExchange(cancelled, 0, 0) != 0;
    }

    bool fetchUrl(const std::string &url, url_policy_bi policy,
                  const char *assetName, std::string *body,
                  volatile LONG *cancelled, volatile LONG *progressOut,
                  size_t maxBytes)
    {
        body->clear();

        HINTERNET session = WinHttpOpen(
            L"Kestrel-Updater/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session)
            return false;

        // Cancellation is observed between synchronous operations. Keeping
        // each individual operation bounded also bounds cancellation latency.
        if (!WinHttpSetTimeouts(session, 5000, 10000, 10000, 10000))
        {
            WinHttpCloseHandle(session);
            return false;
        }

        DWORD secureProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS,
                              &secureProtocols, sizeof(secureProtocols)))
        {
            WinHttpCloseHandle(session);
            return false;
        }

        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY,
                              &redirectPolicy, sizeof(redirectPolicy)))
        {
            WinHttpCloseHandle(session);
            return false;
        }

        std::string currentUrl = url;
        bool ok = false;

        for (unsigned redirect = 0; redirect <= MAX_REDIRECTS; ++redirect)
        {
            if (cancelledNow(cancelled))
                break;

            parsed_url_bi parsed;
            bool valid = false;

            if (redirect == 0)
            {
                valid = validateInitialUrl(currentUrl, policy, assetName, &parsed);
            }
            else
            {
                valid = crackHttpsUrl(currentUrl, &parsed) &&
                        isRedirectHostAllowed(parsed.host);
            }

            if (!valid)
            {
                log_bi::write("update: rejected non-GitHub or non-HTTPS URL");
                break;
            }

            HINTERNET connect = WinHttpConnect(session, parsed.host.c_str(),
                                               parsed.port, 0);
            if (!connect)
                break;

            HINTERNET request = WinHttpOpenRequest(
                connect, L"GET", parsed.target.c_str(), NULL,
                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE);
            if (!request)
            {
                WinHttpCloseHandle(connect);
                break;
            }

            const wchar_t *headers =
                L"Accept: application/vnd.github+json, "
                L"application/octet-stream\r\n"
                L"User-Agent: Kestrel-Updater\r\n";

            bool sent = !cancelledNow(cancelled) &&
                        WinHttpSendRequest(request, headers, (DWORD)-1,
                                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                        WinHttpReceiveResponse(request, NULL);

            DWORD status = 0;
            DWORD statusSize = sizeof(status);
            if (sent)
            {
                WinHttpQueryHeaders(
                    request,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                    WINHTTP_NO_HEADER_INDEX);
            }

            if (sent && isRedirectStatus(status))
            {
                std::string nextUrl;
                bool haveRedirect = queryRedirect(request, &nextUrl);
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);

                if (!haveRedirect || redirect == MAX_REDIRECTS)
                    break;

                currentUrl = nextUrl;
                continue;
            }

            if (!sent || status != 200)
            {
                if (sent)
                    log_bi::write("update: HTTP status %u", (unsigned)status);
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                break;
            }

            DWORD contentLength = 0;
            DWORD lengthSize = sizeof(contentLength);
            if (WinHttpQueryHeaders(
                    request,
                    WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &lengthSize,
                    WINHTTP_NO_HEADER_INDEX) &&
                (size_t)contentLength > maxBytes)
            {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                break;
            }

            std::vector<char> chunk(16384);
            ok = true;

            for (;;)
            {
                if (cancelledNow(cancelled))
                {
                    ok = false;
                    break;
                }

                DWORD read = 0;
                if (!WinHttpReadData(request, &chunk[0],
                                     (DWORD)chunk.size(), &read))
                {
                    ok = false;
                    break;
                }

                if (read == 0)
                    break;

                if ((size_t)read > maxBytes - body->size())
                {
                    ok = false;
                    break;
                }

                body->append(&chunk[0], read);

                if (progressOut && contentLength > 0)
                {
                    LONG percent = (LONG)((body->size() * 100) /
                                          (size_t)contentLength);
                    InterlockedExchange(progressOut,
                                        percent > 100 ? 100 : percent);
                }
            }

            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            break;
        }

        WinHttpCloseHandle(session);

        if (!ok)
            body->clear();
        return ok;
    }

    int hexValue(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    void appendUtf8(unsigned codePoint, std::string *out)
    {
        if (codePoint <= 0x7f)
        {
            out->push_back((char)codePoint);
        }
        else if (codePoint <= 0x7ff)
        {
            out->push_back((char)(0xc0 | (codePoint >> 6)));
            out->push_back((char)(0x80 | (codePoint & 0x3f)));
        }
        else if (codePoint <= 0xffff)
        {
            out->push_back((char)(0xe0 | (codePoint >> 12)));
            out->push_back((char)(0x80 | ((codePoint >> 6) & 0x3f)));
            out->push_back((char)(0x80 | (codePoint & 0x3f)));
        }
        else
        {
            out->push_back((char)(0xf0 | (codePoint >> 18)));
            out->push_back((char)(0x80 | ((codePoint >> 12) & 0x3f)));
            out->push_back((char)(0x80 | ((codePoint >> 6) & 0x3f)));
            out->push_back((char)(0x80 | (codePoint & 0x3f)));
        }
    }

    class json_reader_bi
    {
    public:
        explicit json_reader_bi(const std::string &textIn)
            : text(textIn), position(0)
        {
        }

        bool parseRelease(release_info_bi *out)
        {
            bool haveTag = false;
            bool haveAssets = false;

            if (!consume('{'))
                return false;

            skipWhitespace();
            if (consume('}'))
                return false;

            for (;;)
            {
                std::string key;
                if (!parseString(&key) || !consume(':'))
                    return false;

                if (key == "tag_name")
                {
                    if (haveTag || !parseString(&out->tag))
                        return false;
                    haveTag = true;
                }
                else if (key == "assets")
                {
                    if (haveAssets || !parseAssets(&out->assets))
                        return false;
                    haveAssets = true;
                }
                else if (!skipValue(0))
                {
                    return false;
                }

                skipWhitespace();
                if (consume('}'))
                    break;
                if (!consume(','))
                    return false;
            }

            skipWhitespace();
            return haveTag && haveAssets && position == text.size();
        }

    private:
        const std::string &text;
        size_t position;

        void skipWhitespace()
        {
            while (position < text.size())
            {
                char c = text[position];
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
                    break;
                ++position;
            }
        }

        bool consume(char expected)
        {
            skipWhitespace();
            if (position >= text.size() || text[position] != expected)
                return false;
            ++position;
            return true;
        }

        bool parseHex4(unsigned *valueOut)
        {
            if (position + 4 > text.size())
                return false;

            unsigned value = 0;
            for (int i = 0; i < 4; ++i)
            {
                int digit = hexValue(text[position++]);
                if (digit < 0)
                    return false;
                value = (value << 4) | (unsigned)digit;
            }

            *valueOut = value;
            return true;
        }

        bool parseString(std::string *out)
        {
            skipWhitespace();
            if (position >= text.size() || text[position++] != '"')
                return false;

            out->clear();

            while (position < text.size())
            {
                unsigned char c = (unsigned char)text[position++];

                if (c == '"')
                    return true;
                if (c < 0x20)
                    return false;

                if (c != '\\')
                {
                    out->push_back((char)c);
                    continue;
                }

                if (position >= text.size())
                    return false;

                char escaped = text[position++];
                switch (escaped)
                {
                case '"':
                case '\\':
                case '/':
                    out->push_back(escaped);
                    break;
                case 'b':
                    out->push_back('\b');
                    break;
                case 'f':
                    out->push_back('\f');
                    break;
                case 'n':
                    out->push_back('\n');
                    break;
                case 'r':
                    out->push_back('\r');
                    break;
                case 't':
                    out->push_back('\t');
                    break;
                case 'u':
                {
                    unsigned codePoint = 0;
                    if (!parseHex4(&codePoint))
                        return false;

                    if (codePoint >= 0xd800 && codePoint <= 0xdbff)
                    {
                        if (position + 2 > text.size() ||
                            text[position] != '\\' ||
                            text[position + 1] != 'u')
                        {
                            return false;
                        }
                        position += 2;

                        unsigned low = 0;
                        if (!parseHex4(&low) ||
                            low < 0xdc00 || low > 0xdfff)
                        {
                            return false;
                        }

                        codePoint = 0x10000 +
                                    ((codePoint - 0xd800) << 10) +
                                    (low - 0xdc00);
                    }
                    else if (codePoint >= 0xdc00 && codePoint <= 0xdfff)
                    {
                        return false;
                    }

                    appendUtf8(codePoint, out);
                    break;
                }
                default:
                    return false;
                }
            }

            return false;
        }

        bool skipNumber()
        {
            skipWhitespace();
            size_t start = position;

            if (position < text.size() && text[position] == '-')
                ++position;

            if (position >= text.size())
                return false;

            if (text[position] == '0')
            {
                ++position;
            }
            else
            {
                if (text[position] < '1' || text[position] > '9')
                    return false;
                while (position < text.size() &&
                       text[position] >= '0' && text[position] <= '9')
                {
                    ++position;
                }
            }

            if (position < text.size() && text[position] == '.')
            {
                ++position;
                size_t fraction = position;
                while (position < text.size() &&
                       text[position] >= '0' && text[position] <= '9')
                {
                    ++position;
                }
                if (fraction == position)
                    return false;
            }

            if (position < text.size() &&
                (text[position] == 'e' || text[position] == 'E'))
            {
                ++position;
                if (position < text.size() &&
                    (text[position] == '+' || text[position] == '-'))
                {
                    ++position;
                }

                size_t exponent = position;
                while (position < text.size() &&
                       text[position] >= '0' && text[position] <= '9')
                {
                    ++position;
                }
                if (exponent == position)
                    return false;
            }

            return position > start;
        }

        bool skipLiteral(const char *literal)
        {
            skipWhitespace();
            size_t length = strlen(literal);
            if (text.compare(position, length, literal) != 0)
                return false;
            position += length;
            return true;
        }

        bool skipValue(unsigned depth)
        {
            if (depth > 64)
                return false;

            skipWhitespace();
            if (position >= text.size())
                return false;

            char c = text[position];
            if (c == '"')
            {
                std::string ignored;
                return parseString(&ignored);
            }
            if (c == '{')
            {
                ++position;
                skipWhitespace();
                if (consume('}'))
                    return true;

                for (;;)
                {
                    std::string key;
                    if (!parseString(&key) || !consume(':') ||
                        !skipValue(depth + 1))
                    {
                        return false;
                    }

                    if (consume('}'))
                        return true;
                    if (!consume(','))
                        return false;
                }
            }
            if (c == '[')
            {
                ++position;
                skipWhitespace();
                if (consume(']'))
                    return true;

                for (;;)
                {
                    if (!skipValue(depth + 1))
                        return false;
                    if (consume(']'))
                        return true;
                    if (!consume(','))
                        return false;
                }
            }
            if (c == 't')
                return skipLiteral("true");
            if (c == 'f')
                return skipLiteral("false");
            if (c == 'n')
                return skipLiteral("null");
            return skipNumber();
        }

        bool parseAsset(release_asset_bi *asset)
        {
            bool haveName = false;
            bool haveUrl = false;

            if (!consume('{'))
                return false;
            if (consume('}'))
                return true;

            for (;;)
            {
                std::string key;
                if (!parseString(&key) || !consume(':'))
                    return false;

                if (key == "name")
                {
                    if (haveName || !parseString(&asset->name))
                        return false;
                    haveName = true;
                }
                else if (key == "browser_download_url")
                {
                    if (haveUrl || !parseString(&asset->url))
                        return false;
                    haveUrl = true;
                }
                else if (!skipValue(0))
                {
                    return false;
                }

                if (consume('}'))
                    return true;
                if (!consume(','))
                    return false;
            }
        }

        bool parseAssets(std::vector<release_asset_bi> *assets)
        {
            if (!consume('['))
                return false;
            if (consume(']'))
                return true;

            for (;;)
            {
                release_asset_bi asset;
                if (!parseAsset(&asset))
                    return false;

                if (!asset.name.empty() || !asset.url.empty())
                    assets->push_back(asset);

                if (consume(']'))
                    return true;
                if (!consume(','))
                    return false;
            }
        }
    };

    bool parseVersionPart(const std::string &text, size_t *position,
                          int *valueOut)
    {
        if (*position >= text.size() ||
            text[*position] < '0' || text[*position] > '9')
        {
            return false;
        }

        int value = 0;
        while (*position < text.size() &&
               text[*position] >= '0' && text[*position] <= '9')
        {
            int digit = text[*position] - '0';
            if (value > (INT_MAX - digit) / 10)
                return false;
            value = value * 10 + digit;
            ++*position;
        }

        *valueOut = value;
        return true;
    }

    bool parseVersion(const std::string &text, int *major,
                      int *minor, int *patch)
    {
        size_t position = 0;
        while (position < text.size() && text[position] == ' ')
            ++position;

        if (position < text.size() &&
            (text[position] == 'v' || text[position] == 'V'))
        {
            ++position;
        }

        int a = 0;
        int b = 0;
        int c = 0;

        if (!parseVersionPart(text, &position, &a) ||
            position >= text.size() || text[position++] != '.' ||
            !parseVersionPart(text, &position, &b))
        {
            return false;
        }

        if (position < text.size() && text[position] == '.')
        {
            ++position;
            if (!parseVersionPart(text, &position, &c))
                return false;
        }

        while (position < text.size() && text[position] == ' ')
            ++position;

        if (position != text.size())
            return false;

        *major = a;
        *minor = b;
        *patch = c;
        return true;
    }

    bool isNewer(int major, int minor, int patch)
    {
        if (major != APP_VERSION_MAJOR)
            return major > APP_VERSION_MAJOR;
        if (minor != APP_VERSION_MINOR)
            return minor > APP_VERSION_MINOR;
        return patch > APP_VERSION_PATCH;
    }

    bool selectReleaseAssets(const release_info_bi &release,
                             std::string *exeUrl,
                             std::string *checksumUrl)
    {
        unsigned exeMatches = 0;
        unsigned checksumMatches = 0;

        for (size_t i = 0; i < release.assets.size(); ++i)
        {
            const release_asset_bi &asset = release.assets[i];
            if (asset.name == EXE_ASSET_NAME)
            {
                ++exeMatches;
                *exeUrl = asset.url;
            }
            else if (asset.name == CHECKSUM_ASSET_NAME)
            {
                ++checksumMatches;
                *checksumUrl = asset.url;
            }
        }

        if (exeMatches != 1 || checksumMatches != 1)
            return false;

        parsed_url_bi exeParsed;
        parsed_url_bi checksumParsed;
        if (!validateInitialUrl(*exeUrl, URL_RELEASE_ASSET,
                                EXE_ASSET_NAME, &exeParsed) ||
            !validateInitialUrl(*checksumUrl, URL_RELEASE_ASSET,
                                CHECKSUM_ASSET_NAME, &checksumParsed))
        {
            return false;
        }

        size_t exeSlash = exeParsed.target.find_last_of(L'/');
        size_t checksumSlash = checksumParsed.target.find_last_of(L'/');
        std::wstring expectedDirectory =
            L"/semiloker/Kestrel/releases/download/" +
            utf8ToWide(release.tag);
        return exeSlash != std::wstring::npos &&
               checksumSlash != std::wstring::npos &&
               exeParsed.target.substr(0, exeSlash) ==
                   checksumParsed.target.substr(0, checksumSlash) &&
               exeParsed.target.substr(0, exeSlash) == expectedDirectory;
    }

    bool parseChecksum(const std::string &text, std::string *hashOut)
    {
        size_t position = 0;
        while (position < text.size() &&
               (text[position] == ' ' || text[position] == '\t' ||
                text[position] == '\r' || text[position] == '\n'))
        {
            ++position;
        }

        if (text.size() - position < 64)
            return false;

        std::string hash;
        hash.reserve(64);
        for (size_t i = 0; i < 64; ++i)
        {
            char c = text[position++];
            if (hexValue(c) < 0)
                return false;
            hash.push_back((c >= 'A' && c <= 'F') ? c - 'A' + 'a' : c);
        }

        if (position >= text.size() ||
            (text[position] != ' ' && text[position] != '\t'))
        {
            return false;
        }

        while (position < text.size() &&
               (text[position] == ' ' || text[position] == '\t'))
        {
            ++position;
        }

        if (position < text.size() && text[position] == '*')
            ++position;

        size_t nameStart = position;
        while (position < text.size() &&
               text[position] != '\r' && text[position] != '\n' &&
               text[position] != ' ' && text[position] != '\t')
        {
            ++position;
        }

        if (text.substr(nameStart, position - nameStart) != EXE_ASSET_NAME)
            return false;

        while (position < text.size() &&
               (text[position] == ' ' || text[position] == '\t' ||
                text[position] == '\r' || text[position] == '\n'))
        {
            ++position;
        }

        if (position != text.size())
            return false;

        *hashOut = hash;
        return true;
    }

    class sha256_context_bi
    {
    public:
        sha256_context_bi() : algorithm(NULL), hash(NULL)
        {
        }

        ~sha256_context_bi()
        {
            if (hash)
                BCryptDestroyHash(hash);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0);
        }

        bool initialize()
        {
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                            NULL, 0) < 0)
            {
                return false;
            }

            DWORD objectSize = 0;
            DWORD hashSize = 0;
            DWORD returned = 0;

            if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                  reinterpret_cast<PUCHAR>(&objectSize),
                                  sizeof(objectSize), &returned, 0) < 0 ||
                BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                                  reinterpret_cast<PUCHAR>(&hashSize),
                                  sizeof(hashSize), &returned, 0) < 0 ||
                hashSize != 32)
            {
                return false;
            }

            if (objectSize == 0)
                return false;

            object.resize(objectSize);
            return BCryptCreateHash(algorithm, &hash, &object[0],
                                    (ULONG)object.size(), NULL, 0, 0) >= 0;
        }

        bool add(const void *data, size_t size)
        {
            if (!hash || size > 0xffffffffu)
                return false;
            if (size == 0)
                return true;

            return BCryptHashData(
                       hash,
                       const_cast<PUCHAR>(
                           reinterpret_cast<const UCHAR *>(data)),
                       (ULONG)size, 0) >= 0;
        }

        bool finish(std::string *hexOut)
        {
            BYTE digest[32] = {};
            if (!hash ||
                BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0)
            {
                return false;
            }

            const char digits[] = "0123456789abcdef";
            std::string result;
            result.resize(sizeof(digest) * 2);

            for (size_t i = 0; i < sizeof(digest); ++i)
            {
                result[i * 2] = digits[digest[i] >> 4];
                result[i * 2 + 1] = digits[digest[i] & 0x0f];
            }

            *hexOut = result;
            return true;
        }

    private:
        BCRYPT_ALG_HANDLE algorithm;
        BCRYPT_HASH_HANDLE hash;
        std::vector<BYTE> object;
    };

    bool sha256Bytes(const std::string &data, std::string *hashOut)
    {
        sha256_context_bi context;
        return context.initialize() &&
               context.add(data.data(), data.size()) &&
               context.finish(hashOut);
    }

    bool sha256Handle(HANDLE file, std::string *hashOut)
    {
        LARGE_INTEGER beginning = {};
        if (!SetFilePointerEx(file, beginning, NULL, FILE_BEGIN))
            return false;

        sha256_context_bi context;
        if (!context.initialize())
            return false;

        std::vector<BYTE> buffer(64 * 1024);
        for (;;)
        {
            DWORD read = 0;
            if (!ReadFile(file, &buffer[0], (DWORD)buffer.size(), &read, NULL))
                return false;
            if (read == 0)
                break;
            if (!context.add(&buffer[0], read))
                return false;
        }

        return context.finish(hashOut);
    }

    bool hashesEqual(const std::string &left, const std::string &right)
    {
        if (left.size() != right.size())
            return false;

        unsigned difference = 0;
        for (size_t i = 0; i < left.size(); ++i)
            difference |= (unsigned char)left[i] ^ (unsigned char)right[i];
        return difference == 0;
    }

    bool readAt(HANDLE file, ULONGLONG offset, void *buffer, DWORD bytes)
    {
        if (offset > (ULONGLONG)LLONG_MAX)
            return false;

        LARGE_INTEGER position = {};
        position.QuadPart = (LONGLONG)offset;
        if (!SetFilePointerEx(file, position, NULL, FILE_BEGIN))
            return false;

        DWORD read = 0;
        return ReadFile(file, buffer, bytes, &read, NULL) &&
               read == bytes;
    }

    bool hasCertificateTable(HANDLE file, bool *presentOut)
    {
        *presentOut = false;

        LARGE_INTEGER fileSize = {};
        if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0)
            return false;

        IMAGE_DOS_HEADER dos = {};
        if (!readAt(file, 0, &dos, sizeof(dos)) ||
            dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew < (LONG)sizeof(dos))
        {
            return false;
        }

        ULONGLONG ntOffset = (ULONGLONG)dos.e_lfanew;
        ULONGLONG totalSize = (ULONGLONG)fileSize.QuadPart;
        const ULONGLONG fixedHeaders =
            sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
        if (ntOffset > totalSize || fixedHeaders > totalSize - ntOffset)
            return false;

        DWORD signature = 0;
        IMAGE_FILE_HEADER fileHeader = {};
        if (!readAt(file, ntOffset, &signature, sizeof(signature)) ||
            signature != IMAGE_NT_SIGNATURE ||
            !readAt(file, ntOffset + sizeof(signature),
                    &fileHeader, sizeof(fileHeader)) ||
            fileHeader.SizeOfOptionalHeader < sizeof(WORD))
        {
            return false;
        }

        ULONGLONG optionalOffset = ntOffset + fixedHeaders;
        if ((ULONGLONG)fileHeader.SizeOfOptionalHeader >
            totalSize - optionalOffset)
        {
            return false;
        }

        std::vector<BYTE> optional(fileHeader.SizeOfOptionalHeader);
        if (!readAt(file, optionalOffset, &optional[0],
                    (DWORD)optional.size()))
        {
            return false;
        }

        WORD magic = 0;
        memcpy(&magic, &optional[0], sizeof(magic));

        IMAGE_DATA_DIRECTORY security = {};
        if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        {
            if (optional.size() < sizeof(IMAGE_OPTIONAL_HEADER32))
                return false;

            IMAGE_OPTIONAL_HEADER32 header = {};
            memcpy(&header, &optional[0], sizeof(header));
            if (header.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_SECURITY)
                return true;
            security = header.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];
        }
        else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        {
            if (optional.size() < sizeof(IMAGE_OPTIONAL_HEADER64))
                return false;

            IMAGE_OPTIONAL_HEADER64 header = {};
            memcpy(&header, &optional[0], sizeof(header));
            if (header.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_SECURITY)
                return true;
            security = header.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];
        }
        else
        {
            return false;
        }

        if (security.VirtualAddress == 0 && security.Size == 0)
            return true;

        ULONGLONG tableOffset = security.VirtualAddress;
        ULONGLONG tableSize = security.Size;
        if (tableOffset == 0 || tableSize < sizeof(WIN_CERTIFICATE) ||
            (tableOffset & 7u) != 0 || tableOffset > totalSize ||
            tableSize > totalSize - tableOffset)
        {
            return false;
        }

        *presentOut = true;
        return true;
    }

    bool verifyAuthenticodeIfPresent(const std::wstring &path,
                                     HANDLE file,
                                     bool *signedOut)
    {
        *signedOut = false;

        if (path.empty() || file == INVALID_HANDLE_VALUE)
            return false;

        bool certificateTable = false;
        if (!hasCertificateTable(file, &certificateTable))
            return false;
        if (!certificateTable)
            return true;

        *signedOut = true;

        WINTRUST_FILE_INFO fileInfo = {};
        fileInfo.cbStruct = sizeof(fileInfo);
        fileInfo.pcwszFilePath = path.c_str();

        WINTRUST_DATA trust = {};
        trust.cbStruct = sizeof(trust);
        trust.dwUIChoice = WTD_UI_NONE;
        trust.fdwRevocationChecks = WTD_REVOKE_NONE;
        trust.dwUnionChoice = WTD_CHOICE_FILE;
        trust.pFile = &fileInfo;
        trust.dwStateAction = WTD_STATEACTION_VERIFY;
        trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

        GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        LONG status = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE,
                                     &action, &trust);

        trust.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &trust);

        if (status != ERROR_SUCCESS)
        {
            log_bi::writeErr((unsigned long)status,
                             "update: Authenticode verification failed");
            return false;
        }

        return true;
    }

    bool basicExecutable(HANDLE file)
    {
        LARGE_INTEGER size = {};
        if (!GetFileSizeEx(file, &size) ||
            size.QuadPart < (LONGLONG)MIN_BINARY_BYTES ||
            size.QuadPart > (LONGLONG)MAX_BINARY_BYTES)
        {
            return false;
        }

        LARGE_INTEGER beginning = {};
        if (!SetFilePointerEx(file, beginning, NULL, FILE_BEGIN))
            return false;

        BYTE magic[2] = {};
        DWORD read = 0;
        return ReadFile(file, magic, sizeof(magic), &read, NULL) &&
               read == sizeof(magic) && magic[0] == 'M' && magic[1] == 'Z';
    }

    bool openVerifiedExecutable(const std::wstring &path,
                                const std::string &expectedHash,
                                int expectedSignature,
                                HANDLE *fileOut,
                                std::string *actualHashOut,
                                bool *signedOut)
    {
        *fileOut = INVALID_HANDLE_VALUE;

        HANDLE file = CreateFileW(
            path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN |
                               FILE_FLAG_OPEN_REPARSE_POINT,
            NULL);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        FILE_ATTRIBUTE_TAG_INFO attributes = {};
        BY_HANDLE_FILE_INFORMATION information = {};
        if (GetFileType(file) != FILE_TYPE_DISK ||
            !GetFileInformationByHandleEx(
                file, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
            (attributes.FileAttributes &
             (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0 ||
            !GetFileInformationByHandle(file, &information) ||
            information.nNumberOfLinks != 1)
        {
            CloseHandle(file);
            return false;
        }

        std::string actualHash;
        bool signedFile = false;

        bool valid = basicExecutable(file) &&
                     sha256Handle(file, &actualHash) &&
                     (expectedHash.empty() ||
                      hashesEqual(actualHash, expectedHash)) &&
                      verifyAuthenticodeIfPresent(path, file, &signedFile) &&
                     (expectedSignature < 0 ||
                      signedFile == (expectedSignature != 0));

        if (!valid)
        {
            CloseHandle(file);
            return false;
        }

        if (actualHashOut)
            *actualHashOut = actualHash;
        if (signedOut)
            *signedOut = signedFile;

        *fileOut = file;
        return true;
    }

    bool writeNewFile(const std::wstring &path, const std::string &data)
    {
        DeleteFileW(path.c_str());

        HANDLE file = CreateFileW(
            path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN |
                FILE_FLAG_WRITE_THROUGH,
            NULL);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        size_t position = 0;
        bool ok = true;

        while (position < data.size())
        {
            size_t remaining = data.size() - position;
            DWORD chunk = (DWORD)(remaining > 1024 * 1024
                                      ? 1024 * 1024
                                      : remaining);
            DWORD written = 0;

            if (!WriteFile(file, data.data() + position,
                           chunk, &written, NULL) ||
                written != chunk)
            {
                ok = false;
                break;
            }

            position += written;
        }

        if (ok)
            ok = FlushFileBuffers(file) != FALSE;

        CloseHandle(file);

        if (!ok)
            DeleteFileW(path.c_str());
        return ok;
    }

    void restoreAfterFailedInstall(const std::wstring &exe,
                                   const std::wstring &staged,
                                   const std::wstring &backup)
    {
        MoveFileExW(exe.c_str(), staged.c_str(), MOVEFILE_REPLACE_EXISTING);
        MoveFileExW(backup.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
}

update_bi::update_bi()
    : current(UPDATE_IDLE), stagedSigned(false), progress(0),
      notifyWindow(NULL), worker(NULL), wantDownload(false), cancelled(0)
{
    InitializeCriticalSection(&lock);
}

update_bi::~update_bi()
{
    cancel();
    joinWorker();
    DeleteCriticalSection(&lock);
}

std::wstring update_bi::exeDirectory()
{
    std::wstring exe = paths_bi::exePathWide();
    size_t slash = exe.find_last_of(L"\\/");

    if (slash == std::wstring::npos)
        return std::wstring();

    return exe.substr(0, slash + 1);
}

std::wstring update_bi::stagedPath()
{
    std::wstring dir = exeDirectory();
    return dir.empty() ? std::wstring() : dir + L"kestrel.new.exe";
}

std::wstring update_bi::backupPath()
{
    std::wstring dir = exeDirectory();
    return dir.empty() ? std::wstring() : dir + L"kestrel.old.exe";
}

bool update_bi::backupExists() const
{
    std::wstring path = backupPath();
    if (path.empty())
        return false;

    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::string update_bi::backupVersion() const
{
    if (!backupExists())
        return std::string();

    std::wstring path = backupPath();
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0)
        return std::string();

    std::vector<BYTE> buffer(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, &buffer[0]))
        return std::string();

    VS_FIXEDFILEINFO *info = NULL;
    UINT infoLength = 0;
    if (!VerQueryValueW(&buffer[0], L"\\",
                        (LPVOID *)&info, &infoLength) ||
        !info)
    {
        return std::string();
    }

    return std::format("{}.{}.{}",
                       (unsigned)HIWORD(info->dwFileVersionMS),
                       (unsigned)LOWORD(info->dwFileVersionMS),
                       (unsigned)HIWORD(info->dwFileVersionLS));
}

update_bi::state_bi update_bi::state() const
{
    EnterCriticalSection(&lock);
    state_bi result = current;
    LeaveCriticalSection(&lock);
    return result;
}

std::string update_bi::latestVersion() const
{
    EnterCriticalSection(&lock);
    std::string result = version;
    LeaveCriticalSection(&lock);
    return result;
}

std::string update_bi::message() const
{
    EnterCriticalSection(&lock);
    std::string result = note;
    LeaveCriticalSection(&lock);
    return result;
}

int update_bi::progressPercent() const
{
    return (int)InterlockedCompareExchange(
        const_cast<volatile LONG *>(&progress), 0, 0);
}

bool update_bi::busy() const
{
    state_bi value = state();
    return value == UPDATE_CHECKING || value == UPDATE_DOWNLOADING;
}

void update_bi::publish(state_bi value, const std::string &messageText)
{
    EnterCriticalSection(&lock);
    current = value;
    note = messageText;
    HWND target = notifyWindow;
    LeaveCriticalSection(&lock);

    if (target && IsWindow(target))
        PostMessage(target, WM_APP_UPDATE, 0, 0);
}

void update_bi::cancel()
{
    InterlockedExchange(&cancelled, 1);
}

void update_bi::joinWorker()
{
    HANDLE handle = NULL;

    EnterCriticalSection(&lock);
    handle = worker;
    worker = NULL;
    LeaveCriticalSection(&lock);

    if (!handle)
        return;

    // The object and its critical section must outlive the worker. Network
    // operations have finite per-operation timeouts, so an unbounded object
    // lifetime is safer than returning from destruction with a live thread.
    DWORD wait = WaitForSingleObject(handle, INFINITE);
    if (wait != WAIT_OBJECT_0)
        log_bi::writeErr(GetLastError(), "update: waiting for worker failed");

    CloseHandle(handle);
}

void update_bi::startWorker(HWND notify, bool download)
{
    if (busy())
        return;

    joinWorker();
    InterlockedExchange(&progress, 0);

    EnterCriticalSection(&lock);
    notifyWindow = notify;
    wantDownload = download;
    current = download ? UPDATE_DOWNLOADING : UPDATE_CHECKING;
    note = download ? "Downloading" : "Checking";
    LeaveCriticalSection(&lock);

    InterlockedExchange(&cancelled, 0);

    HANDLE handle = CreateThread(NULL, 0, &update_bi::threadEntry,
                                 this, 0, NULL);

    EnterCriticalSection(&lock);
    worker = handle;
    LeaveCriticalSection(&lock);

    if (!handle)
        publish(UPDATE_FAILED, "Could not start the update thread");
}

void update_bi::checkAsync(HWND notify)
{
    startWorker(notify, false);
}

void update_bi::downloadAsync(HWND notify)
{
    startWorker(notify, true);
}

DWORD WINAPI update_bi::threadEntry(LPVOID parameter)
{
    update_bi *self = (update_bi *)parameter;

    EnterCriticalSection(&self->lock);
    bool download = self->wantDownload;
    LeaveCriticalSection(&self->lock);

    if (download)
        self->runDownload();
    else
        self->runCheck();

    return 0;
}

void update_bi::runCheck()
{
    std::string body;
    if (!fetchUrl(API_URL, URL_API, NULL, &body,
                  &cancelled, NULL, MAX_METADATA_BYTES))
    {
        publish(UPDATE_FAILED, cancelledNow(&cancelled)
                                   ? "Update check cancelled"
                                   : "Could not reach GitHub securely");
        return;
    }

    release_info_bi release;
    json_reader_bi reader(body);
    if (!reader.parseRelease(&release))
    {
        publish(UPDATE_FAILED, "Invalid release metadata");
        return;
    }

    int major = 0;
    int minor = 0;
    int patch = 0;
    if (!parseVersion(release.tag, &major, &minor, &patch))
    {
        publish(UPDATE_FAILED, "Release tag is not a version number");
        return;
    }

    EnterCriticalSection(&lock);
    version = release.tag;
    assetUrl.clear();
    checksumUrl.clear();
    expectedSha256.clear();
    stagedSigned = false;
    LeaveCriticalSection(&lock);

    if (!isNewer(major, minor, patch))
    {
        publish(UPDATE_CURRENT, "You have the newest release");
        return;
    }

    std::string exeUrl;
    std::string hashUrl;
    if (!selectReleaseAssets(release, &exeUrl, &hashUrl))
    {
        publish(UPDATE_FAILED,
                "Release must contain exactly kestrel.exe and "
                "kestrel.exe.sha256");
        return;
    }

    EnterCriticalSection(&lock);
    assetUrl = exeUrl;
    checksumUrl = hashUrl;
    LeaveCriticalSection(&lock);

    log_bi::write("update: %s available (current %s)",
                  release.tag.c_str(), APP_VERSION_STRING);
    publish(UPDATE_AVAILABLE, "Update available");
}

void update_bi::runDownload()
{
    EnterCriticalSection(&lock);
    std::string exeUrl = assetUrl;
    std::string hashUrl = checksumUrl;
    expectedSha256.clear();
    stagedSigned = false;
    LeaveCriticalSection(&lock);

    if (exeUrl.empty() || hashUrl.empty())
    {
        publish(UPDATE_FAILED, "Nothing to download");
        return;
    }

    std::wstring staged = stagedPath();
    if (staged.empty())
    {
        publish(UPDATE_FAILED, "Cannot locate the program folder");
        return;
    }

    DeleteFileW(staged.c_str());

    std::string checksumBody;
    if (!fetchUrl(hashUrl, URL_RELEASE_ASSET, CHECKSUM_ASSET_NAME,
                  &checksumBody, &cancelled, NULL, MAX_CHECKSUM_BYTES))
    {
        publish(UPDATE_FAILED, cancelledNow(&cancelled)
                                   ? "Download cancelled"
                                   : "Could not download the release checksum");
        return;
    }

    std::string expectedHash;
    if (!parseChecksum(checksumBody, &expectedHash))
    {
        publish(UPDATE_FAILED, "Release checksum is invalid");
        return;
    }

    std::string payload;
    if (!fetchUrl(exeUrl, URL_RELEASE_ASSET, EXE_ASSET_NAME,
                  &payload, &cancelled, &progress, MAX_BINARY_BYTES))
    {
        publish(UPDATE_FAILED, cancelledNow(&cancelled)
                                   ? "Download cancelled"
                                   : "Download failed secure URL validation");
        return;
    }

    if (payload.size() < MIN_BINARY_BYTES ||
        payload.size() > MAX_BINARY_BYTES ||
        payload.compare(0, 2, "MZ") != 0)
    {
        publish(UPDATE_FAILED, "Downloaded file is not a Windows program");
        return;
    }

    std::string payloadHash;
    if (!sha256Bytes(payload, &payloadHash) ||
        !hashesEqual(payloadHash, expectedHash))
    {
        publish(UPDATE_FAILED, "Downloaded file checksum does not match");
        return;
    }

    if (!writeNewFile(staged, payload))
    {
        publish(UPDATE_FAILED, "Cannot write to the program folder");
        return;
    }

    HANDLE verified = INVALID_HANDLE_VALUE;
    std::string stagedHash;
    bool signedFile = false;
    if (!openVerifiedExecutable(staged, expectedHash, -1,
                                &verified, &stagedHash, &signedFile))
    {
        DeleteFileW(staged.c_str());
        publish(UPDATE_FAILED,
                "Staged update failed checksum or signature verification");
        return;
    }
    CloseHandle(verified);

    if (cancelledNow(&cancelled))
    {
        DeleteFileW(staged.c_str());
        publish(UPDATE_FAILED, "Download cancelled");
        return;
    }

    EnterCriticalSection(&lock);
    expectedSha256 = expectedHash;
    stagedSigned = signedFile;
    LeaveCriticalSection(&lock);

    InterlockedExchange(&progress, 100);

    log_bi::write("update: staged %u bytes (%s)",
                  (unsigned)payload.size(),
                  signedFile ? "Authenticode valid" : "unsigned, SHA-256 verified");
    publish(UPDATE_READY, "Ready to install");
}

bool update_bi::apply(HANDLE *verifiedGuardOut)
{
    if (!verifiedGuardOut)
        return false;
    *verifiedGuardOut = INVALID_HANDLE_VALUE;

    // A verified file handle does not bind CreateProcess to every ancestor
    // path component. In an elevated process, require a directory that the
    // linked medium-integrity user cannot replace or redirect.
    if (autostart_bi::isElevated() &&
        !autostart_bi::executablePathProtected())
    {
        log_bi::write(
            "update: elevated installation refused from an unprotected path");
        return false;
    }

    std::wstring exe = paths_bi::exePathWide();
    std::wstring staged = stagedPath();
    std::wstring backup = backupPath();

    EnterCriticalSection(&lock);
    std::string expectedHash = expectedSha256;
    bool expectedSigned = stagedSigned;
    LeaveCriticalSection(&lock);

    if (exe.empty() || staged.empty() || backup.empty() ||
        expectedHash.empty())
    {
        return false;
    }

    // Verify the staged path immediately before replacement. The installed
    // path is verified again after the move, covering a path swap in between.
    HANDLE verified = INVALID_HANDLE_VALUE;
    if (!openVerifiedExecutable(staged, expectedHash,
                                expectedSigned ? 1 : 0,
                                &verified, NULL, NULL))
    {
        log_bi::write("update: staged file changed before installation");
        return false;
    }
    CloseHandle(verified);

    DeleteFileW(backup.c_str());

    if (!MoveFileExW(exe.c_str(), backup.c_str(),
                     MOVEFILE_REPLACE_EXISTING))
    {
        log_bi::writeErr(GetLastError(),
                         "update: cannot move the running program aside");
        return false;
    }

    if (!MoveFileExW(staged.c_str(), exe.c_str(),
                     MOVEFILE_REPLACE_EXISTING))
    {
        log_bi::writeErr(GetLastError(),
                         "update: cannot put the new program in place");
        MoveFileExW(backup.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }

    // The caller keeps this deny-write/delete handle open through process
    // creation, after it has torn down this process and released its mutex.
    HANDLE installed = INVALID_HANDLE_VALUE;
    if (!openVerifiedExecutable(exe, expectedHash,
                                expectedSigned ? 1 : 0,
                                &installed, NULL, NULL))
    {
        log_bi::write("update: installed file failed final verification");
        restoreAfterFailedInstall(exe, staged, backup);
        return false;
    }

    log_bi::write("update: installed verified release");
    *verifiedGuardOut = installed;
    return true;
}

bool update_bi::rollback(HANDLE *verifiedGuardOut)
{
    if (!verifiedGuardOut)
        return false;
    *verifiedGuardOut = INVALID_HANDLE_VALUE;

    if (autostart_bi::isElevated() &&
        !autostart_bi::executablePathProtected())
    {
        log_bi::write(
            "rollback: elevated replacement refused from an unprotected path");
        return false;
    }

    std::wstring exe = paths_bi::exePathWide();
    std::wstring backup = backupPath();
    std::wstring staged = stagedPath();

    if (exe.empty() || backup.empty() || !backupExists())
        return false;

    HANDLE verified = INVALID_HANDLE_VALUE;
    std::string backupHash;
    bool backupSigned = false;
    if (!openVerifiedExecutable(backup, std::string(), -1,
                                &verified, &backupHash, &backupSigned))
    {
        log_bi::write("rollback: backup is not a valid trusted executable");
        return false;
    }
    CloseHandle(verified);

    DeleteFileW(staged.c_str());

    if (!MoveFileExW(exe.c_str(), staged.c_str(),
                     MOVEFILE_REPLACE_EXISTING))
    {
        log_bi::writeErr(GetLastError(),
                         "rollback: cannot move the current program aside");
        return false;
    }

    if (!MoveFileExW(backup.c_str(), exe.c_str(),
                     MOVEFILE_REPLACE_EXISTING))
    {
        log_bi::writeErr(GetLastError(),
                         "rollback: cannot restore the previous program");
        MoveFileExW(staged.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }

    HANDLE restored = INVALID_HANDLE_VALUE;
    if (!openVerifiedExecutable(exe, backupHash,
                                backupSigned ? 1 : 0,
                                &restored, NULL, NULL))
    {
        log_bi::write("rollback: restored path failed final verification");
        MoveFileExW(exe.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING);
        MoveFileExW(staged.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }

    log_bi::write("rollback: verified previous version restored");
    *verifiedGuardOut = restored;
    return true;
}
