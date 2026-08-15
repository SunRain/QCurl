#ifndef QCNETWORKRESUMABLEDOWNLOADWRITER_P_H
#define QCNETWORKRESUMABLEDOWNLOADWRITER_P_H

#include "QCGlobal.h"

#include <QFile>
#include <QSaveFile>
#include <QString>

#include <optional>

class QIODevice;
class QThread;

namespace QCurl {

class QCNetworkReply;

namespace Internal {

/**
 * @brief 在任务 owner thread 独占断点续传目标文件的写入器。
 *
 * 该类型不是 QObject，不能复制或移动。QFile 与 QSaveFile 均为直接成员，实例只由
 * QCNetworkResumableDownloadJob 的 private state 唯一持有；所有写入、关闭、提交和析构
 * 必须发生在构造它的线程。
 */
class ResumableDownloadWriter final
{
public:
    /**
     * @brief 创建指定目标的唯一写入器。
     * @param savePath 目标文件路径。
     * @param existingSize 发起请求前记录的已有字节数。
     * @param hadExistingFile 发起请求前目标文件是否存在。
     */
    explicit ResumableDownloadWriter(QString savePath, qint64 existingSize, bool hadExistingFile);
    ~ResumableDownloadWriter();

    /**
     * @brief 消费 reply 当前可读数据并写入活动目标。
     * @param reply 当前 job 在 owner thread 使用的非 owning reply 借用。
     * @return 成功或没有数据时返回空；打开、范围或写入失败时返回稳定诊断。
     */
    [[nodiscard]] std::optional<QString> writeChunk(QCNetworkReply *reply);

    /**
     * @brief 根据 reply 终态关闭或提交当前目标。
     * @param reply 当前 job 在 owner thread 使用的非 owning reply 借用。
     * @return 成功或 reply 已失败时返回空；目标打开或提交失败时返回稳定诊断。
     */
    [[nodiscard]] std::optional<QString> commitIfNeeded(QCNetworkReply *reply);

#ifdef QCURL_ENABLE_TEST_HOOKS
    /** @brief 返回测试进程中当前存活的 writer 实例数。 */
    [[nodiscard]] static int activeInstanceCountForTesting() noexcept;

    /** @brief 返回最近一次 writer 析构发生的线程。 */
    [[nodiscard]] static QThread *lastDestructionThreadForTesting() noexcept;
#endif

private:
    Q_DISABLE_COPY_MOVE(ResumableDownloadWriter)

    [[nodiscard]] bool isAlreadyComplete(QCNetworkReply *reply) const;
    [[nodiscard]] std::optional<QString> decideWriteMode(QCNetworkReply *reply);
    [[nodiscard]] std::optional<QString> ensureWriteTarget(QCNetworkReply *reply);
    [[nodiscard]] QIODevice *activeTarget() noexcept;
    void closeTargets();
    void assertOwnerThread() const;

    QString m_savePath;
    qint64 m_existingSize    = 0;
    bool m_hadExistingFile   = false;
    bool m_modeDecided       = false;
    bool m_appendMode        = false;
    bool m_safeOverwriteMode = false;
    QThread *const m_ownerThread;
    QFile m_file;
    QSaveFile m_overwriteFile;
};

} // namespace Internal
} // namespace QCurl

#endif // QCNETWORKRESUMABLEDOWNLOADWRITER_P_H
