/***************************************************************************
 *   Copyright (C) 2009 Matthias Fuchs <mat69@gmx.net>                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA .        *
 ***************************************************************************/

#include "checksumsearch.h"

#include "core/verifier.h"
#include "kget_debug.h"

#include <QFile>
#include <QFileInfo>

#include <KLocalizedString>

const QStringList ChecksumSearch::URLCHANGEMODES = (QStringList() << i18n("Append") << i18n("Replace file") << i18n("Replace file-ending"));

ChecksumSearch::ChecksumSearch(const QList<QUrl> &srcs, const QString &fileName, const QStringList &types, QObject *parent)
    : QObject(parent)
    , m_copyJob(nullptr)
    , m_srcs(srcs)
    , m_fileName(fileName)
    , m_types(types)
{
    createDownload();
}

ChecksumSearch::~ChecksumSearch()
{
    if (m_copyJob) {
        m_copyJob->kill(KJob::Quietly);
    }
}

void ChecksumSearch::createDownload()
{
    if (m_srcs.isEmpty() || m_types.isEmpty()) {
        deleteLater();
    } else {
        m_src = m_srcs.takeFirst();
        m_type = m_types.takeFirst();
        m_isEmpty = m_type.isEmpty();

        m_copyJob = KIO::get(m_src, KIO::Reload, KIO::HideProgressInfo);
        m_copyJob->addMetaData("errorPage", "false");
        connect(m_copyJob, &KIO::TransferJob::data, this, &ChecksumSearch::slotData);
        connect(m_copyJob, &KJob::result, this, &ChecksumSearch::slotResult);
    }
}

void ChecksumSearch::slotData(KIO::Job *job, const QByteArray &data)
{
    Q_UNUSED(job)

    if (m_dataBA.size() > 5 * 1024) {
        m_copyJob->kill(KJob::EmitResult);
    } else {
        m_dataBA.append(data);
    }
}

void ChecksumSearch::slotResult(KJob *job)
{
    qCDebug(KGET_DEBUG);

    m_data.clear();

    switch (job->error()) {
    case 0: // The download has finished
    {
        qCDebug(KGET_DEBUG) << "Correctly downloaded" << m_src.toDisplayString();
        m_data = QString(m_dataBA);
        break;
    }

    default:
        qCDebug(KGET_DEBUG) << "There was error" << job->error() << "while downloading" << m_src.toDisplayString();
        break;
    }

    m_copyJob = nullptr;
    m_dataBA.clear();

    parseDownload();
}

void ChecksumSearch::parseDownload()
{
    if (!m_data.isEmpty()) {
        qCDebug(KGET_DEBUG) << "*******Parse*******\n" << m_data << "*******************";
    }

    // no type has been specified
    if (m_type.isEmpty()) {
        parseDownloadEmpty();
        return;
    }

    const int length = Verifier::diggestLength(m_type);

    const QString patternChecksum = QString("\\w{%1}").arg(length);
    QRegExp rxChecksum(patternChecksum);
    QString hash;

    // find the correct line
    const QStringList lines = m_data.split('\n');
    foreach (const QString &line, lines) {
        if (line.contains(m_fileName, Qt::CaseInsensitive)) {
            if (rxChecksum.indexIn(line) > -1) {
                hash = rxChecksum.cap(0).toLower();
                if (!m_fileName.contains(hash, Qt::CaseInsensitive)) {
                    qCDebug(KGET_DEBUG) << "Found hash: " << hash;
                    Q_EMIT data(m_type, hash);
                }
            }
        }
    }

    // nothing found yet, so simply search for a word in the whole data that has the correct length
    if (hash.isEmpty() && (rxChecksum.indexIn(m_data) > -1)) {
        QString hash = rxChecksum.cap(0);
        if (!m_fileName.contains(hash, Qt::CaseInsensitive)) {
            qCDebug(KGET_DEBUG) << "Found hash:" << hash;
            Q_EMIT data(m_type, hash);
        }
    }

    // only create a download here if type was specified, otherwise parseDownloadEmpty has to handle this
    if (!m_isEmpty) {
        createDownload();
    }
}

void ChecksumSearch::parseDownloadEmpty()
{
    const QStringList lines = m_data.split('\n');
    const QStringList supportedTypes = Verifier::supportedVerficationTypes();
    foreach (const QString &type, supportedTypes) {
        if (m_data.contains(type, Qt::CaseInsensitive)) {
            m_type = type;
            parseDownload();
        }
    }

    createDownload();
}

QUrl ChecksumSearch::createUrl(const QUrl &src, const QString &change, ChecksumSearch::UrlChangeMode mode)
{
    if (!src.isValid() || change.isEmpty()) {
        return QUrl();
    }

    QUrl url;
    if (mode == kg_Append) {
        url = QUrl(src.toString() + change);
    } else if (mode == kg_ReplaceFile) {
        QUrl temp = src.adjusted(QUrl::RemoveFilename);
        temp.setPath(temp.path() + change);
        url = temp;
    } else if (mode == kg_ReplaceEnding) {
        QString fileName = src.fileName();
        int index = fileName.lastIndexOf('.');
        if (index > -1) {
            fileName = fileName.left(index) + change;
            QUrl temp = src.adjusted(QUrl::RemoveFilename);
            temp.setPath(temp.path() + fileName);
            url = temp;
        }
    }

    return url;
}

#include "moc_checksumsearch.cpp"
