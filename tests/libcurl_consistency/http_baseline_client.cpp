#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstring>
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
    std::optional<std::string> acceptEncoding;  // "gzip,br" | ""(all)
    std::string user;
    std::string pass;
    std::string httpAuth;  // basic | any | anysafe
    bool unrestrictedAuth = false;
    std::string proxy;
    std::string proxyType = "http";
    std::string proxyUser;
    std::string proxyPass;
    std::string cookieFile;
    std::string cookieJar;
    std::string caInfo;
    std::string pinnedPublicKey;
    bool multipartDemo = false;
    bool followLocation = false;
    long maxRedirs = 10;
    bool autoReferer = false;
    std::optional<long> postRedir;
    std::string referer;
    bool streamBody = false;
    bool seekableBody = false;
    bool unknownSize = false;
    bool verifyPeer = false;
    bool verifyHost = false;
    long connectTimeoutMs = -1;
    long totalTimeoutMs = -1;
    long lowSpeedTimeS = -1;
    long lowSpeedLimit = -1;
    long abortAfterBytes = -1;
    long dataSize = -1;
    int repeat = 1;
    bool expect100Continue = false;
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

struct BodyStream {
    const char *data = nullptr;
    std::size_t len = 0;
    std::size_t offset = 0;

    void reset() { offset = 0; }

    std::size_t remaining() const
    {
        if (offset >= len) {
            return 0;
        }
        return len - offset;
    }
};

size_t readBodyCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *ctx = static_cast<BodyStream *>(userdata);
    if (!ctx || !ptr) {
        return 0;
    }
    const std::size_t max = size * nmemb;
    if (max == 0) {
        return 0;
    }
    const std::size_t remain = ctx->remaining();
    if (remain == 0) {
        return 0;
    }
    const std::size_t n = std::min(max, remain);
    std::memcpy(ptr, ctx->data + ctx->offset, n);
    ctx->offset += n;
    return n;
}

int seekBodyCallback(void *userdata, curl_off_t offset, int origin)
{
    auto *ctx = static_cast<BodyStream *>(userdata);
    if (!ctx) {
        return CURL_SEEKFUNC_FAIL;
    }

    curl_off_t base = 0;
    if (origin == SEEK_SET) {
        base = 0;
    } else if (origin == SEEK_CUR) {
        base = static_cast<curl_off_t>(ctx->offset);
    } else if (origin == SEEK_END) {
        base = static_cast<curl_off_t>(ctx->len);
    } else {
        return CURL_SEEKFUNC_FAIL;
    }

    const curl_off_t next = base + offset;
    if (next < 0 || next > static_cast<curl_off_t>(ctx->len)) {
        return CURL_SEEKFUNC_FAIL;
    }
    ctx->offset = static_cast<std::size_t>(next);
    return CURL_SEEKFUNC_OK;
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

std::string toLowerAscii(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

std::optional<unsigned long> httpAuthMask(const std::string &s)
{
    const std::string v = toLowerAscii(s);
    if (v == "basic") {
        return CURLAUTH_BASIC;
    }
    if (v == "any") {
        return CURLAUTH_ANY;
    }
    if (v == "anysafe") {
        return CURLAUTH_ANYSAFE;
    }
    return std::nullopt;
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
        if (arg == "--multipart-demo") {
            out.multipartDemo = true;
            out.method = "POST";
            continue;
        }
        if (arg == "--stream-body") {
            out.streamBody = true;
            continue;
        }
        if (arg == "--seekable-body") {
            out.seekableBody = true;
            continue;
        }
        if (arg == "--unknown-size") {
            out.unknownSize = true;
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
        if (arg == "--user" && i + 1 < argc) {
            out.user = argv[++i];
            continue;
        }
        if (arg == "--pass" && i + 1 < argc) {
            out.pass = argv[++i];
            continue;
        }
        if (arg == "--httpauth" && i + 1 < argc) {
            out.httpAuth = argv[++i];
            continue;
        }
        if (arg == "--unrestricted-auth") {
            out.unrestrictedAuth = true;
            continue;
        }
        if (arg == "--proxy" && i + 1 < argc) {
            out.proxy = argv[++i];
            continue;
        }
        if (arg == "--proxy-type" && i + 1 < argc) {
            out.proxyType = argv[++i];
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
        if (arg == "--pinned-public-key" && i + 1 < argc) {
            out.pinnedPublicKey = argv[++i];
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
        if (arg == "--expect100") {
            out.expect100Continue = true;
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
        if (arg == "--auto-referer") {
            out.autoReferer = true;
            continue;
        }
        if (arg == "--post-redir" && i + 1 < argc) {
            const std::string v = toLowerAscii(argv[++i]);
            if (v == "default") {
                out.postRedir.reset();
            } else if (v == "301") {
                out.postRedir = CURL_REDIR_POST_301;
            } else if (v == "302") {
                out.postRedir = CURL_REDIR_POST_302;
            } else if (v == "303") {
                out.postRedir = CURL_REDIR_POST_303;
            } else if (v == "all") {
                out.postRedir = CURL_REDIR_POST_ALL;
            } else {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--referer" && i + 1 < argc) {
            out.referer = argv[++i];
            continue;
        }
        if (arg == "--accept-encoding" && i + 1 < argc) {
            std::string v = argv[++i];
            if (toLowerAscii(v) == "all") {
                v.clear();  // 空字符串：让 libcurl 使用其内置支持的编码列表，并启用自动解压
            }
            out.acceptEncoding = v;
            continue;
        }
        if (arg == "--accept-encoding-all") {
            out.acceptEncoding = std::string();
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
        << "  [--stream-body [--seekable-body] [--unknown-size]]\n"
        << "  [--expect100]\n"
        << "  [--multipart-demo]\n"
        << "  [--header-out <file>]\n"
        << "  [--progress-out <file>] [--repeat <n>]\n"
        << "  [--user <user> --pass <pass> --httpauth <basic|any|anysafe>] [--unrestricted-auth]\n"
        << "  [--proxy <proxy_url> [--proxy-type <http|https|socks4|socks4a|socks5|socks5h>] --proxy-user <user> --proxy-pass <pass>]\n"
        << "  [--cookiefile <path>] [--cookiejar <path>]\n"
        << "  [--follow] [--max-redirs <n>]\n"
        << "  [--auto-referer] [--referer <url>] [--post-redir <default|301|302|303|all>]\n"
        << "  [--accept-encoding <csv|all>] [--accept-encoding-all]\n"
        << "  [--secure] [--cainfo <path>] [--pinned-public-key <sha256//...|path>]\n"
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

std::string makeMultipartDemoBinary()
{
    std::string out;
    out.resize(256);
    for (int i = 0; i < 256; ++i) {
        out[static_cast<std::size_t>(i)] = static_cast<char>(i);
    }
    return out;
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
    if (args.multipartDemo && args.dataSize > 0) {
        return printUsage();
    }
    if ((args.method == "GET" || args.method == "HEAD") && args.dataSize > 0) {
        return printUsage();
    }
    if (args.streamBody && args.multipartDemo) {
        return printUsage();
    }
    if (args.streamBody && !(args.method == "POST" || args.method == "PUT")) {
        return printUsage();
    }
    if (args.unknownSize && !args.streamBody) {
        return printUsage();
    }
    if (args.unknownSize && args.method != "POST") {
        return printUsage();
    }
    if (args.unknownSize && args.proto != "http/1.1") {
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
        if (args.autoReferer) {
            curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 1L);
        }
        if (args.postRedir.has_value()) {
            curl_easy_setopt(curl, CURLOPT_POSTREDIR, args.postRedir.value());
        }
    }
    if (!args.referer.empty()) {
        curl_easy_setopt(curl, CURLOPT_REFERER, args.referer.c_str());
    }
    if (args.acceptEncoding.has_value()) {
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, args.acceptEncoding.value().c_str());
    }
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, args.verifyPeer ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, args.verifyHost ? 2L : 0L);
    if (args.verifyPeer && !args.caInfo.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, args.caInfo.c_str());
    }
#ifdef CURLOPT_PINNEDPUBLICKEY
    if (!args.pinnedPublicKey.empty()) {
        curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, args.pinnedPublicKey.c_str());
    }
#else
    if (!args.pinnedPublicKey.empty()) {
        std::cerr << "unsupported pinned public key (no CURLOPT_PINNEDPUBLICKEY)\n";
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return 6;
    }
#endif
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
    BodyStream bodyStream;
    bodyStream.data = body.data();
    bodyStream.len = body.size();

    curl_mime *mime = nullptr;
    curl_slist *headers = nullptr;
    std::string multipartBinary;
    if (args.multipartDemo) {
        multipartBinary = makeMultipartDemoBinary();
        mime = curl_mime_init(curl);
        if (!mime) {
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            std::cerr << "curl_mime_init failed\n";
            return 5;
        }

        curl_mimepart *p1 = curl_mime_addpart(mime);
        curl_mime_name(p1, "alpha");
        curl_mime_data(p1, "hello", CURL_ZERO_TERMINATED);

        curl_mimepart *p2 = curl_mime_addpart(mime);
        curl_mime_name(p2, "beta");
        curl_mime_data(p2, "world", CURL_ZERO_TERMINATED);

        curl_mimepart *p3 = curl_mime_addpart(mime);
        curl_mime_name(p3, "file");
        curl_mime_filename(p3, "a.bin");
        curl_mime_type(p3, "application/octet-stream");
        curl_mime_data(p3, multipartBinary.data(), multipartBinary.size());

        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    }

    if (args.expect100Continue) {
        headers = curl_slist_append(headers, "Expect: 100-continue");
        if (!headers) {
            if (mime) {
                curl_mime_free(mime);
            }
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            std::cerr << "curl_slist_append failed\n";
            return 5;
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    if (args.method == "HEAD") {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    } else if (args.streamBody) {
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, &readBodyCallback);
        curl_easy_setopt(curl, CURLOPT_READDATA, &bodyStream);
        if (args.seekableBody) {
            curl_easy_setopt(curl, CURLOPT_SEEKFUNCTION, &seekBodyCallback);
            curl_easy_setopt(curl, CURLOPT_SEEKDATA, &bodyStream);
        }

        if (args.method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, nullptr);
            const curl_off_t postSize = args.unknownSize ? static_cast<curl_off_t>(-1) : static_cast<curl_off_t>(bodyStream.len);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, postSize);
        } else if (args.method == "PUT") {
            curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(bodyStream.len));
        }
    } else if (args.method == "POST" && !args.multipartDemo) {
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

    if (!args.user.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, args.user.c_str());
    }
    if (!args.pass.empty()) {
        curl_easy_setopt(curl, CURLOPT_PASSWORD, args.pass.c_str());
    }
    if (!args.httpAuth.empty()) {
        const auto maskOpt = httpAuthMask(args.httpAuth);
        if (!maskOpt.has_value()) {
            std::cerr << "unsupported httpauth: " << args.httpAuth << "\n";
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return 6;
        }
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, maskOpt.value());
    }
    if (args.unrestrictedAuth && args.followLocation) {
        curl_easy_setopt(curl, CURLOPT_UNRESTRICTED_AUTH, 1L);
    }

    if (!args.proxy.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, args.proxy.c_str());
        long proxyType = CURLPROXY_HTTP;
        if (args.proxyType == "http") {
            proxyType = CURLPROXY_HTTP;
        } else if (args.proxyType == "https") {
#ifdef CURLPROXY_HTTPS
            proxyType = CURLPROXY_HTTPS;
#else
            std::cerr << "unsupported proxy type (no CURLPROXY_HTTPS): " << args.proxyType << "\n";
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return 6;
#endif
        } else if (args.proxyType == "socks4") {
            proxyType = CURLPROXY_SOCKS4;
        } else if (args.proxyType == "socks4a") {
            proxyType = CURLPROXY_SOCKS4A;
        } else if (args.proxyType == "socks5") {
            proxyType = CURLPROXY_SOCKS5;
        } else if (args.proxyType == "socks5h" || args.proxyType == "socks5-hostname" || args.proxyType == "socks5_hostname") {
            proxyType = CURLPROXY_SOCKS5_HOSTNAME;
        } else {
            std::cerr << "unsupported proxy type: " << args.proxyType << "\n";
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return 6;
        }
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, proxyType);
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
        if (args.streamBody) {
            bodyStream.reset();
        }
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

    if (mime) {
        curl_mime_free(mime);
        mime = nullptr;
    }
    if (headers) {
        curl_slist_free_all(headers);
        headers = nullptr;
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
