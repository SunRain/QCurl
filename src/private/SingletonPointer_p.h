/**
 * @file
 * @brief 声明单例指针内部实现。
 */

#ifndef SINGLETONPOINTER_P_H
#define SINGLETONPOINTER_P_H

#include <QAtomicInt>
#include <QThread>
#include <QThreadStorage>
#include <QtGlobal>

/**
 * @brief 内部：线程安全单例工具
 *
 * 采用 qCallOnce + 原子状态实现一次性初始化，仅供内部单例辅助使用。
 */
namespace CallOnce {
enum ECallOnce { CO_Request, CO_InProgress, CO_Finished };

Q_GLOBAL_STATIC(QThreadStorage<QBasicAtomicInt>, once_flag)
} // namespace CallOnce

template<class Function>
inline static void qCallOnce(Function func, QBasicAtomicInt &flag)
{
    int protectFlag = flag.fetchAndStoreAcquire(flag.loadRelaxed());

    if (protectFlag == CallOnce::CO_Finished) {
        return;
    }

    if (protectFlag == CallOnce::CO_Request
        && flag.testAndSetRelaxed(protectFlag, CallOnce::CO_InProgress)) {
        func();
        flag.fetchAndStoreRelease(CallOnce::CO_Finished);
    } else {
        do {
            QThread::yieldCurrentThread();
        } while (!flag.testAndSetAcquire(CallOnce::CO_Finished, CallOnce::CO_Finished));
    }
}

template<class Function>
inline static void qCallOncePerThread(Function func)
{
    qCallOnce(func, CallOnce::once_flag()->localData());
}

template<class T>
class Singleton
{
private:
    typedef T *(*CreateInstanceFunction)();

public:
    /**
     * @brief 获取单例指针
     * @param create 创建函数（仅执行一次）
     * @return 单例指针
     */
    static T *instance(CreateInstanceFunction create);

private:
    static void init();

    Singleton();
    ~Singleton();
    Q_DISABLE_COPY(Singleton)
    static QBasicAtomicPointer<void> create;
    static QBasicAtomicInt flag;
    static QBasicAtomicPointer<void> tptr;
    bool inited;
};

template<class T>
T *Singleton<T>::instance(CreateInstanceFunction create)
{
    Singleton::create.storeRelaxed(reinterpret_cast<void *>(create));
    qCallOnce(init, flag);
    return static_cast<T *>(tptr.loadRelaxed());
}

template<class T>
void Singleton<T>::init()
{
    static Singleton singleton;
    if (singleton.inited) {
        CreateInstanceFunction createFunction = reinterpret_cast<CreateInstanceFunction>(
            Singleton::create.loadRelaxed());
        tptr.storeRelaxed(createFunction());
    }
}

template<class T>
Singleton<T>::Singleton()
{
    inited = true;
};

template<class T>
Singleton<T>::~Singleton()
{
    T *createdTptr = static_cast<T *>(tptr.fetchAndStoreOrdered(nullptr));
    if (createdTptr) {
        delete createdTptr;
    }
    create.storeRelaxed(nullptr);
}

template<class T>
QBasicAtomicPointer<void> Singleton<T>::create = Q_BASIC_ATOMIC_INITIALIZER(nullptr);
template<class T>
QBasicAtomicInt Singleton<T>::flag = Q_BASIC_ATOMIC_INITIALIZER(CallOnce::CO_Request);
template<class T>
QBasicAtomicPointer<void> Singleton<T>::tptr = Q_BASIC_ATOMIC_INITIALIZER(nullptr);

#endif // SINGLETONPOINTER_P_H
