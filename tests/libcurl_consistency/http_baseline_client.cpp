#include <curl/curl.h>

#include <cctype>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

namespace {

struct Args {
    std::string proto = "http/1.1";
    std::string method = "GET";
    std::string url;
    std::string outFile = "download_0.data";
    std::string headerOutFile = "response_headers_0.data";
    std::string progressOutFile;
    std::string proxy;
    std::string proxyUser;
    std::string proxyPass;
    std::string cookieFile;
    std::string cookieJar;
    std::string caInfo;
    bool followLocation = false;
    long maxRedirs = 10;
    bool verifyPeer = false;
    bool verifyHost = false;
    long connectTimeoutMs = -1;
    long totalTimeoutMs = -1;
    long lowSpeedTimeS = -1;
    long lowSpeedLimit = -1;
    long abortAfterBytes = -1;
    long dataSize = -1;
    int repeat = 1;
};

struct TransferSummary {
    curl_off_t dlNowMax = 0;
    curl_off_t dlTotalMax = 0;
    curl_off_t ulNowMax = 0;
    curl_off_t ulTotalMax = 0;
    curl_off_t dlPrev = -1;
    curl_off_t ulPrev = -1;
    bool dlMonotonic = true;
    bool ulMonotonic = true;
    int dlEvents = 0;
    int ulEvents = 0;
};

struct ProgressContext {
    curl_off_t abortAfterBytes = -1;
    bool enableAbort = false;
    bool enableRecord = false;
    TransferSummary summary;
};

bool setHttpVersion(CURL *curl, const std::string &proto)
{
    if (proto == "http/1.1") {
        return curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1) == CURLE_OK;
    }
    if (proto == "h2") {
        return curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS) == CURLE_OK;
    }
    if (proto == "h3") {
#ifdef CURL_HTTP_VERSION_3ONLY
        return curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3ONLY) == CURLE_OK;
#else
        return curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3) == CURLE_OK;
#endif
    }
    return false;
}

int xferInfoCallback(void *userdata,
                     curl_off_t dltotal, curl_off_t dlnow,
                     curl_off_t ultotal, curl_off_t ulnow)
{
    auto *ctx = static_cast<ProgressContext *>(userdata);
    if (!ctx) {
        return 0;
    }

    if (ctx->enableRecord) {
        if (ctx->summary.dlPrev >= 0 && dlnow < ctx->summary.dlPrev) {
            ctx->summary.dlMonotonic = false;
        }
        if (ctx->summary.ulPrev >= 0 && ulnow < ctx->summary.ulPrev) {
            ctx->summary.ulMonotonic = false;
        }
        ctx->summary.dlPrev = dlnow;
        ctx->summary.ulPrev = ulnow;

        if (dlnow > ctx->summary.dlNowMax) {
            ctx->summary.dlNowMax = dlnow;
        }
        if (dltotal > ctx->summary.dlTotalMax) {
            ctx->summary.dlTotalMax = dltotal;
        }
        if (ulnow > ctx->summary.ulNowMax) {
            ctx->summary.ulNowMax = ulnow;
        }
        if (ultotal > ctx->summary.ulTotalMax) {
            ctx->summary.ulTotalMax = ultotal;
        }

        ++ctx->summary.dlEvents;
        ++ctx->summary.ulEvents;
    }

    if (ctx->enableAbort && ctx->abortAfterBytes > 0 && dlnow >= ctx->abortAfterBytes) {
        return 1;  // 中止传输（CURLE_ABORTED_BY_CALLBACK）
    }
    return 0;
}

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *out = static_cast<std::ofstream *>(userdata);
    if (!out || !out->is_open()) {
        return 0;
    }
    const size_t total = size * nmemb;
    if (total == 0) {
        return 0;
    }
    out->write(ptr, static_cast<std::streamsize>(total));
    if (!out->good()) {
        return 0;
    }
    return total;
}

size_t headerCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *buf = static_cast<std::string *>(userdata);
    if (!buf) {
        return 0;
    }
    const size_t total = size * nmemb;
    if (total == 0) {
        return 0;
    }
    buf->append(ptr, total);
    return total;
}

std::string toUpperAscii(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<char>(std::toupper(c)));
    }
    return out;
}

std::optional<Args> parseArgs(int argc, char **argv)
{
    Args out;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-V" && i + 1 < argc) {
            out.proto = argv[++i];
            continue;
        }
        if (arg == "--method" && i + 1 < argc) {
            out.method = toUpperAscii(argv[++i]);
            continue;
        }
        if (arg == "--out" && i + 1 < argc) {
            out.outFile = argv[++i];
            continue;
        }
        if (arg == "--header-out" && i + 1 < argc) {
            out.headerOutFile = argv[++i];
            continue;
        }
        if (arg == "--progress-out" && i + 1 < argc) {
            out.progressOutFile = argv[++i];
            continue;
        }
        if (arg == "--proxy" && i + 1 < argc) {
            out.proxy = argv[++i];
            continue;
        }
        if (arg == "--proxy-user" && i + 1 < argc) {
            out.proxyUser = argv[++i];
            continue;
        }
        if (arg == "--proxy-pass" && i + 1 < argc) {
            out.proxyPass = argv[++i];
            continue;
        }
        if (arg == "--cookiefile" && i + 1 < argc) {
            out.cookieFile = argv[++i];
            continue;
        }
        if (arg == "--cookiejar" && i + 1 < argc) {
            out.cookieJar = argv[++i];
            continue;
        }
        if (arg == "--cainfo" && i + 1 < argc) {
            out.caInfo = argv[++i];
            continue;
        }
        if (arg == "--connect-timeout-ms" && i + 1 < argc) {
            try {
                out.connectTimeoutMs = std::stol(argv[++i]);
            } catch (...) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--timeout-ms" && i + 1 < argc) {
            try {
                out.totalTimeoutMs = std::stol(argv[++i]);
            } catch (...) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--low-speed-time" && i + 1 < argc) {
            try {
                out.lowSpeedTimeS = std::stol(argv[++i]);
            } catch (...) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--low-speed-limit" && i + 1 < argc) {
            try {
                out.lowSpeedLimit = std::stol(argv[++i]);
            } catch (...) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--abort-after-bytes" && i + 1 < argc) {
            try {
                out.abortAfterBytes = std::stol(argv[++i]);
            } catch (...) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--data-size" && i + 1 < argc) {
            try {
                out.dataSize = std::stol(argv[++i]);
            } catch (...) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--repeat" && i + 1 < argc) {
            try {
                out.repeat = std::stoi(argv[++i]);
            } catch (...) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--follow") {
            out.followLocation = true;
            continue;
        }
        if (arg == "--max-redirs" && i + 1 < argc) {
            try {
                out.maxRedirs = std::stol(argv[++i]);
            } catch (...) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--secure") {
            out.verifyPeer = true;
            out.verifyHost = true;
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            return std::nullopt;
        }
        out.url = arg;
    }

    if (out.url.empty()) {
        return std::nullopt;
    }
    return out;
}

int printUsage()
{
    std::cerr
        << "Usage: qcurl_lc_http_baseline [-V <http/1.1|h2|h3>] [--out <file>]\n"
        << "  [--method <GET|HEAD|POST|PUT|PATCH|DELETE>] [--data-size <n>]\n"
        << "  [--header-out <file>]\n"
        << "  [--progress-out <file>] [--repeat <n>]\n"
        << "  [--proxy <proxy_url> --proxy-user <user> --proxy-pass <pass>]\n"
        << "  [--cookiefile <path>] [--cookiejar <path>]\n"
        << "  [--follow] [--max-redirs <n>]\n"
        << "  [--secure] [--cainfo <path>]\n"
        << "  [--connect-timeout-ms <ms>] [--timeout-ms <ms>]\n"
        << "  [--low-speed-time <s>] [--low-speed-limit <bytes_per_sec>]\n"
        << "  [--abort-after-bytes <n>]\n"
        << "  <url>\n";
    return 2;
}

bool isSupportedMethod(const std::string &method)
{
    return method == "GET"
        || method == "HEAD"
        || method == "POST"
        || method == "PUT"
        || method == "PATCH"
        || method == "DELETE";
}

bool writeProgressJson(const std::string &path, const TransferSummary &s)
{
    std::ofstream fp(path, std::ios::binary | std::ios::trunc);
    if (!fp.is_open()) {
        return false;
    }
    fp << "{"
       << "\"download\":{"
       << "\"monotonic\":" << (s.dlMonotonic ? "true" : "false") << ","
       << "\"now_max\":" << static_cast<long long>(s.dlNowMax) << ","
       << "\"total_max\":" << static_cast<long long>(s.dlTotalMax) << ","
       << "\"events_count\":" << s.dlEvents
       << "},"
       << "\"upload\":{"
       << "\"monotonic\":" << (s.ulMonotonic ? "true" : "false") << ","
       << "\"now_max\":" << static_cast<long long>(s.ulNowMax) << ","
       << "\"total_max\":" << static_cast<long long>(s.ulTotalMax) << ","
       << "\"events_count\":" << s.ulEvents
       << "}"
       << "}\n";
    fp.flush();
    return fp.good();
}

}  // namespace

int main(int argc, char **argv)
{
    const auto argsOpt = parseArgs(argc, argv);
    if (!argsOpt.has_value()) {
        return printUsage();
    }
    const Args args = *argsOpt;
    if (!isSupportedMethod(args.method)) {
        return printUsage();
    }
    if (args.repeat < 1) {
        return printUsage();
    }
    if ((args.method == "GET" || args.method == "HEAD") && args.dataSize > 0) {
        return printUsage();
    }

    const CURLcode g = curl_global_init(CURL_GLOBAL_ALL);
    if (g != CURLE_OK) {
        std::cerr << "curl_global_init failed: " << curl_easy_strerror(g) << "\n";
        return 3;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        curl_global_cleanup();
        std::cerr << "curl_easy_init failed\n";
        return 5;
    }

    curl_easy_setopt(curl, CURLOPT_URL, args.url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, args.followLocation ? 1L : 0L);
    if (args.followLocation) {
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, args.maxRedirs);
    }
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, args.verifyPeer ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, args.verifyHost ? 2L : 0L);
    if (args.verifyPeer && !args.caInfo.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, args.caInfo.c_str());
    }
    if (args.connectTimeoutMs > 0) {
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, args.connectTimeoutMs);
    }
    if (args.totalTimeoutMs > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, args.totalTimeoutMs);
    }
    if (args.lowSpeedTimeS > 0) {
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, args.lowSpeedTimeS);
    }
    if (args.lowSpeedLimit > 0) {
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, args.lowSpeedLimit);
    }
    std::string body;
    if (args.dataSize > 0) {
        body.assign(static_cast<std::size_t>(args.dataSize), 'x');
    }

    if (args.method == "HEAD") {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    } else if (args.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    } else if (args.method == "PUT" || args.method == "PATCH" || args.method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, args.method.c_str());
        if (!body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
        }
    }

    ProgressContext progressCtx;
    if (args.abortAfterBytes > 0) {
        progressCtx.abortAfterBytes = static_cast<curl_off_t>(args.abortAfterBytes);
        progressCtx.enableAbort = true;
    }
    if (!args.progressOutFile.empty()) {
        progressCtx.enableRecord = true;
    }
    if (progressCtx.enableAbort || progressCtx.enableRecord) {
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &xferInfoCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressCtx);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeCallback);

    std::string headerData;
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerData);

    if (!setHttpVersion(curl, args.proto)) {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        std::cerr << "unsupported proto: " << args.proto << "\n";
        return 6;
    }

    if (!args.proxy.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, args.proxy.c_str());
        if (!args.proxyUser.empty()) {
            curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME, args.proxyUser.c_str());
        }
        if (!args.proxyPass.empty()) {
            curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD, args.proxyPass.c_str());
        }
        if (!args.proxyUser.empty() || !args.proxyPass.empty()) {
            curl_easy_setopt(curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
        }
    }

    if (!args.cookieFile.empty()) {
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, args.cookieFile.c_str());
    }
    if (!args.cookieJar.empty()) {
        curl_easy_setopt(curl, CURLOPT_COOKIEJAR, args.cookieJar.c_str());
    }

    CURLcode rc = CURLE_OK;
    for (int i = 0; i < args.repeat; ++i) {
        headerData.clear();
        std::ofstream out(args.outFile, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            std::cerr << "failed to open output file: " << args.outFile << "\n";
            return 4;
        }
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
        rc = curl_easy_perform(curl);
        out.close();
        if (rc != CURLE_OK) {
            break;
        }
    }
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    if (!args.headerOutFile.empty()) {
        std::ofstream headerOut(args.headerOutFile, std::ios::binary | std::ios::trunc);
        if (headerOut.is_open()) {
            headerOut.write(headerData.data(), static_cast<std::streamsize>(headerData.size()));
            headerOut.close();
        } else {
            std::cerr << "failed to open header output file: " << args.headerOutFile << "\n";
        }
    }

    curl_easy_cleanup(curl);

    std::cerr << "curlcode=" << static_cast<int>(rc) << " http_code=" << httpCode << "\n";

    if (!args.progressOutFile.empty()) {
        if (!writeProgressJson(args.progressOutFile, progressCtx.summary)) {
            std::cerr << "failed to write progress output file: " << args.progressOutFile << "\n";
        }
    }

    if (rc != CURLE_OK) {
        curl_global_cleanup();
        std::cerr << "curl_easy_perform failed: " << curl_easy_strerror(rc) << "\n";
        return 7;
    }

    curl_global_cleanup();
    return 0;
}
