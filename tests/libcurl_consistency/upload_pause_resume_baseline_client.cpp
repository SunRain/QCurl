#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string proto = "http/1.1";
    std::string url;
    std::string outFile = "download_0.data";
    std::string eventsOutFile;
    long long payloadSize = 0;
    long delayMs = 200;
};

using Clock = std::chrono::steady_clock;

long long elapsedMs(const Clock::time_point &started)
{
    const auto now = Clock::now();
    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - started);
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
        return curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0) == CURLE_OK;
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
        if (arg == "--payload-size" && i + 1 < argc) {
            try {
                out.payloadSize = std::stoll(argv[++i]);
            } catch (...) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--delay-ms" && i + 1 < argc) {
            try {
                out.delayMs = std::stol(argv[++i]);
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
    if (out.payloadSize <= 0) {
        return std::nullopt;
    }
    if (out.delayMs < 0) {
        return std::nullopt;
    }
    return out;
}

int printUsage()
{
    std::cerr
        << "Usage: qcurl_lc_upload_pause_resume_baseline [-V <http/1.1|h2>] [--out <file>] "
        << "--events-out <file> --payload-size <bytes> [--delay-ms <ms>] <url>\n";
    return 2;
}

struct UploadContext {
    std::vector<unsigned char> buffer;
    std::vector<unsigned char> payload;
    std::size_t payloadOffset = 0;
    bool finished = false;

    bool paused = false;
    bool pauseSeen = false;
    bool resumeSeen = false;
    long long zeroReadCount = 0;
    Clock::time_point started;
    bool chunk2Injected = false;
    bool chunk3Injected = false;

    void injectChunk(std::size_t size)
    {
        if (payloadOffset >= payload.size()) {
            return;
        }
        const std::size_t n = std::min(size, payload.size() - payloadOffset);
        buffer.insert(buffer.end(), payload.begin() + static_cast<long long>(payloadOffset),
                      payload.begin() + static_cast<long long>(payloadOffset + n));
        payloadOffset += n;
        if (payloadOffset >= payload.size()) {
            finished = true;
        }
    }
};

size_t readCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *ctx = static_cast<UploadContext *>(userdata);
    if (!ctx || !ptr) {
        return 0;
    }
    const size_t max = size * nmemb;
    if (max == 0) {
        return 0;
    }

    if (ctx->buffer.empty()) {
        if (!ctx->finished) {
            ++ctx->zeroReadCount;
            ctx->paused = true;
            if (!ctx->pauseSeen) {
                ctx->pauseSeen = true;
            }
            return CURL_READFUNC_PAUSE;
        }
        return 0;
    }

    const std::size_t n = std::min(max, ctx->buffer.size());
    std::memcpy(ptr, ctx->buffer.data(), n);
    ctx->buffer.erase(ctx->buffer.begin(), ctx->buffer.begin() + static_cast<long long>(n));
    return n;
}

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *out = static_cast<std::ofstream *>(userdata);
    if (!out || !out->is_open() || !ptr) {
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

bool writeEventsJson(const std::string &path,
                     const Args &args,
                     const UploadContext &ctx,
                     CURLcode curlcode,
                     long httpCode)
{
    std::ofstream fp(path, std::ios::binary | std::ios::trunc);
    if (!fp.is_open()) {
        return false;
    }

    fp << "{";
    fp << "\"schema\":\"qcurl-lc/upload-pause-resume@v1\",";
    fp << "\"proto\":\"" << jsonEscape(args.proto) << "\",";
    fp << "\"url\":\"" << jsonEscape(args.url) << "\",";
    fp << "\"payload_size\":" << args.payloadSize << ",";
    fp << "\"zero_read_count\":" << ctx.zeroReadCount << ",";
    fp << "\"event_seq\":[";
    bool first = true;
    if (ctx.pauseSeen) {
        fp << "\"pause\"";
        first = false;
    }
    if (ctx.resumeSeen) {
        if (!first) {
            fp << ",";
        }
        fp << "\"resume\"";
    }
    fp << "],";
    fp << "\"result\":{"
       << "\"curlcode\":" << static_cast<int>(curlcode) << ","
       << "\"http_code\":" << httpCode
       << "}";
    fp << "}\n";
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

    UploadContext ctx;
    ctx.payload.assign(static_cast<std::size_t>(args.payloadSize), static_cast<unsigned char>('u'));
    ctx.started = Clock::now();

    // 三段注入：chunk1 立即可读，chunk2/chunk3 延迟注入以制造 read=0 非 EOF 的空窗。
    ctx.injectChunk(4096);

    std::ofstream out(args.outFile, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        curl_easy_cleanup(easy);
        curl_multi_cleanup(multi);
        curl_global_cleanup();
        std::cerr << "failed to open output file: " << args.outFile << "\n";
        return 4;
    }

    curl_easy_setopt(easy, CURLOPT_URL, args.url.c_str());
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(easy, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(easy, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(args.payloadSize));
    curl_easy_setopt(easy, CURLOPT_READFUNCTION, &readCallback);
    curl_easy_setopt(easy, CURLOPT_READDATA, &ctx);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &writeCallback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &out);
    if (!setHttpVersion(easy, args.proto)) {
        out.close();
        curl_easy_cleanup(easy);
        curl_multi_cleanup(multi);
        curl_global_cleanup();
        std::cerr << "unsupported proto: " << args.proto << "\n";
        return 6;
    }

    CURLMcode mrc = curl_multi_add_handle(multi, easy);
    if (mrc != CURLM_OK) {
        out.close();
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

        const long long ms = elapsedMs(ctx.started);
        if (!ctx.chunk2Injected && ms >= args.delayMs) {
            ctx.chunk2Injected = true;
            ctx.injectChunk(4096);
        }
        if (!ctx.chunk3Injected && ms >= args.delayMs * 2) {
            ctx.chunk3Injected = true;
            ctx.injectChunk(static_cast<std::size_t>(args.payloadSize));
        }

        if (ctx.paused && !ctx.buffer.empty()) {
            const CURLcode prc = curl_easy_pause(easy, CURLPAUSE_CONT);
            if (prc != CURLE_OK) {
                rc = prc;
                break;
            }
            ctx.paused = false;
            if (ctx.pauseSeen) {
                ctx.resumeSeen = true;
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

    out.close();
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

    if (ctx.zeroReadCount <= 0 || !ctx.pauseSeen || !ctx.resumeSeen) {
        std::cerr << "upload pause/resume contract not observed (zero_read_count=" << ctx.zeroReadCount << ")\n";
        return 8;
    }

    return 0;
}
