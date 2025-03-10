/* This file is part of the KDE project

   Copyright (C) 2004 Dario Massarin <nekkar@libero.it>
   Copyright (C) 2006 Manolo Valdes <nolis71cu@gmail.com>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
*/

#include "transfermultisegkio.h"

#include "core/kget.h"
#include "core/transferdatasource.h"
#include "multisegkiosettings.h"
// #include "mirrors.h"
#include "core/filemodel.h"
#include "core/signature.h"
#include "core/verifier.h"

#ifdef Q_OS_WIN
#include <sys/utime.h>
#else
#include <utime.h>
#endif

#include "kget_debug.h"

#include <KIO/CopyJob>
#include <KLocalizedString>
#include <KMessageBox>
#include <kwidgetsaddons_version.h>

#include <QDebug>
#include <QDomElement>
#include <QFile>

TransferMultiSegKio::TransferMultiSegKio(TransferGroup *parent,
                                         TransferFactory *factory,
                                         Scheduler *scheduler,
                                         const QUrl &source,
                                         const QUrl &dest,
                                         const QDomElement *e)
    : Transfer(parent, factory, scheduler, source, dest, e)
    , m_movingFile(false)
    , m_searchStarted(false)
    , m_verificationSearch(false)
    , m_dataSourceFactory(nullptr)
    , m_fileModel(nullptr)
{
}

void TransferMultiSegKio::init()
{
    Transfer::init();

    if (!m_dataSourceFactory) {
        m_dataSourceFactory = new DataSourceFactory(this, m_dest);
        connect(m_dataSourceFactory, &DataSourceFactory::capabilitiesChanged, this, &TransferMultiSegKio::slotUpdateCapabilities);
        connect(m_dataSourceFactory, &DataSourceFactory::dataSourceFactoryChange, this, &TransferMultiSegKio::slotDataSourceFactoryChange);
        connect(m_dataSourceFactory->verifier(), &Verifier::verified, this, &TransferMultiSegKio::slotVerified);
        connect(m_dataSourceFactory, &DataSourceFactory::log, this, &Transfer::setLog);

        m_dataSourceFactory->addMirror(m_source, MultiSegKioSettings::segments());

        slotUpdateCapabilities();
    }
}

void TransferMultiSegKio::deinit(Transfer::DeleteOptions options)
{
    if (options & Transfer::DeleteFiles) // if the transfer is not finished, we delete the *.part-file
    {
        m_dataSourceFactory->deinit();
    } // TODO: Ask the user if he/she wants to delete the *.part-file? To discuss (boom1992)
}

void TransferMultiSegKio::start()
{
    qCDebug(KGET_DEBUG) << "Start TransferMultiSegKio";
    if (status() == Running) {
        return;
    }

    m_dataSourceFactory->start();

    if (MultiSegKioSettings::useSearchEngines() && !m_searchStarted) {
        m_searchStarted = true;
        QDomDocument doc;
        QDomElement element = doc.createElement("TransferDataSource");
        element.setAttribute("type", "search");
        doc.appendChild(element);

        TransferDataSource *mirrorSearch = KGet::createTransferDataSource(m_source, element, this);
        if (mirrorSearch) {
            connect(mirrorSearch, SIGNAL(data(QList<QUrl>)), this, SLOT(slotSearchUrls(QList<QUrl>)));
            mirrorSearch->start();
        }
    }
}

void TransferMultiSegKio::stop()
{
    qCDebug(KGET_DEBUG);

    if ((status() == Stopped) || (status() == Finished)) {
        return;
    }

    if (m_dataSourceFactory) {
        m_dataSourceFactory->stop();
    }
}

bool TransferMultiSegKio::repair(const QUrl &file)
{
    if (!file.isValid() || (m_dest == file)) {
        if (m_dataSourceFactory && (m_dataSourceFactory->verifier()->status() == Verifier::NotVerified)) {
            m_dataSourceFactory->repair();
            return true;
        }
    }

    return false;
}

bool TransferMultiSegKio::setDirectory(const QUrl &newDirectory)
{
    QUrl newDest = newDirectory;
    newDest.setPath(newDest.path() + "/" + m_dest.fileName());
    return setNewDestination(newDest);
}

bool TransferMultiSegKio::setNewDestination(const QUrl &newDestination)
{
    qCDebug(KGET_DEBUG) << "New destination: " << newDestination;
    if (newDestination.isValid() && (newDestination != dest()) && m_dataSourceFactory) {
        m_movingFile = true;
        stop();
        m_dataSourceFactory->setNewDestination(newDestination);

        m_dest = newDestination;

        if (m_fileModel) {
            m_fileModel->setDirectory(directory());
        }

        setTransferChange(Tc_FileName);
        return true;
    }
    return false;
}

void TransferMultiSegKio::load(const QDomElement *element)
{
    qCDebug(KGET_DEBUG);

    Transfer::load(element);
    m_dataSourceFactory->load(element);
}

void TransferMultiSegKio::save(const QDomElement &element)
{
    qCDebug(KGET_DEBUG);
    Transfer::save(element);
    m_dataSourceFactory->save(element);
}

void TransferMultiSegKio::slotDataSourceFactoryChange(Transfer::ChangesFlags change)
{
    if (change & Tc_FileName) {
        QList<QUrl> urls = m_dataSourceFactory->mirrors().keys();
        QString filename = urls.first().fileName();
        if (filename.isEmpty())
            return;
        foreach (const QUrl url, urls) {
            if (filename != url.fileName())
                return;
        }
        QUrl path = m_dest.adjusted(QUrl::RemoveFilename);
        path.setPath(path.path() + filename);
        setNewDestination(path);
    }
    if (change & Tc_Source) {
        m_source = QUrl();
        QHash<QUrl, QPair<bool, int>> mirrors = m_dataSourceFactory->mirrors();
        QHash<QUrl, QPair<bool, int>>::const_iterator it = mirrors.constBegin();
        QHash<QUrl, QPair<bool, int>>::const_iterator end = mirrors.constEnd();
        for (; it != end; it++) {
            if (it.value().first) {
                m_source = it.key();
                break;
            }
        }
    }
    if (change & Tc_Status) {
        if ((m_dataSourceFactory->status() == Job::Finished) && m_source.scheme() == "ftp") {
            KIO::StatJob *statJob = KIO::stat(m_source);
            connect(statJob, &KJob::result, this, &TransferMultiSegKio::slotStatResult);
            statJob->start();
        } else {
            setStatus(m_dataSourceFactory->status());
        }

        if (m_fileModel) {
            QModelIndex statusIndex = m_fileModel->index(m_dest, FileItem::Status);
            m_fileModel->setData(statusIndex, status());
        }
    }
    if (change & Tc_TotalSize) {
        m_totalSize = m_dataSourceFactory->size();
        if (m_fileModel) {
            QModelIndex sizeIndex = m_fileModel->index(m_dest, FileItem::Size);
            m_fileModel->setData(sizeIndex, static_cast<qlonglong>(m_totalSize));
        }
    }
    if (change & Tc_DownloadedSize) {
        KIO::filesize_t processedSize = m_dataSourceFactory->downloadedSize();
        // only start the verification search _after_ data has come in, that way only connections
        // are requested if there is already a successful one
        if ((processedSize != m_downloadedSize) && !m_verificationSearch && MultiSegKioSettings::useSearchVerification()) {
            m_verificationSearch = true;
            QDomDocument doc;
            QDomElement element = doc.createElement("TransferDataSource");
            element.setAttribute("type", "checksumsearch");
            doc.appendChild(element);

            TransferDataSource *checksumSearch = KGet::createTransferDataSource(m_source, element, this);
            if (checksumSearch) {
                connect(checksumSearch, SIGNAL(data(QString, QString)), this, SLOT(slotChecksumFound(QString, QString)));
                checksumSearch->start();
            }
        }
        m_downloadedSize = m_dataSourceFactory->downloadedSize();
    }
    if (change & Tc_Percent) {
        m_percent = m_dataSourceFactory->percent();
    }
    if (change & Tc_DownloadSpeed) {
        qCDebug(KGET_DEBUG) << "speed:" << m_downloadSpeed;
        m_downloadSpeed = m_dataSourceFactory->currentSpeed();
    }

    setTransferChange(change, true);
}

void TransferMultiSegKio::slotVerified(bool isVerified)
{
    if (m_fileModel) {
        QModelIndex checksumVerified = m_fileModel->index(m_dest, FileItem::ChecksumVerified);
        m_fileModel->setData(checksumVerified, verifier()->status());
    }

    if (!isVerified) {
        QString text;
        KGuiItem action;
        if (verifier()->partialChunkLength()) {
            text = i18n("The download (%1) could not be verified. Do you want to repair it?", m_dest.fileName());
            action = KGuiItem(i18nc("@action:button", "Repair"));
        } else {
            text = i18n("The download (%1) could not be verified. Do you want to redownload it?", m_dest.fileName());
            action = KGuiItem(i18nc("@action:button", "Download Again"), QStringLiteral("document-save"));
        }
#if KWIDGETSADDONS_VERSION >= QT_VERSION_CHECK(5, 100, 0)
        if (KMessageBox::warningTwoActions(nullptr,
#else
        if (KMessageBox::warningYesNo(nullptr,
#endif
                                           text,
                                           i18n("Verification failed."),
                                           action,
                                           KGuiItem(i18n("Ignore"), QStringLiteral("dialog-cancel")))
#if KWIDGETSADDONS_VERSION >= QT_VERSION_CHECK(5, 100, 0)
            == KMessageBox::PrimaryAction) {
#else
            == KMessageBox::Yes) {
#endif
            repair();
        }
    }
}

void TransferMultiSegKio::slotStatResult(KJob *kioJob)
{
    auto *statJob = qobject_cast<KIO::StatJob *>(kioJob);

    if (!statJob->error()) {
        const KIO::UDSEntry entryResult = statJob->statResult();
        struct utimbuf time;

        time.modtime = entryResult.numberValue(KIO::UDSEntry::UDS_MODIFICATION_TIME);
        time.actime = QDateTime::currentDateTime().toSecsSinceEpoch();
        utime(m_dest.toLocalFile().toUtf8().constData(), &time);
    }

    setStatus(Job::Finished);
    setTransferChange(Tc_Status, true);
}

void TransferMultiSegKio::slotSearchUrls(const QList<QUrl> &urls)
{
    qCDebug(KGET_DEBUG) << "Found " << urls.size() << " urls.";

    foreach (const QUrl &url, urls) {
        m_dataSourceFactory->addMirror(url, MultiSegKioSettings::segments());
    }
}

void TransferMultiSegKio::slotChecksumFound(QString type, QString checksum)
{
    m_dataSourceFactory->verifier()->addChecksum(type, checksum);
}

QHash<QUrl, QPair<bool, int>> TransferMultiSegKio::availableMirrors(const QUrl &file) const
{
    Q_UNUSED(file)

    return m_dataSourceFactory->mirrors();
}

void TransferMultiSegKio::setAvailableMirrors(const QUrl &file, const QHash<QUrl, QPair<bool, int>> &mirrors)
{
    Q_UNUSED(file)

    m_dataSourceFactory->setMirrors(mirrors);

    m_source = QUrl();
    QHash<QUrl, QPair<bool, int>>::const_iterator it = mirrors.begin();
    QHash<QUrl, QPair<bool, int>>::const_iterator end = mirrors.end();
    for (; it != end; it++) {
        if (it.value().first) {
            m_source = it.key();
            break;
        }
    }
    setTransferChange(Tc_Source, true);
}

Verifier *TransferMultiSegKio::verifier(const QUrl &file)
{
    Q_UNUSED(file)

    return m_dataSourceFactory->verifier();
}

Signature *TransferMultiSegKio::signature(const QUrl &file)
{
    Q_UNUSED(file)

    return m_dataSourceFactory->signature();
}

FileModel *TransferMultiSegKio::fileModel()
{
    if (!m_fileModel) {
        m_fileModel = new FileModel(QList<QUrl>() << m_dest, m_dest.adjusted(QUrl::RemoveFilename), this);
        connect(m_fileModel, SIGNAL(rename(QUrl, QUrl)), this, SLOT(slotRename(QUrl, QUrl)));

        QModelIndex statusIndex = m_fileModel->index(m_dest, FileItem::Status);
        m_fileModel->setData(statusIndex, m_dataSourceFactory->status());
        QModelIndex sizeIndex = m_fileModel->index(m_dest, FileItem::Size);
        m_fileModel->setData(sizeIndex, static_cast<qlonglong>(m_dataSourceFactory->size()));
        QModelIndex checksumVerified = m_fileModel->index(m_dest, FileItem::ChecksumVerified);
        m_fileModel->setData(checksumVerified, verifier()->status());
        QModelIndex signatureVerified = m_fileModel->index(m_dest, FileItem::SignatureVerified);
        m_fileModel->setData(signatureVerified, signature()->status());
    }

    return m_fileModel;
}

void TransferMultiSegKio::slotRename(const QUrl &oldUrl, const QUrl &newUrl)
{
    Q_UNUSED(oldUrl)

    if (newUrl.isValid() && (newUrl != dest()) && m_dataSourceFactory) {
        m_movingFile = true;
        stop();
        m_dataSourceFactory->setNewDestination(newUrl);

        m_dest = newUrl;

        setTransferChange(Tc_FileName);
    }
}

void TransferMultiSegKio::slotUpdateCapabilities()
{
    setCapabilities(m_dataSourceFactory->capabilities());
}

#include "moc_transfermultisegkio.cpp"
