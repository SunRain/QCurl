#ifndef CURLGLOBALCONSTRUCTOR_H
#define CURLGLOBALCONSTRUCTOR_H

#include <QObject>

#include "SingletonPointer.h"

namespace QCurl {

class CurlGlobalConstructor : public QObject
{
    Q_OBJECT
public:
    ~CurlGlobalConstructor();
    static CurlGlobalConstructor *instance();

private:
    CurlGlobalConstructor(QObject *parent = Q_NULLPTR);
    static CurlGlobalConstructor *createInstance();

};

} // namespace QCurl
#endif // CURLGLOBALCONSTRUCTOR_H
