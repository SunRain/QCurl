#include <curl/curl.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

namespace {

struct Args {
    std::uint64_t abortOffset = 0;
    std::uint64_t fileSize = 0;
    std::string proto;
    std::string url;
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

struct WriteContext {
    std::ofstream *file = nullptr;
    std::uint64_t abortAt = 0;
    std::uint64_t written = 0;
};

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *ctx = static_cast<WriteContext *>(userdata);
    if (!ctx || !ctx->file || !ctx->file->is_open()) {
        return 0;
    }

    const std::uint64_t total = static_cast<std::uint64_t>(size) * static_cast<std::uint64_t>(nmemb);
    if (total == 0) {
        return 0;
    }

    if (ctx->abortAt > 0 && ctx->written >= ctx->abortAt) {
        return 0;
    }

    std::uint64_t toWrite = total;
    if (ctx->abortAt > 0 && (ctx->written + total) > ctx->abortAt) {
        toWrite = ctx->abortAt - ctx->written;
    }

    if (toWrite > 0) {
        ctx->file->write(ptr, static_cast<std::streamsize>(toWrite));
        if (!ctx->file->good()) {
            return 0;
        }
        ctx->written += toWrite;
    }

    if (ctx->abortAt > 0 && ctx->written >= ctx->abortAt) {
        return 0;
    }

    return static_cast<size_t>(total);
}

bool performDownload(const Args &args, std::ofstream &file, WriteContext &ctx, const std::optional<std::string> &rangeHeader)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, args.url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    if (!setHttpVersion(curl, args.proto)) {
        curl_easy_cleanup(curl);
        return false;
    }
    if (rangeHeader.has_value()) {
        curl_easy_setopt(curl, CURLOPT_RANGE, rangeHeader->c_str());
    }

    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (!file.good()) {
        return false;
    }

    if (!rangeHeader.has_value()) {
        return (rc == CURLE_WRITE_ERROR) || (rc == CURLE_ABORTED_BY_CALLBACK);
    }
    return rc == CURLE_OK;
}

std::optional<Args> parseArgs(int argc, char **argv)
{
    Args out;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-A" && i + 1 < argc) {
            out.abortOffset = std::stoull(argv[++i]);
            continue;
        }
        if (arg == "-S" && i + 1 < argc) {
            out.fileSize = std::stoull(argv[++i]);
            continue;
        }
        if (arg == "-V" && i + 1 < argc) {
            out.proto = argv[++i];
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            return std::nullopt;
        }
        out.url = arg;
    }

    if (out.abortOffset == 0 || out.fileSize == 0 || out.proto.empty() || out.url.empty()) {
        return std::nullopt;
    }
    if (out.fileSize <= out.abortOffset) {
        return std::nullopt;
    }
    return out;
}

int printUsage()
{
    std::cerr << "Usage: qcurl_lc_range_resume_baseline -A <abort_offset> -S <file_size> -V <http/1.1|h2|h3> <url>\n";
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

    const std::string outFile = "download_0.data";
    std::ofstream out(outFile, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        curl_global_cleanup();
        std::cerr << "failed to open output file: " << outFile << "\n";
        return 4;
    }

    WriteContext ctx;
    ctx.file = &out;
    ctx.abortAt = args.abortOffset;
    ctx.written = 0;
    if (!performDownload(args, out, ctx, std::nullopt)) {
        out.close();
        curl_global_cleanup();
        std::cerr << "phase1 download failed\n";
        return 5;
    }
    out.close();

    if (ctx.written != args.abortOffset) {
        curl_global_cleanup();
        std::cerr << "phase1 bytes mismatch, got=" << ctx.written << " expected=" << args.abortOffset << "\n";
        return 6;
    }

    std::ofstream append(outFile, std::ios::binary | std::ios::app);
    if (!append.is_open()) {
        curl_global_cleanup();
        std::cerr << "failed to open output file for append: " << outFile << "\n";
        return 7;
    }

    WriteContext ctx2;
    ctx2.file = &append;
    ctx2.abortAt = 0;
    ctx2.written = args.abortOffset;
    const std::string range = std::to_string(args.abortOffset) + "-" + std::to_string(args.fileSize - 1);
    if (!performDownload(args, append, ctx2, range)) {
        append.close();
        curl_global_cleanup();
        std::cerr << "phase2 range download failed\n";
        return 8;
    }
    append.close();

    std::ifstream in(outFile, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        curl_global_cleanup();
        std::cerr << "failed to stat output file\n";
        return 9;
    }
    const std::uint64_t gotSize = static_cast<std::uint64_t>(in.tellg());
    if (gotSize != args.fileSize) {
        curl_global_cleanup();
        std::cerr << "final size mismatch, got=" << gotSize << " expected=" << args.fileSize << "\n";
        return 10;
    }

    curl_global_cleanup();
    return 0;
}

