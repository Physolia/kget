/* This file is part of the KDE project

   Copyright (C) 2004 Dario Massarin <nekkar@libero.it>
   Copyright (C) 2007 Manolo Valdes <nolis71cu@gmail.com>
   Copyright (C) 2009 Matthias Fuchs <mat69@gmx.net>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
*/

#include "metalink.h"
#include "fileselectiondlg.h"
#include "metalinksettings.h"

#include "core/download.h"
#include "core/filemodel.h"
#include "core/kget.h"
#include "core/signature.h"
#include "core/transferdatasource.h"
#include "core/transfergroup.h"
#include "core/urlchecker.h"
#include "core/verifier.h"

#include <algorithm>

#include <KConfigGroup>
#include <KIO/DeleteJob>
#include <KIO/RenameDialog>
#include <KLocalizedString>
#include <KMessageBox>
#include <kwidgetsaddons_version.h>

#include <QDebug>
#include <QDialog>
#include <QDomElement>
#include <QFile>
#include <QStandardPaths>

Metalink::Metalink(TransferGroup *parent, TransferFactory *factory, Scheduler *scheduler, const QUrl &source, const QUrl &dest, const QDomElement *e)
    : Transfer(parent, factory, scheduler, source, dest, e)
    , m_fileModel(nullptr)
    , m_currentFiles(0)
    , m_metalinkJustDownloaded(false)
    , m_ready(false)
    , m_speedCount(0)
    , m_tempAverageSpeed(0)
    , m_averageSpeed(0)
{
}

Metalink::~Metalink()
{
}

void Metalink::start()
{
    qCDebug(KGET_DEBUG) << "metalink::start";

    if (!m_ready) {
        if (m_localMetalinkLocation.isValid() && metalinkInit()) {
            startMetalink();
        } else {
            downloadMetalink();
        }
    } else {
        startMetalink();
    }
}

void Metalink::downloadMetalink()
{
    m_metalinkJustDownloaded = true;

    setStatus(Job::Stopped, i18n("Downloading Metalink File...."), "document-save");
    setTransferChange(Tc_Status, true);
    // make sure that the DataLocation directory exists (earlier this used to be handled by KStandardDirs)
    if (!QFileInfo::exists(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))) {
        QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    }
    Download *download =
        new Download(m_source, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/metalinks/") + m_source.fileName());
    connect(download, SIGNAL(finishedSuccessfully(QUrl, QByteArray)), SLOT(metalinkInit(QUrl, QByteArray)));
}

bool Metalink::metalinkInit(const QUrl &src, const QByteArray &data)
{
    qCDebug(KGET_DEBUG);

    if (!src.isEmpty()) {
        m_localMetalinkLocation = src;
    }

    // use the downloaded metalink-file data directly if possible
    if (!data.isEmpty()) {
        KGetMetalink::HandleMetalink::load(data, &m_metalink);
    }

    // try to parse the locally stored metalink-file
    if (!m_metalink.isValid() && m_localMetalinkLocation.isValid()) {
        KGetMetalink::HandleMetalink::load(m_localMetalinkLocation.toLocalFile(), &m_metalink);
    }

    if (!m_metalink.isValid()) {
        qCCritical(KGET_DEBUG) << "Unknown error when trying to load the .metalink-file. Metalink is not valid.";
        setStatus(Job::Aborted);
        setTransferChange(Tc_Status, true);
        return false;
    }

    // offers a dialog to download the newest version of a dynamic metalink
    if ((m_source.isLocalFile() || !m_metalinkJustDownloaded) && m_metalink.dynamic && (UrlChecker::checkSource(m_metalink.origin) == UrlChecker::NoError)) {
#if KWIDGETSADDONS_VERSION >= QT_VERSION_CHECK(5, 100, 0)
        if (KMessageBox::questionTwoActions(nullptr,
#else
        if (KMessageBox::questionYesNo(nullptr,
#endif
                                            i18n("A newer version of this Metalink might exist, do you want to download it?"),
                                            i18n("Redownload Metalink"),
                                            KGuiItem(i18nc("@action:button", "Download Again"), QStringLiteral("view-refresh")),
                                            KGuiItem(i18nc("@action:button", "Ignore"), QStringLiteral("dialog-cancel")))
#if KWIDGETSADDONS_VERSION >= QT_VERSION_CHECK(5, 100, 0)
            == KMessageBox::PrimaryAction) {
#else
            == KMessageBox::Yes) {
#endif
            m_localMetalinkLocation.clear();
            m_source = m_metalink.origin;
            downloadMetalink();
            return false;
        }
    }

    QList<KGetMetalink::File>::const_iterator it;
    QList<KGetMetalink::File>::const_iterator itEnd = m_metalink.files.files.constEnd();
    m_totalSize = 0;
    KIO::fileoffset_t segSize = 500 * 1024; // TODO use config here!
    const QUrl tempDest = QUrl(m_dest.directory());
    QUrl dest;
    for (it = m_metalink.files.files.constBegin(); it != itEnd; ++it) {
        dest = tempDest;
        dest.addPath((*it).name);

        QList<KGetMetalink::Url> urlList = (*it).resources.urls;
        // sort the urls according to their priority (highest first)
        std::sort(urlList.begin(), urlList.end(), [](const KGetMetalink::Url &a, const KGetMetalink::Url &b) {
            return b < a;
        });

        KIO::filesize_t fileSize = (*it).size;
        m_totalSize += fileSize;

        // create a DataSourceFactory for each separate file
        DataSourceFactory *dataFactory = new DataSourceFactory(this, dest, fileSize, segSize);
        dataFactory->setMaxMirrorsUsed(MetalinkSettings::mirrorsPerFile());

        // TODO compare available file size (<size>) with the sizes of the server while downloading?

        connect(dataFactory, SIGNAL(capabilitiesChanged()), this, SLOT(slotUpdateCapabilities()));
        connect(dataFactory, SIGNAL(dataSourceFactoryChange(Transfer::ChangesFlags)), this, SLOT(slotDataSourceFactoryChange(Transfer::ChangesFlags)));
        connect(dataFactory->verifier(), SIGNAL(verified(bool)), this, SLOT(slotVerified(bool)));
        connect(dataFactory->signature(), SIGNAL(verified(int)), this, SLOT(slotSignatureVerified()));
        connect(dataFactory, SIGNAL(log(QString, Transfer::LogLevel)), this, SLOT(setLog(QString, Transfer::LogLevel)));

        // add the DataSources
        for (int i = 0; i < urlList.size(); ++i) {
            const QUrl url = urlList[i].url;
            if (url.isValid()) {
                dataFactory->addMirror(url, MetalinkSettings::connectionsPerUrl());
            }
        }
        // no datasource has been created, so remove the datasource factory
        if (dataFactory->mirrors().isEmpty()) {
            delete dataFactory;
        } else {
            dataFactory->verifier()->addChecksums((*it).verification.hashes);
            foreach (const KGetMetalink::Pieces &pieces, (*it).verification.pieces) {
                dataFactory->verifier()->addPartialChecksums(pieces.type, pieces.length, pieces.hashes);
            }

            const QHash<QString, QString> signatures = (*it).verification.signatures;
            QHash<QString, QString>::const_iterator it;
            QHash<QString, QString>::const_iterator itEnd = signatures.constEnd();
            for (it = signatures.constBegin(); it != itEnd; ++it) {
                if (it.key().toLower() == "pgp") {
                    dataFactory->signature()->setAsciiDetachedSignature(*it);
                }
            }

            m_dataSourceFactory[dataFactory->dest()] = dataFactory;
        }
    }

    if ((m_metalink.files.files.size() == 1) && m_dataSourceFactory.size()) {
        m_dest = dest;
    }

    if (!m_dataSourceFactory.size()) {
        // TODO make this via log in the future + do not display the KMessageBox
        qCWarning(KGET_DEBUG) << "Download of" << m_source << "failed, no working URLs were found.";
        KMessageBox::error(nullptr, i18n("Download failed, no working URLs were found."), i18n("Error"));
        setStatus(Job::Aborted);
        setTransferChange(Tc_Status, true);
        return false;
    }

    m_ready = !m_dataSourceFactory.isEmpty();
    slotUpdateCapabilities();

    // the metalink-file has just been downloaded, so ask the user to choose the files that
    //  should be downloaded
    if (m_metalinkJustDownloaded) {
        QDialog *dialog = new FileSelectionDlg(fileModel());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, SIGNAL(finished(int)), this, SLOT(fileDlgFinished(int)));

        dialog->show();
    }

    return true;
}

void Metalink::untickAllFiles()
{
    for (int row = 0; row < fileModel()->rowCount(); ++row) {
        QModelIndex index = fileModel()->index(row, FileItem::File);
        if (index.isValid()) {
            fileModel()->setData(index, Qt::Unchecked, Qt::CheckStateRole);
        }
    }
}

void Metalink::fileDlgFinished(int result)
{
    // the dialog was not accepted untick every file, this ensures that the user does not
    // press start by accident without first selecting the desired files
    if (result != QDialog::Accepted) {
        untickAllFiles();
    }

    filesSelected();

    // no files selected to download or dialog rejected, stop the download
    if (!m_numFilesSelected || (result != QDialog::Accepted)) {
        setStatus(Job::Stopped);
        setTransferChange(Tc_Status, true);
        return;
    }

    startMetalink();
}

void Metalink::startMetalink()
{
    if (m_ready) {
        foreach (DataSourceFactory *factory, m_dataSourceFactory) {
            // specified number of files is downloaded simultaneously
            if (m_currentFiles < MetalinkSettings::simultaneousFiles()) {
                const int status = factory->status();
                // only start factories that should be downloaded
                if (factory->doDownload() && (status != Job::Finished) && (status != Job::FinishedKeepAlive) && (status != Job::Running)) {
                    ++m_currentFiles;
                    factory->start();
                }
            } else {
                break;
            }
        }
    }
}

void Metalink::deinit(Transfer::DeleteOptions options)
{
    foreach (DataSourceFactory *factory, m_dataSourceFactory) {
        if (options & Transfer::DeleteFiles) {
            factory->deinit();
        }
    } // TODO: Ask the user if he/she wants to delete the *.part-file? To discuss (boom1992)

    // FIXME does that mean, that the metalink file is always removed, even if
    // downloaded by the user?
    if ((options & Transfer::DeleteTemporaryFiles) && m_localMetalinkLocation.isLocalFile()) {
        KIO::Job *del = KIO::del(m_localMetalinkLocation, KIO::HideProgressInfo);
        if (!del->exec()) {
            qCDebug(KGET_DEBUG) << "Could not delete " << m_localMetalinkLocation.path();
        }
    }
}

void Metalink::stop()
{
    qCDebug(KGET_DEBUG) << "metalink::Stop";
    if (m_ready && status() != Stopped) {
        m_currentFiles = 0;
        foreach (DataSourceFactory *factory, m_dataSourceFactory) {
            factory->stop();
        }
    }
}

void Metalink::slotDataSourceFactoryChange(Transfer::ChangesFlags change)
{
    if ((change & Tc_Status) | (change & Tc_TotalSize)) {
        DataSourceFactory *factory = qobject_cast<DataSourceFactory *>(sender());
        if (change & Tc_Status) {
            bool changeStatus;
            updateStatus(factory, &changeStatus);
            if (!changeStatus) {
                change &= ~Tc_Status;
            }
        }
        if (change & Tc_TotalSize) {
            recalculateTotalSize(factory);
        }
    }
    if (change & Tc_DownloadedSize) {
        recalculateProcessedSize();
        change |= Tc_Percent;
    }
    if (change & Tc_DownloadSpeed) {
        recalculateSpeed();
    }

    setTransferChange(change, true);
}

void Metalink::recalculateTotalSize(DataSourceFactory *sender)
{
    m_totalSize = 0;
    foreach (DataSourceFactory *factory, m_dataSourceFactory) {
        if (factory->doDownload()) {
            m_totalSize += factory->size();
        }
    }

    if (m_fileModel) {
        if (sender) {
            QModelIndex sizeIndex = m_fileModel->index(sender->dest(), FileItem::Size);
            m_fileModel->setData(sizeIndex, static_cast<qlonglong>(sender->size()));
        }
    }
}

void Metalink::recalculateProcessedSize()
{
    m_downloadedSize = 0;
    foreach (DataSourceFactory *factory, m_dataSourceFactory) {
        if (factory->doDownload()) {
            m_downloadedSize += factory->downloadedSize();
        }
    }

    if (m_totalSize) {
        m_percent = (m_downloadedSize * 100) / m_totalSize;
    } else {
        m_percent = 0;
    }
}

void Metalink::recalculateSpeed()
{
    m_downloadSpeed = 0;
    foreach (DataSourceFactory *factory, m_dataSourceFactory) {
        if (factory->doDownload()) {
            m_downloadSpeed += factory->currentSpeed();
        }
    }

    // calculate the average of the last three speeds
    m_tempAverageSpeed += m_downloadSpeed;
    ++m_speedCount;
    if (m_speedCount == 3) {
        m_averageSpeed = m_tempAverageSpeed / 3;
        m_speedCount = 0;
        m_tempAverageSpeed = 0;
    }
}

int Metalink::remainingTime() const
{
    if (!m_averageSpeed) {
        m_averageSpeed = m_downloadSpeed;
    }
    return KIO::calculateRemainingSeconds(m_totalSize, m_downloadedSize, m_averageSpeed);
}

void Metalink::updateStatus(DataSourceFactory *sender, bool *changeStatus)
{
    Job::Status status = (sender ? sender->status() : Job::Stopped);
    *changeStatus = true;
    switch (status) {
    case Job::Aborted:
    case Job::Stopped: {
        m_currentFiles = 0;
        foreach (DataSourceFactory *factory, m_dataSourceFactory) {
            // one factory is still running, do not change the status
            if (factory->doDownload() && (factory->status() == Job::Running)) {
                *changeStatus = false;
                ++m_currentFiles;
            }
        }

        if (*changeStatus) {
            setStatus(status);
        }
        break;
    }
    case Job::Finished:
        // one file that has been downloaded now is finished//FIXME ignore downloads that were finished in the previous download!!!!
        if (m_currentFiles) {
            --m_currentFiles;
            startMetalink();
        }
        foreach (DataSourceFactory *factory, m_dataSourceFactory) {
            // one factory is not finished, do not change the status
            if (factory->doDownload() && (factory->status() != Job::Finished)) {
                *changeStatus = false;
                break;
            }
        }

        if (*changeStatus) {
            setStatus(Job::Finished);
        }
        break;

    default:
        setStatus(status);
        break;
    }

    if (m_fileModel) {
        if (sender) {
            QModelIndex statusIndex = m_fileModel->index(sender->dest(), FileItem::Status);
            m_fileModel->setData(statusIndex, status);
        }
    }
}

void Metalink::slotVerified(bool isVerified)
{
    Q_UNUSED(isVerified)

    if (status() == Job::Finished) {
        // see if some files are NotVerified
        QStringList brokenFiles;
        foreach (DataSourceFactory *factory, m_dataSourceFactory) {
            if (m_fileModel) {
                QModelIndex checksumVerified = m_fileModel->index(factory->dest(), FileItem::ChecksumVerified);
                m_fileModel->setData(checksumVerified, factory->verifier()->status());
            }
            if (factory->doDownload() && (factory->verifier()->status() == Verifier::NotVerified)) {
                brokenFiles.append(factory->dest().pathOrUrl());
            }
        }

        if (brokenFiles.count()) {
#if KWIDGETSADDONS_VERSION >= QT_VERSION_CHECK(5, 100, 0)
            if (KMessageBox::warningTwoActionsList(
                    nullptr,
#else
            if (KMessageBox::warningYesNoList(
                    nullptr,
#endif
                    i18n("The download could not be verified, do you want to repair (if repairing does not work the download would be restarted) it?"),
                    brokenFiles,
                    QString(),
                    KGuiItem(i18nc("@action:button", "Repair")),
                    KGuiItem(i18nc("@action:button", "Ignore"), QStringLiteral("dialog-cancel")))
#if KWIDGETSADDONS_VERSION >= QT_VERSION_CHECK(5, 100, 0)
                == KMessageBox::PrimaryAction) {
#else
                == KMessageBox::Yes) {
#endif
                if (repair()) {
                    return;
                }
            }
        }
    }
}

void Metalink::slotSignatureVerified()
{
    if (status() == Job::Finished) {
        // see if some files are NotVerified
        QStringList brokenFiles;
        foreach (DataSourceFactory *factory, m_dataSourceFactory) {
            if (m_fileModel) {
                QModelIndex signatureVerified = m_fileModel->index(factory->dest(), FileItem::SignatureVerified);
                m_fileModel->setData(signatureVerified, factory->signature()->status());
            }
            if (factory->doDownload() && (factory->verifier()->status() == Verifier::NotVerified)) {
                brokenFiles.append(factory->dest().pathOrUrl());
            }
        }
        /*
                if (brokenFiles.count())//TODO
                {
                    if (KMessageBox::warningYesNoCancelList(nullptr,
                        i18n("The download could not be verified, try to repair it?"),
                             brokenFiles) == KMessageBox::Yes)
                    {
                        if (repair())
                        {
                            return;
                        }
                    }
                }*/
    }
}

bool Metalink::repair(const QUrl &file)
{
    if (file.isValid()) {
        if (m_dataSourceFactory.contains(file)) {
            DataSourceFactory *broken = m_dataSourceFactory[file];
            if (broken->verifier()->status() == Verifier::NotVerified) {
                broken->repair();
                return true;
            }
        }
    } else {
        QList<DataSourceFactory *> broken;
        foreach (DataSourceFactory *factory, m_dataSourceFactory) {
            if (factory->doDownload() && (factory->verifier()->status() == Verifier::NotVerified)) {
                broken.append(factory);
            }
        }
        if (broken.count()) {
            foreach (DataSourceFactory *factory, broken) {
                factory->repair();
            }
            return true;
        }
    }

    return false;
}

void Metalink::load(const QDomElement *element)
{
    Transfer::load(element);

    if (!element) {
        return;
    }

    const QDomElement e = *element;
    m_localMetalinkLocation = QUrl(e.attribute("LocalMetalinkLocation"));
    QDomNodeList factories = e.firstChildElement("factories").elementsByTagName("factory");

    // no stored information found, stop right here
    if (!factories.count()) {
        return;
    }

    while (factories.count()) {
        QDomDocument doc;
        QDomElement factory = doc.createElement("factories");
        factory.appendChild(factories.item(0).toElement());
        doc.appendChild(factory);

        DataSourceFactory *file = new DataSourceFactory(this);
        file->load(&factory);
        connect(file, SIGNAL(capabilitiesChanged()), this, SLOT(slotUpdateCapabilities()));
        connect(file, SIGNAL(dataSourceFactoryChange(Transfer::ChangesFlags)), this, SLOT(slotDataSourceFactoryChange(Transfer::ChangesFlags)));
        m_dataSourceFactory[file->dest()] = file;
        connect(file->verifier(), SIGNAL(verified(bool)), this, SLOT(slotVerified(bool)));
        connect(file->signature(), SIGNAL(verified(int)), this, SLOT(slotSignatureVerified()));
        connect(file, SIGNAL(log(QString, Transfer::LogLevel)), this, SLOT(setLog(QString, Transfer::LogLevel)));

        // start the DataSourceFactories that were Started when KGet was closed
        if (file->status() == Job::Running) {
            if (m_currentFiles < MetalinkSettings::simultaneousFiles()) {
                ++m_currentFiles;
                file->start();
            } else {
                // enough simultaneous files already, so increase the number and set file to stop --> that will decrease the number again
                file->stop();
            }
        }
    }
    m_ready = !m_dataSourceFactory.isEmpty();
    slotUpdateCapabilities();
}

void Metalink::save(const QDomElement &element)
{
    Transfer::save(element);

    QDomElement e = element;
    e.setAttribute("LocalMetalinkLocation", m_localMetalinkLocation.url());

    foreach (DataSourceFactory *factory, m_dataSourceFactory) {
        factory->save(e);
    }
}

Verifier *Metalink::verifier(const QUrl &file)
{
    if (!m_dataSourceFactory.contains(file)) {
        return nullptr;
    }

    return m_dataSourceFactory[file]->verifier();
}

Signature *Metalink::signature(const QUrl &file)
{
    if (!m_dataSourceFactory.contains(file)) {
        return nullptr;
    }

    return m_dataSourceFactory[file]->signature();
}

QList<QUrl> Metalink::files() const
{
    return m_dataSourceFactory.keys();
}

FileModel *Metalink::fileModel()
{
    if (!m_fileModel) {
        m_fileModel = new FileModel(files(), directory(), this);
        connect(m_fileModel, SIGNAL(rename(QUrl, QUrl)), this, SLOT(slotRename(QUrl, QUrl)));
        connect(m_fileModel, SIGNAL(checkStateChanged()), this, SLOT(filesSelected()));

        foreach (DataSourceFactory *factory, m_dataSourceFactory) {
            const QUrl dest = factory->dest();
            QModelIndex size = m_fileModel->index(dest, FileItem::Size);
            m_fileModel->setData(size, static_cast<qlonglong>(factory->size()));
            QModelIndex status = m_fileModel->index(dest, FileItem::Status);
            m_fileModel->setData(status, factory->status());
            QModelIndex checksumVerified = m_fileModel->index(dest, FileItem::ChecksumVerified);
            m_fileModel->setData(checksumVerified, factory->verifier()->status());
            QModelIndex signatureVerified = m_fileModel->index(dest, FileItem::SignatureVerified);
            m_fileModel->setData(signatureVerified, factory->signature()->status());
            if (!factory->doDownload()) {
                QModelIndex index = m_fileModel->index(factory->dest(), FileItem::File);
                m_fileModel->setData(index, Qt::Unchecked, Qt::CheckStateRole);
            }
        }
    }

    return m_fileModel;
}

void Metalink::filesSelected()
{
    bool overwriteAll = false;
    bool autoSkip = false;
    bool cancel = false;
    QModelIndexList files = fileModel()->fileIndexes(FileItem::File);
    m_numFilesSelected = 0;

    // sets the CheckState of the fileModel to the according DataSourceFactories
    // and asks the user if there are existing files already
    foreach (const QModelIndex &index, files) {
        const QUrl dest = fileModel()->getUrl(index);
        bool doDownload = index.data(Qt::CheckStateRole).toBool();
        if (m_dataSourceFactory.contains(dest)) {
            DataSourceFactory *factory = m_dataSourceFactory[dest];
            // ignore finished transfers
            if ((factory->status() == Job::Finished) || (factory->status() == Job::FinishedKeepAlive)) {
                continue;
            }

            // check if the file at dest exists already and ask the user what to do in this case, ignore already running transfers
            if (doDownload && (factory->status() != Job::Running) && QFile::exists(dest.toLocalFile())) {
                // user has chosen to skip all files that exist already before
                if (autoSkip) {
                    fileModel()->setData(index, Qt::Unchecked, Qt::CheckStateRole);
                    doDownload = false;
                    // ask the user, unless he has chosen overwriteAll before
                } else if (!overwriteAll) {
                    KIO::RenameDialog dlg(nullptr,
                                          i18n("File already exists"),
                                          index.data().toString(),
                                          dest,
                                          KIO::RenameDialog_Options(KIO::RenameDialog_MultipleItems | KIO::RenameDialog_Overwrite | KIO::RenameDialog_Skip));
                    const int result = dlg.exec();

                    if (result == KIO::Result_Rename) {
                        // no reason to use FileModel::rename() since the file does not exist yet, so simply skip it
                        // avoids having to deal with signals
                        const QUrl newDest = dlg.newDestUrl();
                        factory->setDoDownload(doDownload);
                        factory->setNewDestination(newDest);
                        fileModel()->setData(index, newDest.fileName(), FileItem::File);
                        ++m_numFilesSelected;

                        m_dataSourceFactory.remove(dest);
                        m_dataSourceFactory[newDest] = factory;
                        continue;
                    } else if (result == KIO::Result_Skip) {
                        fileModel()->setData(index, Qt::Unchecked, Qt::CheckStateRole);
                        doDownload = false;
                    } else if (result == KIO::Result_Cancel) {
                        cancel = true;
                        break;
                    } else if (result == KIO:Result_AutoSkip:) {
                        autoSkip = true;
                        fileModel()->setData(index, Qt::Unchecked, Qt::CheckStateRole);
                        doDownload = false;
                    } else if (result == KIO::Result_OverwriteAll) {
                        overwriteAll = true;
                    }
                }
            }

            factory->setDoDownload(doDownload);
            if (doDownload && (factory->status() != Finished) && (factory->status() != FinishedKeepAlive)) {
                ++m_numFilesSelected;
            }
        }
    }

    // the user decided to cancel, so untick all files
    if (cancel) {
        m_numFilesSelected = 0;
        untickAllFiles();
        foreach (DataSourceFactory *factory, m_dataSourceFactory) {
            factory->setDoDownload(false);
        }
    }

    Transfer::ChangesFlags change = (Tc_TotalSize | Tc_DownloadSpeed);
    // some files have been selected that are not finished yet, set them to stop if the transfer is not running (checked in slotStatus)
    if (m_numFilesSelected) {
        change |= Tc_Status;
    }
    slotDataSourceFactoryChange(change);
}

void Metalink::slotRename(const QUrl &oldUrl, const QUrl &newUrl)
{
    if (!m_dataSourceFactory.contains(oldUrl)) {
        return;
    }

    m_dataSourceFactory[newUrl] = m_dataSourceFactory[oldUrl];
    m_dataSourceFactory.remove(oldUrl);
    m_dataSourceFactory[newUrl]->setNewDestination(newUrl);

    setTransferChange(Tc_FileName);
}

bool Metalink::setDirectory(const QUrl &new_directory)
{
    if (new_directory == directory()) {
        return false;
    }

    if (m_fileModel) {
        m_fileModel->setDirectory(new_directory);
    }

    const QString oldDirectory = directory().pathOrUrl(QUrl::AddTrailingSlash);
    const QString newDirectory = new_directory.pathOrUrl(QUrl::AddTrailingSlash);
    const QString fileName = m_dest.fileName();
    m_dest = new_directory;
    m_dest.addPath(fileName);

    QHash<QUrl, DataSourceFactory *> newStorage;
    foreach (DataSourceFactory *factory, m_dataSourceFactory) {
        const QUrl oldUrl = factory->dest();
        const QUrl newUrl = QUrl(oldUrl.pathOrUrl().replace(oldDirectory, newDirectory));
        factory->setNewDestination(newUrl);
        newStorage[newUrl] = factory;
    }
    m_dataSourceFactory = newStorage;

    setTransferChange(Tc_FileName);
    return true;
}

QHash<QUrl, QPair<bool, int>> Metalink::availableMirrors(const QUrl &file) const
{
    QHash<QUrl, QPair<bool, int>> urls;

    if (m_dataSourceFactory.contains(file)) {
        urls = m_dataSourceFactory[file]->mirrors();
    }

    return urls;
}

void Metalink::setAvailableMirrors(const QUrl &file, const QHash<QUrl, QPair<bool, int>> &mirrors)
{
    if (!m_dataSourceFactory.contains(file)) {
        return;
    }

    m_dataSourceFactory[file]->setMirrors(mirrors);
}

void Metalink::slotUpdateCapabilities()
{
    Capabilities oldCap = capabilities();
    Capabilities newCap = 0;
    foreach (DataSourceFactory *file, m_dataSourceFactory) {
        if (file->doDownload()) { // FIXME when a download did not start yet it should be moveable!!//FIXME why not working, when only two connections?
            if (newCap) {
                newCap &= file->capabilities();
            } else {
                newCap = file->capabilities();
            }
        }
    }

    if (newCap != oldCap) {
        setCapabilities(newCap);
    }
}

#include "moc_metalink.cpp"
