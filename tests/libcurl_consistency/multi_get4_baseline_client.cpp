#include <curl/curl.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Args {
    int count = 0;
    std::string requestId;
    std::string proto;
    std::string urlPrefix;
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

std::string formatSuffix(int index1Based)
{
    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "%04d", index1Based);
    return std::string(buf);
}

std::string appendRequestId(const std::string &url, const std::string &requestId)
{
    if (requestId.empty()) {
        return url;
    }
    const char sep = (url.find('?') == std::string::npos) ? '?' : '&';
    return url + sep + "id=" + requestId;
}

struct Transfer {
    int index = 0;
    std::string url;
    std::ofstream file;
    std::uint64_t written = 0;
    CURLcode result = CURLE_OK;
};

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *t = static_cast<Transfer *>(userdata);
    if (!t || !t->file.is_open()) {
        return 0;
    }

    const std::uint64_t total = static_cast<std::uint64_t>(size) * static_cast<std::uint64_t>(nmemb);
    if (total == 0) {
        return 0;
    }

    t->file.write(ptr, static_cast<std::streamsize>(total));
    if (!t->file.good()) {
        return 0;
    }
    t->written += total;
    return static_cast<size_t>(total);
}

std::optional<Args> parseArgs(int argc, char **argv)
{
    Args out;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-n" && i + 1 < argc) {
            out.count = std::stoi(argv[++i]);
            continue;
        }
        if (arg == "-I" && i + 1 < argc) {
            out.requestId = argv[++i];
            continue;
        }
        if (arg == "-V" && i + 1 < argc) {
            out.proto = argv[++i];
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            return std::nullopt;
        }
        if (!out.urlPrefix.empty()) {
            return std::nullopt;
        }
        out.urlPrefix = arg;
    }

    if (out.count <= 0 || out.proto.empty() || out.urlPrefix.empty()) {
        return std::nullopt;
    }
    return out;
}

int printUsage()
{
    std::cerr << "Usage: qcurl_lc_multi_get4_baseline -n <count> -I <req_id> -V <h2|h3|http/1.1> <url_prefix>\n";
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

    CURLM *multi = curl_multi_init();
    if (!multi) {
        curl_global_cleanup();
        std::cerr << "curl_multi_init failed\n";
        return 4;
    }

    curl_multi_setopt(multi, CURLMOPT_MAXCONNECTS, 1L);

    std::vector<CURL *> handles;
    handles.reserve(static_cast<size_t>(args.count));
    std::vector<Transfer> transfers;
    transfers.reserve(static_cast<size_t>(args.count));

    for (int i = 0; i < args.count; ++i) {
        transfers.emplace_back();
        Transfer &t = transfers.back();
        t.index = i;
        t.url = appendRequestId(args.urlPrefix + formatSuffix(i + 1), args.requestId);

        const std::string outFile = "download_" + std::to_string(i) + ".data";
        t.file.open(outFile, std::ios::binary | std::ios::trunc);
        if (!t.file.is_open()) {
            curl_multi_cleanup(multi);
            curl_global_cleanup();
            std::cerr << "failed to open output file: " << outFile << "\n";
            return 5;
        }

        CURL *easy = curl_easy_init();
        if (!easy) {
            curl_multi_cleanup(multi);
            curl_global_cleanup();
            std::cerr << "curl_easy_init failed\n";
            return 6;
        }

        curl_easy_setopt(easy, CURLOPT_URL, t.url.c_str());
        curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 60000L);
        if (!setHttpVersion(easy, args.proto)) {
            curl_easy_cleanup(easy);
            curl_multi_cleanup(multi);
            curl_global_cleanup();
            std::cerr << "unknown proto: " << args.proto << "\n";
            return 7;
        }

        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &writeCallback);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, &t);
        curl_easy_setopt(easy, CURLOPT_PRIVATE, &t);

        const CURLMcode mc = curl_multi_add_handle(multi, easy);
        if (mc != CURLM_OK) {
            curl_easy_cleanup(easy);
            curl_multi_cleanup(multi);
            curl_global_cleanup();
            std::cerr << "curl_multi_add_handle failed: " << curl_multi_strerror(mc) << "\n";
            return 8;
        }
        handles.push_back(easy);
    }

    int running = 0;
    curl_multi_perform(multi, &running);

    while (running > 0) {
        int numfds = 0;
        const CURLMcode mc = curl_multi_poll(multi, nullptr, 0, 1000, &numfds);
        if (mc != CURLM_OK) {
            std::cerr << "curl_multi_poll failed: " << curl_multi_strerror(mc) << "\n";
            break;
        }
        curl_multi_perform(multi, &running);

        int msgsLeft = 0;
        while (CURLMsg *msg = curl_multi_info_read(multi, &msgsLeft)) {
            if (msg->msg != CURLMSG_DONE) {
                continue;
            }
            Transfer *t = nullptr;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &t);
            if (t) {
                t->result = msg->data.result;
            }
        }
    }

    int msgsLeft = 0;
    while (CURLMsg *msg = curl_multi_info_read(multi, &msgsLeft)) {
        if (msg->msg != CURLMSG_DONE) {
            continue;
        }
        Transfer *t = nullptr;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &t);
        if (t) {
            t->result = msg->data.result;
        }
    }

    bool ok = true;
    for (CURL *easy : handles) {
        curl_multi_remove_handle(multi, easy);
        curl_easy_cleanup(easy);
    }
    curl_multi_cleanup(multi);

    for (const auto &t : transfers) {
        if (t.result != CURLE_OK) {
            ok = false;
            std::cerr << "transfer failed: idx=" << t.index << " url=" << t.url
                      << " err=" << curl_easy_strerror(t.result) << "\n";
        }
        if (!t.file.good()) {
            ok = false;
            std::cerr << "output file not good: idx=" << t.index << "\n";
        }
        if (t.written == 0) {
            ok = false;
            std::cerr << "empty response: idx=" << t.index << " url=" << t.url << "\n";
        }
    }

    curl_global_cleanup();
    return ok ? 0 : 9;
}
