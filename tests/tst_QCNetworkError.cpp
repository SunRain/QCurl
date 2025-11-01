/**
 * @file tst_QCNetworkError.cpp
 * @brief QCNetworkError 单元测试
 */

#include <QtTest/QtTest>
#include "QCNetworkError.h"

using namespace QCurl;

class TestQCNetworkError : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 枚举测试
    void testNoError();
    void testHttpErrors();
    void testCurlErrors();

    // 转换函数测试
    void testFromCurlCode();
    void testFromHttpCode();

    // 工具函数测试
    void testIsHttpError();
    void testIsCurlError();
    void testErrorString();
};

void TestQCNetworkError::initTestCase()
{
    qDebug() << "初始化 QCNetworkError 测试套件";
}

void TestQCNetworkError::cleanupTestCase()
{
    qDebug() << "清理 QCNetworkError 测试套件";
}

void TestQCNetworkError::testNoError()
{
    NetworkError error = NetworkError::NoError;
    QCOMPARE(static_cast<int>(error), 0);
    QVERIFY(!isHttpError(error));
    QVERIFY(!isCurlError(error));
}

void TestQCNetworkError::testHttpErrors()
{
    // 测试常见 HTTP 错误码
    QCOMPARE(static_cast<int>(NetworkError::HttpBadRequest), 400);
    QCOMPARE(static_cast<int>(NetworkError::HttpUnauthorized), 401);
    QCOMPARE(static_cast<int>(NetworkError::HttpForbidden), 403);
    QCOMPARE(static_cast<int>(NetworkError::HttpNotFound), 404);
    QCOMPARE(static_cast<int>(NetworkError::HttpInternalServerError), 500);

    // 测试 isHttpError()
    QVERIFY(isHttpError(NetworkError::HttpNotFound));
    QVERIFY(isHttpError(NetworkError::HttpInternalServerError));
}

void TestQCNetworkError::testCurlErrors()
{
    // 测试常见 curl 错误码
    NetworkError connectionError = fromCurlCode(CURLE_COULDNT_CONNECT);
    NetworkError timeoutError = fromCurlCode(CURLE_OPERATION_TIMEDOUT);

    QVERIFY(isCurlError(connectionError));
    QVERIFY(isCurlError(timeoutError));
    QVERIFY(!isHttpError(connectionError));
}

void TestQCNetworkError::testFromCurlCode()
{
    // 测试 CURLE_OK
    NetworkError noError = fromCurlCode(CURLE_OK);
    QCOMPARE(noError, NetworkError::NoError);

    // 测试常见 curl 错误
    NetworkError connError = fromCurlCode(CURLE_COULDNT_CONNECT);
    QVERIFY(connError != NetworkError::NoError);
    QVERIFY(isCurlError(connError));
}

void TestQCNetworkError::testFromHttpCode()
{
    // 测试有效 HTTP 错误码
    NetworkError error404 = fromHttpCode(404);
    QCOMPARE(error404, NetworkError::HttpNotFound);

    NetworkError error500 = fromHttpCode(500);
    QCOMPARE(error500, NetworkError::HttpInternalServerError);

    // 测试无效 HTTP 码（成功码应返回 NoError）
    NetworkError error200 = fromHttpCode(200);
    QCOMPARE(error200, NetworkError::NoError);
}

void TestQCNetworkError::testIsHttpError()
{
    QVERIFY(isHttpError(NetworkError::HttpNotFound));
    QVERIFY(isHttpError(NetworkError::HttpForbidden));
    QVERIFY(!isHttpError(NetworkError::NoError));
    QVERIFY(!isHttpError(fromCurlCode(CURLE_COULDNT_CONNECT)));
}

void TestQCNetworkError::testIsCurlError()
{
    NetworkError curlError = fromCurlCode(CURLE_OPERATION_TIMEDOUT);
    QVERIFY(isCurlError(curlError));
    QVERIFY(!isCurlError(NetworkError::NoError));
    QVERIFY(!isCurlError(NetworkError::HttpNotFound));
}

void TestQCNetworkError::testErrorString()
{
    // 测试 NoError
    QString noErrorStr = errorString(NetworkError::NoError);
    QVERIFY(!noErrorStr.isEmpty());

    // 测试 HTTP 错误
    QString error404Str = errorString(NetworkError::HttpNotFound);
    QVERIFY(error404Str.contains("404") || error404Str.contains("Not Found"));

    // 测试 curl 错误
    NetworkError connError = fromCurlCode(CURLE_COULDNT_CONNECT);
    QString connErrorStr = errorString(connError);
    QVERIFY(!connErrorStr.isEmpty());
}

QTEST_MAIN(TestQCNetworkError)
#include "tst_QCNetworkError.moc"
