#ifndef CURLGLOBALCONSTRUCTOR_H
#define CURLGLOBALCONSTRUCTOR_H

#include <QObject>

namespace QCurl {

class CurlGlobalConstructor : public QObject
{
    Q_OBJECT
public:
    ~CurlGlobalConstructor() override;
    static CurlGlobalConstructor *instance();

private:
    explicit CurlGlobalConstructor(QObject *parent = nullptr);

};

} // namespace QCurl
#endif // CURLGLOBALCONSTRUCTOR_H
