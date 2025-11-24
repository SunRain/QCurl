
QMAKE_CXXFLAGS = -fpermissive

LIBS += -lcurl

INCLUDEPATH += $$PWD/

HEADERS += \
    $$PWD/QCNetworkAccessManager.h \
    $$PWD/QCUtility.h \
    $$PWD/private/SingletonPointer.h \
    $$PWD/private/SingletonPointer_p.h \
    $$PWD/CurlGlobalConstructor.h \
    $$PWD/QCNetworkRequest.h \
    $$PWD/qbytedata_p.h \
    $$PWD/QCNetworkAccessManager_p.h \
    $$PWD/CurlMultiHandleProcesser.h \
    $$PWD/CurlEasyHandleInitializtionClass.h \
    $$PWD/QCNetworkAsyncReply.h \
    $$PWD/QCNetworkAsyncHttpGetReply.h \
    $$PWD/QCNetworkAsyncHttpGetReply_p.h \
    $$PWD/QCNetworkAsyncHttpHeadReply.h \
    $$PWD/QCNetworkAsyncHttpHeadReply_p.h \
    $$PWD/QCNetworkAsyncReply_p.h \
    $$PWD/QCNetworkSyncReply.h \
    $$PWD/QCNetworkAsyncDataPostReply.h

SOURCES += \
    $$PWD/QCNetworkAccessManager.cpp \
    $$PWD/CurlGlobalConstructor.cpp \
    $$PWD/QCNetworkRequest.cpp \
    $$PWD/CurlMultiHandleProcesser.cpp \
    $$PWD/CurlEasyHandleInitializtionClass.cpp \
    $$PWD/QCNetworkAsyncHttpGetReply.cpp \
    $$PWD/QCNetworkAsyncHttpHeadReply.cpp \
    $$PWD/QCNetworkSyncReply.cpp \
    $$PWD/QCNetworkAsyncReply.cpp \
    $$PWD/QCNetworkAsyncDataPostReply.cpp \
    $$PWD/QCUtility.cpp


