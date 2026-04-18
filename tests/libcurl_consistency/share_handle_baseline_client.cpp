#include "cli_parse.h"

#include <curl/curl.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Args
{
    std::string mode;
    std::string proto = "http/1.1";
    long port = 0;
    std::string requestId;
    int count = 64;
};

struct Transfer
{
    std::string body;
    long status = 0;
    CURLcode result = CURLE_OK;
};

bool setHttpVersion(CURL *curl, const std::string &proto)
{
    if (proto == "http/1.1") {
        return curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1) == CURLE_OK;
    }
    if (proto == "h2") {
        return curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE) == CURLE_OK;
    }
    return false;
}

std::optional<long> parseLongArg(const char *raw)
{
    const auto parsed = qcurl::lc::parseInt<long>(raw ? std::string_view(raw) : std::string_view());
    if (!parsed.has_value() || *parsed <= 0) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<int> parseIntArg(const char *raw)
{
    const auto parsed = qcurl::lc::parseInt<int>(raw ? std::string_view(raw) : std::string_view());
    if (!parsed.has_value() || *parsed <= 0) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<Args> parseArgs(int argc, char **argv)
{
    Args out;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            out.mode = argv[++i];
            continue;
        }
        if (arg == "--proto" && i + 1 < argc) {
            out.proto = argv[++i];
            continue;
        }
        if (arg == "--port" && i + 1 < argc) {
            const auto port = parseLongArg(argv[++i]);
            if (!port.has_value()) {
                return std::nullopt;
            }
            out.port = *port;
            continue;
        }
        if (arg == "--req-id" && i + 1 < argc) {
            out.requestId = argv[++i];
            continue;
        }
        if (arg == "--count" && i + 1 < argc) {
            const auto count = parseIntArg(argv[++i]);
            if (!count.has_value()) {
                return std::nullopt;
            }
            out.count = *count;
            continue;
        }
        return std::nullopt;
    }

    if (out.mode.empty() || out.port <= 0 || out.requestId.empty()) {
        return std::nullopt;
    }
    return out;
}

int printUsage()
{
    std::cerr << "Usage: qcurl_lc_share_handle_baseline --mode <cookie_disabled|cookie_enabled|cookie_concurrency> "
                 "--proto <http/1.1|h2> --port <port> --req-id <id> [--count <n>]\n";
    return 2;
}

size_t writeToString(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *body = static_cast<std::string *>(userdata);
    if (!body) {
        return 0;
    }
    const size_t total = size * nmemb;
    if (total == 0) {
        return 0;
    }
    body->append(ptr, total);
    return total;
}

std::string makeUrl(long port, const std::string &path, const std::string &reqId, int seq = -1)
{
    std::string url = "http://localhost:" + std::to_string(port) + path;
    const char *sep = "?";
    if (seq >= 0) {
        url += sep;
        url += "seq=" + std::to_string(seq);
        sep = "&";
    }
    url += sep;
    url += "id=" + reqId;
    return url;
}

bool configureEasy(CURL *easy, const Args &args, const std::string &url, std::string *body, CURLSH *share)
{
    if (!easy || !body) {
        return false;
    }
    curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 60000L);
    curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &writeToString);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, body);
    if (share) {
        curl_easy_setopt(easy, CURLOPT_SHARE, share);
    }
    return setHttpVersion(easy, args.proto);
}

bool performEasy(CURL *easy, Transfer &transfer)
{
    transfer.body.clear();
    transfer.result = curl_easy_perform(easy);
    if (transfer.result != CURLE_OK) {
        return false;
    }
    return curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &transfer.status) == CURLE_OK;
}

bool writeBodyFile(const std::string &path, const std::string &body)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
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

    const CURLcode globalRc = curl_global_init(CURL_GLOBAL_ALL);
    if (globalRc != CURLE_OK) {
        std::cerr << "curl_global_init failed: " << curl_easy_strerror(globalRc) << "\n";
        return 3;
    }

    CURLSH *share = nullptr;
    if (args.mode != "cookie_disabled") {
        share = curl_share_init();
        if (!share || curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE) != CURLSHE_OK) {
            std::cerr << "curl_share_init/share failed\n";
            if (share) {
                curl_share_cleanup(share);
            }
            curl_global_cleanup();
            return 4;
        }
    }

    Transfer loginTransfer;
    CURL *login = curl_easy_init();
    if (!login || !configureEasy(login, args, makeUrl(args.port, "/login", args.requestId), &loginTransfer.body, share)) {
        curl_easy_cleanup(login);
        if (share) {
            curl_share_cleanup(share);
        }
        curl_global_cleanup();
        return 5;
    }
    if (!performEasy(login, loginTransfer) || loginTransfer.status != 302) {
        std::cerr << "login failed: rc=" << curl_easy_strerror(loginTransfer.result)
                  << ", status=" << loginTransfer.status << "\n";
        curl_easy_cleanup(login);
        if (share) {
            curl_share_cleanup(share);
        }
        curl_global_cleanup();
        return 6;
    }
    curl_easy_cleanup(login);

    if (args.mode == "cookie_disabled" || args.mode == "cookie_enabled") {
        const bool expectCookie = (args.mode == "cookie_enabled");
        Transfer homeTransfer;
        CURL *home = curl_easy_init();
        if (!home || !configureEasy(home, args, makeUrl(args.port, "/home", args.requestId), &homeTransfer.body, share)) {
            curl_easy_cleanup(home);
            if (share) {
                curl_share_cleanup(share);
            }
            curl_global_cleanup();
            return 7;
        }
        if (!performEasy(home, homeTransfer)) {
            std::cerr << "home request failed: " << curl_easy_strerror(homeTransfer.result) << "\n";
            curl_easy_cleanup(home);
            if (share) {
                curl_share_cleanup(share);
            }
            curl_global_cleanup();
            return 8;
        }
        curl_easy_cleanup(home);
        if (share) {
            curl_share_cleanup(share);
        }
        curl_global_cleanup();

        const long expectedStatus = expectCookie ? 200 : 401;
        const std::string expectedBody = expectCookie ? "home-ok\n" : "missing cookie\n";
        if (homeTransfer.status != expectedStatus || homeTransfer.body != expectedBody) {
            std::cerr << "unexpected home response: status=" << homeTransfer.status
                      << ", body=" << homeTransfer.body << "\n";
            return 9;
        }
        return writeBodyFile("download_0.data", homeTransfer.body) ? 0 : 10;
    }

    if (args.mode != "cookie_concurrency" || !share) {
        if (share) {
            curl_share_cleanup(share);
        }
        curl_global_cleanup();
        return printUsage();
    }

    CURLM *multi = curl_multi_init();
    if (!multi) {
        curl_share_cleanup(share);
        curl_global_cleanup();
        return 11;
    }

    std::vector<CURL *> handles;
    std::vector<Transfer> transfers(static_cast<std::size_t>(args.count));
    handles.reserve(static_cast<std::size_t>(args.count));

    for (int i = 0; i < args.count; ++i) {
        CURL *easy = curl_easy_init();
        if (!easy || !configureEasy(easy, args, makeUrl(args.port, "/home", args.requestId, i), &transfers[static_cast<std::size_t>(i)].body, share)) {
            curl_easy_cleanup(easy);
            curl_multi_cleanup(multi);
            curl_share_cleanup(share);
            curl_global_cleanup();
            return 12;
        }
        curl_easy_setopt(easy, CURLOPT_PRIVATE, &transfers[static_cast<std::size_t>(i)]);
        if (curl_multi_add_handle(multi, easy) != CURLM_OK) {
            curl_easy_cleanup(easy);
            curl_multi_cleanup(multi);
            curl_share_cleanup(share);
            curl_global_cleanup();
            return 12;
        }
        handles.push_back(easy);
    }

    int running = 0;
    curl_multi_perform(multi, &running);
    while (running > 0) {
        int numfds = 0;
        if (curl_multi_poll(multi, nullptr, 0, 1000, &numfds) != CURLM_OK) {
            break;
        }
        curl_multi_perform(multi, &running);
        int msgsLeft = 0;
        while (CURLMsg *msg = curl_multi_info_read(multi, &msgsLeft)) {
            if (msg->msg != CURLMSG_DONE) {
                continue;
            }
            Transfer *transfer = nullptr;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &transfer);
            if (transfer) {
                transfer->result = msg->data.result;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &transfer->status);
            }
        }
    }

    int msgsLeft = 0;
    while (CURLMsg *msg = curl_multi_info_read(multi, &msgsLeft)) {
        if (msg->msg != CURLMSG_DONE) {
            continue;
        }
        Transfer *transfer = nullptr;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &transfer);
        if (transfer) {
            transfer->result = msg->data.result;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &transfer->status);
        }
    }

    int rc = 0;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        const auto &transfer = transfers[i];
        if (transfer.result != CURLE_OK || transfer.status != 200 || transfer.body != "home-ok\n") {
            std::cerr << "concurrency transfer failed at " << i << ": rc="
                      << curl_easy_strerror(transfer.result) << ", status=" << transfer.status << "\n";
            rc = 13;
        }
        curl_multi_remove_handle(multi, handles[i]);
        curl_easy_cleanup(handles[i]);
    }

    curl_multi_cleanup(multi);
    curl_share_cleanup(share);
    curl_global_cleanup();
    return rc;
}
