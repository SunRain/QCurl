#include <curl/curl.h>
#include <curl/websockets.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

enum class WsEventType {
    Text,
    Binary,
    Ping,
    Pong,
    Close,
};

struct WsEvent {
    WsEventType type;
    std::vector<std::uint8_t> payload;
};

std::string toEventName(WsEventType type)
{
    switch (type) {
    case WsEventType::Text:
        return "TEXT";
    case WsEventType::Binary:
        return "BINARY";
    case WsEventType::Ping:
        return "PING";
    case WsEventType::Pong:
        return "PONG";
    case WsEventType::Close:
        return "CLOSE";
    }
    return "UNKNOWN";
}

std::string hexEncode(const std::vector<std::uint8_t> &data)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (std::uint8_t b : data) {
        out.push_back(kHex[(b >> 4) & 0x0F]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

bool writeEventsFile(const std::string &path, const std::vector<WsEvent> &events)
{
    std::ofstream fp(path, std::ios::binary | std::ios::trunc);
    if (!fp.is_open()) {
        return false;
    }
    for (const WsEvent &e : events) {
        fp << toEventName(e.type) << " " << e.payload.size() << " " << hexEncode(e.payload) << "\n";
    }
    fp.flush();
    return fp.good();
}

struct BufferedMessage {
    unsigned int flags = 0;
    std::vector<std::uint8_t> data;
};

std::optional<WsEventType> eventTypeFromFlags(unsigned int flags)
{
    if (flags & CURLWS_TEXT) {
        return WsEventType::Text;
    }
    if (flags & CURLWS_BINARY) {
        return WsEventType::Binary;
    }
    if (flags & CURLWS_PING) {
        return WsEventType::Ping;
    }
    if (flags & CURLWS_PONG) {
        return WsEventType::Pong;
    }
    if (flags & CURLWS_CLOSE) {
        return WsEventType::Close;
    }
    return std::nullopt;
}

bool payloadEquals(const std::vector<std::uint8_t> &payload, const std::string &ascii)
{
    if (payload.size() != ascii.size()) {
        return false;
    }
    for (std::size_t i = 0; i < payload.size(); ++i) {
        if (payload[i] != static_cast<std::uint8_t>(ascii[i])) {
            return false;
        }
    }
    return true;
}

bool closePayloadEquals(const std::vector<std::uint8_t> &payload, std::uint16_t code, const std::string &reason)
{
    if (payload.size() != 2 + reason.size()) {
        return false;
    }
    const std::uint16_t gotCode = (static_cast<std::uint16_t>(payload[0]) << 8) | payload[1];
    if (gotCode != code) {
        return false;
    }
    for (std::size_t i = 0; i < reason.size(); ++i) {
        if (payload[2 + i] != static_cast<std::uint8_t>(reason[i])) {
            return false;
        }
    }
    return true;
}

int runScenario(const std::string &scenario, const std::string &url, const std::string &outFile, std::chrono::milliseconds timeout)
{
    CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
    if (res != CURLE_OK) {
        std::cerr << "curl_global_init failed: " << curl_easy_strerror(res) << "\n";
        return 2;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        curl_global_cleanup();
        std::cerr << "curl_easy_init failed\n";
        return 2;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(curl, CURLOPT_WS_OPTIONS, CURLWS_NOAUTOPONG);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout.count()));

    struct curl_slist *headers = nullptr;
    if (scenario == "lc_ping_deflate") {
        headers = curl_slist_append(headers, "Sec-WebSocket-Extensions: permessage-deflate");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    res = curl_easy_perform(curl);
    if (headers) {
        curl_slist_free_all(headers);
    }
    if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform failed: " << curl_easy_strerror(res) << "\n";
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return 3;
    }

    const auto started = std::chrono::steady_clock::now();
    std::vector<WsEvent> events;
    std::optional<BufferedMessage> buffered;

    for (;;) {
        if (std::chrono::steady_clock::now() - started > timeout) {
            std::cerr << "timeout waiting for frames\n";
            res = CURLE_OPERATION_TIMEDOUT;
            break;
        }

        std::uint8_t buf[4096];
        std::size_t nread = 0;
        const struct curl_ws_frame *meta = nullptr;
        res = curl_ws_recv(curl, buf, sizeof(buf), &nread, &meta);
        if (res == CURLE_AGAIN) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (res != CURLE_OK) {
            std::cerr << "curl_ws_recv failed: " << curl_easy_strerror(res) << "\n";
            break;
        }
        if (!meta) {
            continue;
        }

        const auto maybeType = eventTypeFromFlags(meta->flags);
        if (!maybeType.has_value()) {
            continue;
        }
        const WsEventType type = *maybeType;

        if (type == WsEventType::Ping) {
            std::size_t sent = 0;
            CURLcode sres = curl_ws_send(curl, buf, nread, &sent, 0, CURLWS_PONG);
            if (sres != CURLE_OK) {
                std::cerr << "curl_ws_send(PONG) failed: " << curl_easy_strerror(sres) << "\n";
                res = sres;
                break;
            }
        }

        const bool isDataFrame = (type == WsEventType::Text) || (type == WsEventType::Binary);
        if (!isDataFrame) {
            WsEvent e{type, std::vector<std::uint8_t>(buf, buf + nread)};
            events.push_back(std::move(e));
            if (type == WsEventType::Close) {
                break;
            }
            continue;
        }

        if (!buffered.has_value()) {
            buffered = BufferedMessage{static_cast<unsigned int>(meta->flags), {}};
        }
        buffered->data.insert(buffered->data.end(), buf, buf + nread);

        if (meta->bytesleft == 0) {
            WsEvent e{type, std::move(buffered->data)};
            events.push_back(std::move(e));
            buffered.reset();
        }
    }

    const bool okWrite = writeEventsFile(outFile, events);

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    if (res != CURLE_OK) {
        return 4;
    }
    if (!okWrite) {
        return 5;
    }

    if (scenario == "lc_ping" || scenario == "lc_ping_deflate") {
        if (events.size() != 2) {
            return 6;
        }
        if (events[0].type != WsEventType::Ping) {
            return 6;
        }
        if (!events[0].payload.empty()) {
            return 6;
        }
        if (events[1].type != WsEventType::Close) {
            return 6;
        }
        if (!closePayloadEquals(events[1].payload, 1000, "done")) {
            return 6;
        }
        return 0;
    }

    if (scenario == "lc_frame_types") {
        if (events.size() != 5) {
            return 7;
        }
        if (events[0].type != WsEventType::Text || !payloadEquals(events[0].payload, "txt")) {
            return 7;
        }
        if (events[1].type != WsEventType::Binary || !payloadEquals(events[1].payload, "bin")) {
            return 7;
        }
        if (events[2].type != WsEventType::Ping || !payloadEquals(events[2].payload, "ping")) {
            return 7;
        }
        if (events[3].type != WsEventType::Pong || !payloadEquals(events[3].payload, "pong")) {
            return 7;
        }
        if (events[4].type != WsEventType::Close || !closePayloadEquals(events[4].payload, 1000, "close")) {
            return 7;
        }
        return 0;
    }

    return 8;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 4) {
        std::cerr << "usage: qcurl_lc_ws_baseline <scenario> <url> <out_file> [timeout_ms]\n";
        return 1;
    }

    const std::string scenario = argv[1];
    const std::string url = argv[2];
    const std::string outFile = argv[3];
    std::chrono::milliseconds timeout{20000};
    if (argc >= 5) {
        try {
            timeout = std::chrono::milliseconds(std::stoll(argv[4]));
        } catch (...) {
            return 1;
        }
    }

    return runScenario(scenario, url, outFile, timeout);
}
