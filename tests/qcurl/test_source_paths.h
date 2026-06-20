#ifndef QCURL_TEST_SOURCE_PATHS_H
#define QCURL_TEST_SOURCE_PATHS_H

#include <QDir>
#include <QString>
#include <QtCore/QtEnvironmentVariables>

namespace QCurl::TestSourcePaths {

inline QString sourceRoot()
{
    const QString envRoot = qEnvironmentVariable("QCURL_TEST_SOURCE_ROOT");
    if (!envRoot.isEmpty()) {
        return QDir::cleanPath(envRoot);
    }

#ifdef QCURL_TEST_SOURCE_ROOT
    const QString builtInRoot = QStringLiteral(QCURL_TEST_SOURCE_ROOT);
    if (!builtInRoot.isEmpty()) {
        return QDir::cleanPath(builtInRoot);
    }
#endif

    return {};
}

inline QString sourcePath(const QString &relativePath)
{
    const QString root = sourceRoot();
    if (root.isEmpty()) {
        return {};
    }

    return QDir(root).absoluteFilePath(relativePath);
}

} // namespace QCurl::TestSourcePaths

#endif // QCURL_TEST_SOURCE_PATHS_H
