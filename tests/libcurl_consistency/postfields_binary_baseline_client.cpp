#include <curl/curl.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Args {
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

    ctx->file->write(ptr, static_cast<std::streamsize>(total));
    if (!ctx->file->good()) {
        return 0;
    }
    ctx->written += total;
    return static_cast<size_t>(total);
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
        if (!arg.empty() && arg[0] == '-') {
            return std::nullopt;
        }
        out.url = arg;
    }
    if (out.proto.empty() || out.url.empty()) {
        return std::nullopt;
    }
    return out;
}

int printUsage()
{
    std::cerr << "Usage: qcurl_lc_postfields_binary_baseline -V <http/1.1|h2|h3> <url>\n";
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

    const std::vector<std::uint8_t> body = {'.', 'a', 'b', 'c', 0x00, 'x', 'y', 'z'};

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

    CURL *curl = curl_easy_init();
    if (!curl) {
        out.close();
        curl_global_cleanup();
        std::cerr << "curl_easy_init failed\n";
        return 5;
    }

    WriteContext ctx;
    ctx.file = &out;
    ctx.written = 0;

    curl_easy_setopt(curl, CURLOPT_URL, args.url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    if (!setHttpVersion(curl, args.proto)) {
        curl_easy_cleanup(curl);
        out.close();
        curl_global_cleanup();
        std::cerr << "unknown proto: " << args.proto << "\n";
        return 6;
    }

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    out.close();

    if (rc != CURLE_OK) {
        curl_global_cleanup();
        std::cerr << "curl_easy_perform failed: " << curl_easy_strerror(rc) << "\n";
        return 7;
    }

    if (ctx.written != body.size()) {
        curl_global_cleanup();
        std::cerr << "response size mismatch, got=" << ctx.written << " expected=" << body.size() << "\n";
        return 8;
    }

    std::ifstream in(outFile, std::ios::binary);
    if (!in.is_open()) {
        curl_global_cleanup();
        std::cerr << "failed to read output file\n";
        return 9;
    }
    std::vector<std::uint8_t> got(body.size(), 0);
    in.read(reinterpret_cast<char *>(got.data()), static_cast<std::streamsize>(got.size()));
    if (!in.good()) {
        curl_global_cleanup();
        std::cerr << "failed to read output file content\n";
        return 10;
    }
    if (got != body) {
        curl_global_cleanup();
        std::cerr << "response content mismatch\n";
        return 11;
    }

    curl_global_cleanup();
    return 0;
}

