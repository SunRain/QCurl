/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtCore module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

/**
 * @file
 * @brief 声明 QCByteDataBuffer 内部字节缓冲工具。
 */

#ifndef QBYTEDATA_P_H
#define QBYTEDATA_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QByteArray>
#include <QList>

namespace QCurl {

/**
 * @brief 轻量级 QByteArray 链式缓冲区
 *
 * 基于多个 QByteArray 片段组织数据，优先复用已有块，减少不必要的
 * malloc/realloc/memcpy。
 */
class QCByteDataBuffer
{
private:
    QList<QByteArray> m_buffers;
    qint64 m_bufferCompleteSize;
    qint64 m_firstPos;

public:
    /// 构造一个空的字节缓冲区。
    QCByteDataBuffer()
        : m_bufferCompleteSize(0)
        , m_firstPos(0)
    {}

    /// 清空所有片段并释放当前缓冲状态。
    ~QCByteDataBuffer() { clear(); }

    /// 从单个 QByteArray 头部弹出指定字节数。
    static inline void popFront(QByteArray &ba, qint64 n)
    {
        ba = QByteArray(ba.constData() + n, ba.size() - n);
    }

    /// 在需要时压缩首块，确保 `firstPos` 回到 0。
    inline void squeezeFirst()
    {
        if (!m_buffers.isEmpty() && m_firstPos > 0) {
            popFront(m_buffers.first(), m_firstPos);
            m_firstPos = 0;
        }
    }

    inline void append(const QCByteDataBuffer &other)
    {
        if (other.isEmpty()) {
            return;
        }

        m_buffers.append(other.m_buffers);
        m_bufferCompleteSize += other.byteAmount();

        if (other.m_firstPos > 0) {
            popFront(m_buffers[bufferCount() - other.bufferCount()], other.m_firstPos);
        }
    }

    inline void append(const QByteArray &bd)
    {
        if (bd.isEmpty()) {
            return;
        }

        m_buffers.append(bd);
        m_bufferCompleteSize += bd.size();
    }

    inline void prepend(const QByteArray &bd)
    {
        if (bd.isEmpty()) {
            return;
        }

        squeezeFirst();

        m_buffers.prepend(bd);
        m_bufferCompleteSize += bd.size();
    }

    /// 取出首个数据块；适合零散读取场景。
    inline QByteArray read()
    {
        squeezeFirst();
        m_bufferCompleteSize -= m_buffers.first().size();
        return m_buffers.takeFirst();
    }

    /// 读取全部数据；可能触发额外的分配与拷贝。
    inline QByteArray readAll() { return read(byteAmount()); }

    /// 读取指定字节数；内部可能产生新的连续缓冲区。
    inline QByteArray read(qint64 amount)
    {
        amount = qMin(byteAmount(), amount);
        QByteArray byteData;
        byteData.resize(amount);
        read(byteData.data(), byteData.size());
        return byteData;
    }

    /// 读取指定字节数到外部缓冲区。
    qint64 read(char *dst, qint64 amount)
    {
        amount                = qMin(amount, byteAmount());
        qint64 originalAmount = amount;
        char *writeDst        = dst;

        while (amount > 0) {
            const QByteArray &first = m_buffers.first();
            qint64 firstSize        = first.size() - m_firstPos;
            if (amount >= firstSize) {
                // 整块消费，避免保留额外片段状态。
                m_bufferCompleteSize -= firstSize;
                amount -= firstSize;
                memcpy(writeDst, first.constData() + m_firstPos, firstSize);
                writeDst += firstSize;
                m_firstPos = 0;
                m_buffers.takeFirst();
            } else {
                // 仅消费首块前缀，保留剩余数据供后续读取。
                m_bufferCompleteSize -= amount;
                memcpy(writeDst, first.constData() + m_firstPos, amount);
                m_firstPos += amount;
                amount = 0;
            }
        }

        return originalAmount;
    }

    inline char getChar()
    {
        char c;
        read(&c, 1);
        return c;
    }

    inline void clear()
    {
        m_buffers.clear();
        m_bufferCompleteSize = 0;
        m_firstPos           = 0;
    }

    /// 返回所有片段合计的字节数。
    inline qint64 byteAmount() const { return m_bufferCompleteSize; }

    /// 返回当前片段数量。
    inline qint64 bufferCount() const { return m_buffers.length(); }

    inline bool isEmpty() const { return byteAmount() == 0; }

    inline qint64 sizeNextBlock() const
    {
        if (m_buffers.isEmpty()) {
            return 0;
        }
        return m_buffers.first().size() - m_firstPos;
    }

    inline QByteArray &operator[](int i)
    {
        if (i == 0) {
            squeezeFirst();
        }

        return m_buffers[i];
    }

    inline bool canReadLine() const
    {
        int i = 0;
        if (i < m_buffers.length()) {
            if (m_buffers.at(i).indexOf('\n', m_firstPos) != -1) {
                return true;
            }
            ++i;

            for (; i < m_buffers.length(); i++) {
                if (m_buffers.at(i).contains('\n')) {
                    return true;
                }
            }
        }
        return false;
    }
};

} // namespace QCurl

#endif // QBYTEDATA_P_H
