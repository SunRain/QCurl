/**
 * @file
 * @brief Internal retry-policy test controls.
 */

#ifndef QCNETWORKRETRYPOLICY_P_H
#define QCNETWORKRETRYPOLICY_P_H

#include <QByteArray>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <optional>

namespace QCurl::Internal {

inline constexpr auto kRetryJitterFractionTestEnv = "QCURL_TEST_RETRY_JITTER_FRACTION";

/**
 * @brief Sets a deterministic equal-jitter sample for unit tests.
 *
 * A missing value restores the production random source.
 */
inline void setRetryJitterFractionForTest(std::optional<double> fraction)
{
    if (!fraction.has_value() || !std::isfinite(fraction.value())) {
        qunsetenv(kRetryJitterFractionTestEnv);
        return;
    }

    const double bounded = std::clamp(fraction.value(), 0.0, 1.0);
    qputenv(kRetryJitterFractionTestEnv, QByteArray::number(bounded, 'g', 17));
}

} // namespace QCurl::Internal

#endif // QCNETWORKRETRYPOLICY_P_H
