#include <curl/curl.h>

#include <fstream>
#include <iostream>
#include <optional>
#include <string>

namespace {

struct Args {
    std::string proto = "http/1.1";
    std::string url;
    std::string outFile = "download_0.data";
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

std::optional<Args> parseArgs(int argc, char **argv)
{
    Args out;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-V" && i + 1 < argc) {
            out.proto = argv[++i];
            continue;
        }
        if (arg == "--out" && i + 1 < argc) {
            out.outFile = argv[++i];
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
        << "  [--proxy <proxy_url> --proxy-user <user> --proxy-pass <pass>]\n"
        << "  [--cookiefile <path>] [--cookiejar <path>]\n"
        << "  [--follow] [--max-redirs <n>]\n"
        << "  [--secure] [--cainfo <path>]\n"
        << "  <url>\n";
    return 2;
}

}  // namespace

int main(int argc, char **argv)
{
    const auto argsOpt = parseArgs(argc, argv);
    if (!argsOpt.has_value()) {
        return printUsage();
    }
    const Args args = *argsOpt;

    const CURLcode g = curl_global_init(CURL_GLOBAL_ALL);
    if (g != CURLE_OK) {
        std::cerr << "curl_global_init failed: " << curl_easy_strerror(g) << "\n";
        return 3;
    }

    std::ofstream out(args.outFile, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        curl_global_cleanup();
        std::cerr << "failed to open output file: " << args.outFile << "\n";
        return 4;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        out.close();
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
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);

    if (!setHttpVersion(curl, args.proto)) {
        curl_easy_cleanup(curl);
        out.close();
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

    const CURLcode rc = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_easy_cleanup(curl);
    out.close();

    std::cerr << "curlcode=" << static_cast<int>(rc) << " http_code=" << httpCode << "\n";

    if (rc != CURLE_OK) {
        curl_global_cleanup();
        std::cerr << "curl_easy_perform failed: " << curl_easy_strerror(rc) << "\n";
        return 7;
    }

    curl_global_cleanup();
    return 0;
}
