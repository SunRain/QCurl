#include "cli_parse.h"

#include <algorithm>
#include <chrono>
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
    std::string proto = "h2";
    std::string url;
    std::string outFile = "download_0.data";
    std::string eventsOutFile;
    long long limitBytes  = 0;
    long long resumeBytes = 0;
};

struct Event
{
    int seq       = 0;
    long long tUs = 0;
    std::string type;
    long long bufferedBytes = 0;
    long long writtenTotal  = 0;
};

using Clock = std::chrono::steady_clock;

long long elapsedUs(const Clock::time_point &started)
{
    const auto now   = Clock::now();
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
        if (arg == "--limit-bytes" && i + 1 < argc) {
            const auto limit = qcurl::lc::parseInt<long long>(argv[++i]);
            if (!limit.has_value()) {
                return std::nullopt;
            }
            out.limitBytes = *limit;
            continue;
        }
        if (arg == "--resume-bytes" && i + 1 < argc) {
            const auto resume = qcurl::lc::parseInt<long long>(argv[++i]);
            if (!resume.has_value()) {
                return std::nullopt;
            }
            out.resumeBytes = *resume;
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
    if (out.limitBytes <= 0) {
        return std::nullopt;
    }
    if (out.resumeBytes <= 0 || out.resumeBytes >= out.limitBytes) {
        return std::nullopt;
    }
    return out;
}

int printUsage()
{
    std::cerr << "Usage: qcurl_lc_backpressure_baseline [-V <http/1.1|h2|h3>] [--out <file>] "
              << "--events-out <file> --limit-bytes <bytes> --resume-bytes <bytes> <url>\n";
    return 2;
}

struct BackpressureContext
{
    std::ofstream out;
    std::vector<unsigned char> buffer;
    long long limitBytes  = 0;
    long long resumeBytes = 0;

    bool paused        = false;
    bool bpOnRecorded  = false;
    bool bpOffRecorded = false;
    bool directWrite   = false;

    long long peakBufferedBytes = 0;
    long long bytesWrittenTotal = 0;

    Clock::time_point started;
    int nextSeq = 1;
    std::vector<Event> events;
    std::vector<std::string> eventSeq;

    void record(const std::string &type)
    {
        Event e;
        e.seq           = nextSeq++;
        e.tUs           = elapsedUs(started);
        e.type          = type;
        e.bufferedBytes = static_cast<long long>(buffer.size());
        e.writtenTotal  = bytesWrittenTotal;
        events.push_back(e);

        if (type == "bp_on") {
            eventSeq.push_back("bp_on");
        } else if (type == "bp_off") {
            eventSeq.push_back("bp_off");
        }
    }
};

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *ctx = static_cast<BackpressureContext *>(userdata);
    if (!ctx || !ptr) {
        return 0;
    }
    if (!ctx->out.is_open()) {
        return 0;
    }

    const size_t total = size * nmemb;
    if (total == 0) {
        return 0;
    }

    if (ctx->directWrite) {
        ctx->out.write(ptr, static_cast<std::streamsize>(total));
        if (!ctx->out.good()) {
            return 0;
        }
        ctx->bytesWrittenTotal += static_cast<long long>(total);
        return total;
    }

    const long long buffered = static_cast<long long>(ctx->buffer.size());
    if (buffered + static_cast<long long>(total) > ctx->limitBytes) {
        ctx->paused = true;
        if (!ctx->bpOnRecorded) {
            ctx->bpOnRecorded = true;
            ctx->record("bp_on");
        }
        return CURL_WRITEFUNC_PAUSE;
    }

    ctx->buffer.insert(ctx->buffer.end(),
                       reinterpret_cast<const unsigned char *>(ptr),
                       reinterpret_cast<const unsigned char *>(ptr) + total);
    ctx->peakBufferedBytes = std::max(ctx->peakBufferedBytes,
                                      static_cast<long long>(ctx->buffer.size()));
    return total;
}

bool drainBufferToFile(BackpressureContext &ctx)
{
    if (!ctx.out.is_open()) {
        return false;
    }
    if (ctx.buffer.empty()) {
        return true;
    }
    ctx.out.write(reinterpret_cast<const char *>(ctx.buffer.data()),
                  static_cast<std::streamsize>(ctx.buffer.size()));
    if (!ctx.out.good()) {
        return false;
    }
    ctx.bytesWrittenTotal += static_cast<long long>(ctx.buffer.size());
    ctx.buffer.clear();
    ctx.buffer.shrink_to_fit();
    return true;
}

bool writeEventsJson(const std::string &path,
                     const Args &args,
                     const BackpressureContext &ctx,
                     CURLcode curlcode,
                     long httpCode)
{
    std::ofstream fp(path, std::ios::binary | std::ios::trunc);
    if (!fp.is_open()) {
        return false;
    }

    fp << "{";
    fp << "\"schema\":\"qcurl-lc/backpressure@v1\",";
    fp << "\"proto\":\"" << jsonEscape(args.proto) << "\",";
    fp << "\"url\":\"" << jsonEscape(args.url) << "\",";
    fp << "\"limit_bytes\":" << args.limitBytes << ",";
    fp << "\"resume_bytes\":" << args.resumeBytes << ",";
    fp << "\"curl_max_write_size\":" << static_cast<long long>(CURL_MAX_WRITE_SIZE) << ",";
    fp << "\"peak_buffered_bytes\":" << ctx.peakBufferedBytes << ",";
    fp << "\"result\":{"
       << "\"curlcode\":" << static_cast<int>(curlcode) << ","
       << "\"http_code\":" << httpCode << "},";

    fp << "\"event_seq\":[";
    for (std::size_t i = 0; i < ctx.eventSeq.size(); ++i) {
        if (i > 0) {
            fp << ",";
        }
        fp << "\"" << jsonEscape(ctx.eventSeq[i]) << "\"";
    }
    fp << "],";

    fp << "\"events\":[";
    for (std::size_t i = 0; i < ctx.events.size(); ++i) {
        const auto &e = ctx.events[i];
        if (i > 0) {
            fp << ",";
        }
        fp << "{"
           << "\"seq\":" << e.seq << ","
           << "\"t_us\":" << e.tUs << ","
           << "\"type\":\"" << jsonEscape(e.type) << "\","
           << "\"buffered_bytes\":" << e.bufferedBytes << ","
           << "\"written_total\":" << e.writtenTotal << "}";
    }
    fp << "]}\n";
    fp.flush();
    return fp.good();
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
        return 5;
    }

    CURL *easy = curl_easy_init();
    if (!easy) {
        curl_multi_cleanup(multi);
        curl_global_cleanup();
        std::cerr << "curl_easy_init failed\n";
        return 5;
    }

    BackpressureContext ctx;
    ctx.limitBytes  = args.limitBytes;
    ctx.resumeBytes = args.resumeBytes;
    ctx.started     = Clock::now();
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

    int running         = 0;
    CURLcode rc         = CURLE_OK;
    long httpCode       = 0;
    bool done           = false;
    const auto deadline = ctx.started + std::chrono::seconds(60);

    curl_multi_perform(multi, &running);
    while (!done) {
        if (Clock::now() >= deadline) {
            rc = CURLE_OPERATION_TIMEDOUT;
            break;
        }

        int numfds = 0;
        curl_multi_wait(multi, nullptr, 0, 50, &numfds);
        curl_multi_perform(multi, &running);

        if (ctx.paused && !ctx.bpOffRecorded) {
            if (!drainBufferToFile(ctx)) {
                rc = CURLE_WRITE_ERROR;
                break;
            }
            if (static_cast<long long>(ctx.buffer.size()) <= ctx.resumeBytes) {
                // 注意：curl_easy_pause(CURLPAUSE_CONT) 可能触发回调重入。
                // 为避免“恢复期间收到的数据落在 buffer 中但后续不再 drain”导致下载文件缺块，
                // 这里在恢复前切换到 directWrite，使重入回调直接写盘。
                ctx.directWrite    = true;
                const CURLcode rrc = curl_easy_pause(easy, CURLPAUSE_CONT);
                if (rrc != CURLE_OK) {
                    rc = rrc;
                    break;
                }
                ctx.bpOffRecorded = true;
                ctx.paused        = false;
                ctx.record("bp_off");
            }
        }

        int msgs = 0;
        while (CURLMsg *msg = curl_multi_info_read(multi, &msgs)) {
            if (msg->msg != CURLMSG_DONE) {
                continue;
            }
            rc   = msg->data.result;
            done = true;
            break;
        }
    }

    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &httpCode);

    if (!drainBufferToFile(ctx)) {
        rc = CURLE_WRITE_ERROR;
    }

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

    if (!writeEventsJson(args.eventsOutFile, args, ctx, rc, httpCode)) {
        std::cerr << "failed to write events: " << args.eventsOutFile << "\n";
        return 7;
    }

    if (rc != CURLE_OK) {
        std::cerr << "curl failed: " << curl_easy_strerror(rc) << "\n";
        return 7;
    }

    if (!ctx.bpOnRecorded || !ctx.bpOffRecorded) {
        std::cerr << "backpressure contract not observed: bp_on/bp_off missing\n";
        return 8;
    }

    return 0;
}
