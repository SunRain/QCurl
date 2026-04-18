#include "cli_parse.h"

#include <cstdint>
#include <cstdio>
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Args
{
    int count = 0;
    std::string requestId;
    std::string proto;
    std::string urlPrefix;
    std::string summaryOut;
    long maxConnects = 1;
    long maxHostConnections = 0;
    long maxTotalConnections = 0;
    long maxConcurrentStreams = 0;
};

struct Transfer
{
    int index = 0;
    std::string url;
    std::ofstream file;
    std::uint64_t written = 0;
    CURLcode result       = CURLE_OK;
    long localPort        = 0;
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

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *t = static_cast<Transfer *>(userdata);
    if (!t || !t->file.is_open()) {
        return 0;
    }

    const std::uint64_t total = static_cast<std::uint64_t>(size)
                                * static_cast<std::uint64_t>(nmemb);
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

std::optional<long> parseLongArg(const char *raw)
{
    const auto parsed = qcurl::lc::parseInt<long>(raw ? std::string_view(raw) : std::string_view());
    if (!parsed.has_value() || *parsed < 0) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<Args> parseArgs(int argc, char **argv)
{
    Args out;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-n" && i + 1 < argc) {
            const auto count = qcurl::lc::parseInt<int>(argv[++i]);
            if (!count.has_value()) {
                return std::nullopt;
            }
            out.count = *count;
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
        if (arg == "--summary-out" && i + 1 < argc) {
            out.summaryOut = argv[++i];
            continue;
        }
        if (arg == "--maxconnects" && i + 1 < argc) {
            const auto value = parseLongArg(argv[++i]);
            if (!value.has_value()) {
                return std::nullopt;
            }
            out.maxConnects = *value;
            continue;
        }
        if (arg == "--max-host-connections" && i + 1 < argc) {
            const auto value = parseLongArg(argv[++i]);
            if (!value.has_value()) {
                return std::nullopt;
            }
            out.maxHostConnections = *value;
            continue;
        }
        if (arg == "--max-total-connections" && i + 1 < argc) {
            const auto value = parseLongArg(argv[++i]);
            if (!value.has_value()) {
                return std::nullopt;
            }
            out.maxTotalConnections = *value;
            continue;
        }
        if (arg == "--max-concurrent-streams" && i + 1 < argc) {
            const auto value = parseLongArg(argv[++i]);
            if (!value.has_value()) {
                return std::nullopt;
            }
            out.maxConcurrentStreams = *value;
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
    std::cerr
        << "Usage: qcurl_lc_multi_get4_baseline -n <count> -I <req_id> -V <h2|h3|http/1.1> "
           "[--summary-out <path>] [--maxconnects <n>] [--max-host-connections <n>] "
           "[--max-total-connections <n>] [--max-concurrent-streams <n>] <url_prefix>\n";
    return 2;
}

void recordTransferResult(CURL *easy, CURLcode code)
{
    Transfer *t = nullptr;
    curl_easy_getinfo(easy, CURLINFO_PRIVATE, &t);
    if (!t) {
        return;
    }
    t->result = code;
    long localPort = 0;
    if (curl_easy_getinfo(easy, CURLINFO_LOCAL_PORT, &localPort) == CURLE_OK) {
        t->localPort = localPort;
    }
}

bool writeConnectionSummary(const Args &args, const std::vector<Transfer> &transfers)
{
    if (args.summaryOut.empty()) {
        return true;
    }

    std::vector<int> connSeq;
    connSeq.reserve(transfers.size());
    std::vector<long> localPorts;
    localPorts.reserve(transfers.size());
    std::vector<long> seenPorts;
    int nextConnId = 1;

    for (const auto &transfer : transfers) {
        if (transfer.result != CURLE_OK || transfer.localPort <= 0) {
            return false;
        }
        localPorts.push_back(transfer.localPort);
        int connId = 0;
        for (std::size_t idx = 0; idx < seenPorts.size(); ++idx) {
            if (seenPorts[idx] == transfer.localPort) {
                connId = static_cast<int>(idx) + 1;
                break;
            }
        }
        if (connId == 0) {
            seenPorts.push_back(transfer.localPort);
            connId = nextConnId++;
        }
        connSeq.push_back(connId);
    }

    std::ofstream out(args.summaryOut, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << "{\n";
    out << "  \"request_count\": " << transfers.size() << ",\n";
    out << "  \"unique_connections\": " << seenPorts.size() << ",\n";
    out << "  \"local_ports\": [";
    for (std::size_t i = 0; i < localPorts.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << localPorts[i];
    }
    out << "],\n";
    out << "  \"conn_seq\": [";
    for (std::size_t i = 0; i < connSeq.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << connSeq[i];
    }
    out << "]\n";
    out << "}\n";
    return out.good();
}

} // namespace

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

    curl_multi_setopt(multi, CURLMOPT_MAXCONNECTS, args.maxConnects);
#ifdef CURLMOPT_PIPELINING
    curl_multi_setopt(multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
#endif
#ifdef CURLMOPT_MAX_HOST_CONNECTIONS
    if (args.maxHostConnections > 0) {
        curl_multi_setopt(multi, CURLMOPT_MAX_HOST_CONNECTIONS, args.maxHostConnections);
    }
#endif
#ifdef CURLMOPT_MAX_TOTAL_CONNECTIONS
    if (args.maxTotalConnections > 0) {
        curl_multi_setopt(multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, args.maxTotalConnections);
    }
#endif
#ifdef CURLMOPT_MAX_CONCURRENT_STREAMS
    if (args.maxConcurrentStreams > 0) {
        curl_multi_setopt(multi, CURLMOPT_MAX_CONCURRENT_STREAMS, args.maxConcurrentStreams);
    }
#endif

    std::vector<CURL *> handles;
    std::vector<Transfer> transfers;
    handles.reserve(static_cast<std::size_t>(args.count));
    transfers.reserve(static_cast<std::size_t>(args.count));

    for (int i = 0; i < args.count; ++i) {
        transfers.emplace_back();
        auto &transfer = transfers.back();
        transfer.index = i;
        transfer.url = appendRequestId(args.urlPrefix + formatSuffix(i + 1), args.requestId);

        const std::string outFile = "download_" + std::to_string(i) + ".data";
        transfer.file.open(outFile, std::ios::binary | std::ios::trunc);
        if (!transfer.file.is_open()) {
            std::cerr << "failed to open output file: " << outFile << "\n";
            curl_multi_cleanup(multi);
            curl_global_cleanup();
            return 5;
        }

        CURL *easy = curl_easy_init();
        if (!easy) {
            std::cerr << "curl_easy_init failed\n";
            curl_multi_cleanup(multi);
            curl_global_cleanup();
            return 6;
        }

        curl_easy_setopt(easy, CURLOPT_URL, transfer.url.c_str());
        curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 60000L);
#ifdef CURLOPT_PIPEWAIT
        curl_easy_setopt(easy, CURLOPT_PIPEWAIT, 1L);
#endif
        if (!setHttpVersion(easy, args.proto)) {
            curl_easy_cleanup(easy);
            curl_multi_cleanup(multi);
            curl_global_cleanup();
            std::cerr << "unknown proto: " << args.proto << "\n";
            return 7;
        }

        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &writeCallback);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, &transfer);
        curl_easy_setopt(easy, CURLOPT_PRIVATE, &transfer);
        if (curl_multi_add_handle(multi, easy) != CURLM_OK) {
            curl_easy_cleanup(easy);
            curl_multi_cleanup(multi);
            curl_global_cleanup();
            std::cerr << "curl_multi_add_handle failed\n";
            return 8;
        }
        handles.push_back(easy);
    }

    int running = 0;
    bool multiOk = true;
    curl_multi_perform(multi, &running);
    while (running > 0) {
        int numfds = 0;
        if (curl_multi_poll(multi, nullptr, 0, 1000, &numfds) != CURLM_OK) {
            std::cerr << "curl_multi_poll failed\n";
            multiOk = false;
            break;
        }
        curl_multi_perform(multi, &running);

        int msgsLeft = 0;
        while (CURLMsg *msg = curl_multi_info_read(multi, &msgsLeft)) {
            if (msg->msg == CURLMSG_DONE) {
                recordTransferResult(msg->easy_handle, msg->data.result);
            }
        }
    }

    int msgsLeft = 0;
    while (CURLMsg *msg = curl_multi_info_read(multi, &msgsLeft)) {
        if (msg->msg == CURLMSG_DONE) {
            recordTransferResult(msg->easy_handle, msg->data.result);
        }
    }

    bool ok = writeConnectionSummary(args, transfers);
    for (CURL *easy : handles) {
        curl_multi_remove_handle(multi, easy);
        curl_easy_cleanup(easy);
    }
    curl_multi_cleanup(multi);
    curl_global_cleanup();

    if (!ok) {
        std::cerr << "failed to write connection summary\n";
        return 9;
    }
    if (!multiOk) {
        return 10;
    }
    for (const auto &transfer : transfers) {
        if (transfer.result != CURLE_OK) {
            std::cerr << "transfer failed: " << transfer.url << " => "
                      << curl_easy_strerror(transfer.result) << "\n";
            return 11;
        }
    }
    return 0;
}
