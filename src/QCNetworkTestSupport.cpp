/**
 * @file
 * @brief 实现显式 Test Support 的 manager 绑定入口。
 */

#include "QCNetworkTestSupport.h"

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"

#include <QVariant>

namespace QCurl::TestSupport {
namespace {

constexpr auto kMockHandlerProperty = "_qcurl_test_support_mock_handler";

} // namespace

void setMockHandler(QCNetworkAccessManager *manager, QCNetworkMockHandler *handler)
{
    if (!manager) {
        return;
    }

    if (!handler) {
        manager->setProperty(kMockHandlerProperty, QVariant());
        return;
    }

    manager->setProperty(
        kMockHandlerProperty,
        QVariant::fromValue(static_cast<qulonglong>(reinterpret_cast<quintptr>(handler))));
}

QCNetworkMockHandler *mockHandler(const QCNetworkAccessManager *manager)
{
    if (!manager) {
        return nullptr;
    }

    const QVariant value = manager->property(kMockHandlerProperty);
    if (!value.isValid()) {
        return nullptr;
    }

    return reinterpret_cast<QCNetworkMockHandler *>(static_cast<quintptr>(value.toULongLong()));
}

} // namespace QCurl::TestSupport
