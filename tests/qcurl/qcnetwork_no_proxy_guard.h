#ifndef QCNETWORK_NO_PROXY_GUARD_H
#define QCNETWORK_NO_PROXY_GUARD_H

#include <QByteArray>
#include <QtGlobal>

/// 显式代理夹具临时关闭环境绕过规则，作用域结束后恢复未设置与空值的区别。
class NoProxyGuard final
{
public:
    NoProxyGuard()
        : m_lower(qgetenv("no_proxy"))
        , m_upper(qgetenv("NO_PROXY"))
        , m_hadLower(qEnvironmentVariableIsSet("no_proxy"))
        , m_hadUpper(qEnvironmentVariableIsSet("NO_PROXY"))
    {
        qunsetenv("no_proxy");
        qunsetenv("NO_PROXY");
    }

    ~NoProxyGuard()
    {
        restore("no_proxy", m_lower, m_hadLower);
        restore("NO_PROXY", m_upper, m_hadUpper);
    }

private:
    Q_DISABLE_COPY_MOVE(NoProxyGuard)

    static void restore(const char *name, const QByteArray &value, bool wasSet)
    {
        if (wasSet) {
            qputenv(name, value);
        } else {
            qunsetenv(name);
        }
    }

    QByteArray m_lower;
    QByteArray m_upper;
    bool m_hadLower = false;
    bool m_hadUpper = false;
};

#endif // QCNETWORK_NO_PROXY_GUARD_H
