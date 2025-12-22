#include <curl/curl.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string proto = "h2";
    std::string url;
    std::string outFile = "download_0.data";
    std::string eventsOutFile;
    long long pauseOffset = -1;
    long resumeDelayMs = 50;
};

struct Event {
    int seq = 0;
    long long tUs = 0;
    std::string type;
    long long bytesDeliveredTotal = 0;
    long long bytesWrittenTotal = 0;
};

using Clock = std::chrono::steady_clock;

long long elapsedUs(const Clock::time_point &started)
{
    const auto now = Clock::now();
    const auto delta = std::chrono::duration_cast<std::chrono::microseconds>(now - started);
    return delta.count();
}

std::string jsonEscape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[7] = {0};
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    return out;
}

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
        if (arg == "--events-out" && i + 1 < argc) {
            out.eventsOutFile = argv[++i];
            continue;
        }
        if (arg == "--pause-offset" && i + 1 < argc) {
            try {
                out.pauseOffset = std::stoll(argv[++i]);
            } catch (...) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--resume-delay-ms" && i + 1 < argc) {
            try {
                out.resumeDelayMs = std::stol(argv[++i]);
            } catch (...) {
                return std::nullopt;
            }
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
    if (out.eventsOutFile.empty()) {
        return std::nullopt;
    }
    if (out.pauseOffset <= 0) {
        return std::nullopt;
    }
    if (out.resumeDelayMs < 0) {
        return std::nullopt;
    }
    return out;
}

int printUsage()
{
    std::cerr
        << "Usage: qcurl_lc_pause_resume_baseline [-V <http/1.1|h2|h3>] [--out <file>]\n"
        << "  --events-out <file> --pause-offset <bytes> [--resume-delay-ms <ms>] <url>\n";
    return 2;
}

struct PauseResumeContext {
    std::ofstream out;
    long long pauseOffset = 0;
    long resumeDelayMs = 0;
    long long bytesWritten = 0;
    bool firstByteRecorded = false;
    bool pauseRequested = false;
    bool pauseEffective = false;
    bool resumeRequested = false;
    bool resumeEffective = false;
    Clock::time_point started;
    Clock::time_point pauseEffectiveAt;
    int nextSeq = 1;
    std::vector<Event> events;

    void record(const std::string &type)
    {
        Event e;
        e.seq = nextSeq++;
        e.tUs = elapsedUs(started);
        e.type = type;
        e.bytesDeliveredTotal = bytesWritten;
        e.bytesWrittenTotal = bytesWritten;
        events.push_back(e);
    }
};

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *ctx = static_cast<PauseResumeContext *>(userdata);
    if (!ctx) {
        return 0;
    }
    if (!ctx->out.is_open()) {
        return 0;
    }

    const size_t total = size * nmemb;
    if (total == 0) {
        return 0;
    }

    ctx->out.write(ptr, static_cast<std::streamsize>(total));
    if (!ctx->out.good()) {
        return 0;
    }

    ctx->bytesWritten += static_cast<long long>(total);

    if (!ctx->firstByteRecorded) {
        ctx->firstByteRecorded = true;
        ctx->record("first_byte");
    }

    if (!ctx->pauseRequested && ctx->bytesWritten >= ctx->pauseOffset) {
        ctx->pauseRequested = true;
        ctx->record("pause_req");
    }

    return total;
}

bool writeEventsJson(const std::string &path, const Args &args, const std::vector<Event> &events, CURLcode curlcode, long httpCode)
{
    std::ofstream fp(path, std::ios::binary | std::ios::trunc);
    if (!fp.is_open()) {
        return false;
    }

    fp << "{";
    fp << "\"schema\":\"qcurl-lc/pause-resume@v1\",";
    fp << "\"proto\":\"" << jsonEscape(args.proto) << "\",";
    fp << "\"url\":\"" << jsonEscape(args.url) << "\",";
    fp << "\"pause_offset\":" << args.pauseOffset << ",";
    fp << "\"resume_delay_ms\":" << args.resumeDelayMs << ",";
    fp << "\"result\":{"
       << "\"curlcode\":" << static_cast<int>(curlcode) << ","
       << "\"http_code\":" << httpCode
       << "},";

    fp << "\"events\":[";
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto &e = events[i];
        if (i > 0) {
            fp << ",";
        }
        fp << "{"
           << "\"seq\":" << e.seq << ","
           << "\"t_us\":" << e.tUs << ","
           << "\"type\":\"" << jsonEscape(e.type) << "\","
           << "\"bytes_delivered_total\":" << e.bytesDeliveredTotal << ","
           << "\"bytes_written_total\":" << e.bytesWrittenTotal
           << "}";
    }
    fp << "]}\n";
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

    const CURLcode g = curl_global_init(CURL_GLOBAL_ALL);
    if (g != CURLE_OK) {
        std::cerr << "curl_global_init failed: " << curl_easy_strerror(g) << "\n";
        return 3;
    }

    CURLM *multi = curl_multi_init();
    if (!multi) {
        curl_global_cleanup();
        std::cerr << "curl_multi_init failed\n";
        return 5;
    }

    CURL *easy = curl_easy_init();
    if (!easy) {
        curl_multi_cleanup(multi);
        curl_global_cleanup();
        std::cerr << "curl_easy_init failed\n";
        return 5;
    }

    PauseResumeContext ctx;
    ctx.pauseOffset = args.pauseOffset;
    ctx.resumeDelayMs = args.resumeDelayMs;
    ctx.started = Clock::now();
    ctx.record("start");

    ctx.out.open(args.outFile, std::ios::binary | std::ios::trunc);
    if (!ctx.out.is_open()) {
        curl_easy_cleanup(easy);
        curl_multi_cleanup(multi);
        curl_global_cleanup();
        std::cerr << "failed to open output file: " << args.outFile << "\n";
        return 4;
    }

    curl_easy_setopt(easy, CURLOPT_URL, args.url.c_str());
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &writeCallback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &ctx);
    if (!setHttpVersion(easy, args.proto)) {
        ctx.out.close();
        curl_easy_cleanup(easy);
        curl_multi_cleanup(multi);
        curl_global_cleanup();
        std::cerr << "unsupported proto: " << args.proto << "\n";
        return 6;
    }

    CURLMcode mrc = curl_multi_add_handle(multi, easy);
    if (mrc != CURLM_OK) {
        ctx.out.close();
        curl_easy_cleanup(easy);
        curl_multi_cleanup(multi);
        curl_global_cleanup();
        std::cerr << "curl_multi_add_handle failed: " << curl_multi_strerror(mrc) << "\n";
        return 6;
    }

    int running = 0;
    CURLcode rc = CURLE_OK;
    long httpCode = 0;
    bool done = false;

    curl_multi_perform(multi, &running);
    while (!done) {
        int numfds = 0;
        curl_multi_wait(multi, nullptr, 0, 50, &numfds);
        curl_multi_perform(multi, &running);

        if (ctx.pauseRequested && !ctx.pauseEffective) {
            const CURLcode prc = curl_easy_pause(easy, CURLPAUSE_RECV);
            if (prc != CURLE_OK) {
                rc = prc;
                break;
            }
            ctx.pauseEffective = true;
            ctx.pauseEffectiveAt = Clock::now();
            ctx.record("pause_effective");
        }

        if (ctx.pauseEffective && !ctx.resumeRequested) {
            const auto now = Clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - ctx.pauseEffectiveAt);
            if (elapsed.count() >= ctx.resumeDelayMs) {
                ctx.resumeRequested = true;
                ctx.record("resume_req");
                const CURLcode rrc = curl_easy_pause(easy, CURLPAUSE_CONT);
                if (rrc != CURLE_OK) {
                    rc = rrc;
                    break;
                }
                ctx.resumeEffective = true;
                ctx.record("resume_effective");
            }
        }

        int msgs = 0;
        while (CURLMsg *msg = curl_multi_info_read(multi, &msgs)) {
            if (msg->msg != CURLMSG_DONE) {
                continue;
            }
            rc = msg->data.result;
            done = true;
            break;
        }

        if (running == 0 && !done) {
            done = true;
        }
    }

    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &httpCode);

    if (rc == CURLE_OK) {
        ctx.record("finished");
    } else {
        ctx.record("failed");
    }

    ctx.out.close();
    curl_multi_remove_handle(multi, easy);
    curl_easy_cleanup(easy);
    curl_multi_cleanup(multi);
    curl_global_cleanup();

    if (!writeEventsJson(args.eventsOutFile, args, ctx.events, rc, httpCode)) {
        std::cerr << "failed to write events: " << args.eventsOutFile << "\n";
        return 7;
    }

    if (rc != CURLE_OK) {
        std::cerr << "curl failed: " << curl_easy_strerror(rc) << "\n";
        return 7;
    }
    return 0;
}
