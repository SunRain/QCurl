#ifndef QCURL_TESTS_LIBCURL_CONSISTENCY_CLI_PARSE_H
#define QCURL_TESTS_LIBCURL_CONSISTENCY_CLI_PARSE_H

#include <charconv>
#include <optional>
#include <string_view>
#include <system_error>

namespace qcurl::lc {

template<typename IntT>
std::optional<IntT> parseInt(std::string_view s) noexcept
{
    if (s.empty()) {
        return std::nullopt;
    }

    IntT value{};
    const char *begin = s.data();
    const char *end   = begin + s.size();
    const auto res    = std::from_chars(begin, end, value);
    if (res.ec != std::errc() || res.ptr != end) {
        return std::nullopt;
    }
    return value;
}

} // namespace qcurl::lc

#endif // QCURL_TESTS_LIBCURL_CONSISTENCY_CLI_PARSE_H
