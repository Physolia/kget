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

#include "core/kget.h"
#include "core/transfergroup.h"
#include "core/download.h"
#include "core/transferdatasource.h"
#include "core/filemodel.h"
#include "core/verifier.h"
#ifdef HAVE_NEPOMUK
#include "metanepomukhandler.h"
#endif //HAVE_NEPOMUK

#include <KIconLoader>
#include <KIO/DeleteJob>
#include <KIO/NetAccess>
#include <KIO/RenameDialog>
#include <KLocale>
#include <KMessageBox>
#include <KDebug>
#include <KDialog>
#include <KStandardDirs>

#include <QtCore/QFile>
#include <QtXml/QDomElement>

Metalink::Metalink(TransferGroup * parent, TransferFactory * factory,
                         Scheduler * scheduler, const KUrl & source, const KUrl & dest,
                         const QDomElement * e)
    : Transfer(parent, factory, scheduler, source, dest, e),
      m_fileModel(0),
      m_currentFiles(0),
      m_ready(false),
      m_speedCount(0),
      m_tempAverageSpeed(0),
      m_averageSpeed(0)
#ifdef HAVE_NEPOMUK
      , m_nepHandler(0)
#endif
{
}

Metalink::~Metalink()
{
}

void Metalink::init()
{
#ifdef HAVE_NEPOMUK
    if (!m_nepHandler)
    {
        m_nepHandler = new MetaNepomukHandler(this);
        setNepomukHandler(m_nepHandler);
    }
#endif //HAVE_NEPOMUK

    Transfer::init();
}

void Metalink::start()
{
    kDebug(5001) << "metalink::start";

    if (!m_ready)
    {
        if (m_localMetalinkLocation.isValid())
        {
            metalinkInit();
            startMetalink();
        }
        else
        {
            Download *download = new Download(m_source, KStandardDirs::locateLocal("appdata", "metalinks/") + m_source.fileName());

            setStatus(Job::Stopped, i18n("Downloading Metalink File...."), SmallIcon("document-save"));
            setTransferChange(Tc_Status, true);

            connect(download, SIGNAL(finishedSuccessfully(KUrl, QByteArray)), SLOT(metalinkInit(KUrl, QByteArray)));
        }
    }
    else
    {
        startMetalink();
    }
}

void Metalink::metalinkInit(const KUrl &src, const QByteArray &data)
{
    kDebug(5001);

    bool justDownloaded = !m_localMetalinkLocation.isValid();
    if (!src.isEmpty())
    {
        m_localMetalinkLocation = src;
    }

    //use the downloaded metalink-file data directly if possible
    if (!data.isEmpty())
    {
        KGetMetalink::HandleMetalink::load(data, &m_metalink);
    }

    //TODO error message?
    //try to parse the locally stored metalink-file
    if (!m_metalink.isValid())
    {
        KGetMetalink::HandleMetalink::load(m_localMetalinkLocation.toLocalFile(), &m_metalink);
    }

    //error
    if (!m_metalink.isValid())
    {
        kError(5001) << "Unknown error when trying to load the .metalink-file. Metalink is not valid.";
        setStatus(Job::Aborted);
        setTransferChange(Tc_Status, true);
        return;
    }

    QList<KGetMetalink::File>::const_iterator it;
    QList<KGetMetalink::File>::const_iterator itEnd = m_metalink.files.files.constEnd();
    m_totalSize = 0;
    KIO::fileoffset_t segSize = 500 * 1024;//TODO use config here!
    const KUrl tempDest = KUrl(m_dest.directory());
    KUrl dest;
    for (it = m_metalink.files.files.constBegin(); it != itEnd ; ++it)
    {
        dest = tempDest;
        dest.addPath((*it).name);

        QList<KGetMetalink::Url> urlList = (*it).resources.urls;
        //sort the urls according to their priority (highest first)
        qSort(urlList.begin(), urlList.end(), qGreater<KGetMetalink::Url>());

        KIO::filesize_t fileSize = (*it).size;
        m_totalSize += fileSize;

        //create a DataSourceFactory for each separate file
        DataSourceFactory *dataFactory = new DataSourceFactory(this, dest, fileSize, segSize);
        dataFactory->setMaxMirrorsUsed(MetalinkSettings::mirrorsPerFile());

#ifdef HAVE_NEPOMUK
        m_nepHandler->setFileMetaData(dest, *it);
#endif //HAVE_NEPOMUK

//TODO compare available file size (<size>) with the sizes of the server while downloading?

        connect(dataFactory, SIGNAL(totalSize(KIO::filesize_t)), this, SLOT(totalSizeChanged(KIO::filesize_t)));
        connect(dataFactory, SIGNAL(processedSize(KIO::filesize_t)), this, SLOT(processedSizeChanged()));
        connect(dataFactory, SIGNAL(speed(ulong)), this, SLOT(speedChanged()));
        connect(dataFactory, SIGNAL(statusChanged(Job::Status)), this, SLOT(slotStatus(Job::Status)));
        connect(dataFactory->verifier(), SIGNAL(verified(bool)), this, SLOT(slotVerified(bool)));
        connect(dataFactory->signature(), SIGNAL(verified(int)), this, SLOT(slotSignatureVerified()));

        //add the DataSources
        for (int i = 0; i < urlList.size(); ++i)
        {
            const KUrl url = urlList[i].url;
            if (url.isValid())
            {
                dataFactory->addMirror(url, MetalinkSettings::connectionsPerUrl());
            }
        }
        //no datasource has been created, so remove the datasource factory
        if (dataFactory->mirrors().isEmpty())
        {
            delete dataFactory;
        }
        else
        {
            dataFactory->verifier()->model()->addChecksums((*it).verification.hashes);
            foreach (const KGetMetalink::Pieces &pieces, (*it).verification.pieces) {
                dataFactory->verifier()->addPartialChecksums(pieces.type, pieces.length, pieces.hashes);
            }

            const QHash <QString, QString> signatures = (*it).verification.signatures;
            QHash<QString, QString>::const_iterator it;
            QHash<QString, QString>::const_iterator itEnd = signatures.constEnd();
            for (it = signatures.constBegin(); it != itEnd; ++it) {
                if (it.key().toLower() == "pgp") {
                    dataFactory->signature()->setSignature(*it);
                }
            }

            m_dataSourceFactory[dataFactory->dest()] = dataFactory;
        }
    }

    if ((m_metalink.files.files.size() == 1) && m_dataSourceFactory.size())
    {
        m_dest = dest;
    }

    if (!m_dataSourceFactory.size())
    {
        KMessageBox::error(0, i18n("Download failed, no working URLs were found."), i18n("Error"));
        setStatus(Job::Aborted);
        setTransferChange(Tc_Status, true);
        return;
    }

    m_ready = !m_dataSourceFactory.isEmpty();

    //the metalink-file has just been downloaded, so ask the user to choose the files that
    // should be downloaded
    if (justDownloaded)
    {
        KDialog *dialog = new FileSelectionDlg(fileModel());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, SIGNAL(finished(int)), this, SLOT(fileDlgFinished(int)));

        dialog->show();
    }
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
    //the dialog was not accepted untick every file, this ensures that the user does not
    //press start by accident without first selecting the desired files
    if (result != QDialog::Accepted) {
        untickAllFiles();
    }

    filesSelected();

    //no files selected to download or dialog rejected, stop the download
    if (!m_numFilesSelected  || (result != QDialog::Accepted)) {
        setStatus(Job::Stopped);
        setTransferChange(Tc_Status, true);
        return;
    }

    startMetalink();
}

void Metalink::startMetalink()
{
    if (m_ready)
    {
        foreach (DataSourceFactory *factory, m_dataSourceFactory)
        {
            //specified number of files is downloaded simultanously
            if (m_currentFiles < MetalinkSettings::simultanousFiles())
            {
                const int status = factory->status();
                //only start factories that should be downloaded
                if (factory->doDownload() &&
                    (status != Job::Finished) &&
                    (status != Job::FinishedKeepAlive) &&
                    (status != Job::Running))
                {
                    ++m_currentFiles;
                    factory->start();
                }
            }
            else
            {
                break;
            }
        }

#ifdef HAVE_NEPOMUK
        m_nepHandler->setDestinations(files());
#endif //HAVE_NEPOMUK
    }
}

void Metalink::deinit()
{
    if (status() != Job::Finished)//if the transfer is not finished, we delete the written files
    {
        foreach (DataSourceFactory *factory, m_dataSourceFactory)
        {
            factory->deinit();
        }
    }//TODO: Ask the user if he/she wants to delete the *.part-file? To discuss (boom1992)
    else
    {
        //in any case delete the files that should not be downloaded
        foreach (DataSourceFactory *factory, m_dataSourceFactory)
        {
            if (!factory->doDownload())
            {
                factory->deinit();
            }
        }
    }

    if (m_localMetalinkLocation.isLocalFile())
    {
        KIO::Job *del = KIO::del(m_localMetalinkLocation, KIO::HideProgressInfo);
        KIO::NetAccess::synchronousRun(del, 0);
    }

#ifdef HAVE_NEPOMUK
    m_nepHandler->deinit();
#endif //HAVE_NEPOMUK
}

void Metalink::stop()
{
    kDebug(5001) << "metalink::Stop";
    if (m_ready && status() != Stopped)
    {
        m_currentFiles = 0;
        foreach (DataSourceFactory *factory, m_dataSourceFactory)
        {
            factory->stop();
        }
    }
}

bool Metalink::isResumable() const
{
    return true;
}

void Metalink::totalSizeChanged(KIO::filesize_t size)
{
    m_totalSize = 0;
    foreach (DataSourceFactory *factory, m_dataSourceFactory)
    {
        if (factory->doDownload())
        {
            m_totalSize += factory->size();
        }
    }

    if (m_fileModel)
    {
        DataSourceFactory *factory = qobject_cast<DataSourceFactory*>(sender());
        if (factory)
        {
            QModelIndex sizeIndex = m_fileModel->index(factory->dest(), FileItem::Size);
            m_fileModel->setData(sizeIndex, static_cast<qlonglong>(size));
        }
    }

    setTransferChange(Tc_TotalSize, true);
    processedSizeChanged();
}

void Metalink::processedSizeChanged()
{
    m_downloadedSize = 0;
    foreach (DataSourceFactory *factory, m_dataSourceFactory)
    {
        if (factory->doDownload())
        {
            m_downloadedSize += factory->downloadedSize();
        }
    }

    if (m_totalSize)
    {
        m_percent = (m_downloadedSize * 100) / m_totalSize;
    }
    else
    {
        m_percent = 0;
    }

    Transfer::ChangesFlags flags = (Tc_DownloadedSize | Tc_Percent);
    setTransferChange(flags, true);
}

void Metalink::speedChanged()
{
    m_downloadSpeed = 0;
    foreach (DataSourceFactory *factory, m_dataSourceFactory)
    {
        if (factory->doDownload())
        {
            m_downloadSpeed += factory->currentSpeed();
        }
    }

    setTransferChange(Tc_DownloadSpeed, true);

    //calculate the average of the last three speeds
    m_tempAverageSpeed += m_downloadSpeed;
    ++m_speedCount;
    if (m_speedCount == 3)
    {
        m_averageSpeed = m_tempAverageSpeed / 3;
        m_speedCount = 0;
        m_tempAverageSpeed = 0;
    }
}

int Metalink::remainingTime() const
{
    if (!m_averageSpeed)
    {
        m_averageSpeed = m_downloadSpeed;
    }
    return KIO::calculateRemainingSeconds(m_totalSize, m_downloadedSize, m_averageSpeed);
}

void Metalink::slotStatus(Job::Status status)
{
    ChangesFlags flags = Tc_Status;
    bool changeStatus = true;
    switch (status)
    {
        case Job::Aborted:
        case Job::Stopped: {
            m_currentFiles = 0;
            foreach (DataSourceFactory *factory, m_dataSourceFactory) {
                //one factory is still running, do not change the status
                if (factory->doDownload() && (factory->status() == Job::Running)) {
                    changeStatus = false;
                    ++m_currentFiles;
                }
            }

            if (changeStatus) {
                setStatus(status);
            }
            break;
        }
        case Job::Finished:
            //one file that has been downloaded now is finished//FIXME ignore downloads that were finished in the previous download!!!!
            if (m_currentFiles)
            {
                --m_currentFiles;
                startMetalink();
            }
            foreach (DataSourceFactory *factory, m_dataSourceFactory)
            {
                //one factory is not finished, do not change the status
                if (factory->doDownload() && (factory->status() != Job::Finished))
                {
                    changeStatus = false;
                    break;
                }
            }

            if (changeStatus)
            {
                setStatus(Job::Finished);
            }
            break;

        default:
            setStatus(status);
            break;
    }

    if (m_fileModel)
    {
        DataSourceFactory *factory = qobject_cast<DataSourceFactory*>(sender());
        if (factory)
        {
            QModelIndex statusIndex = m_fileModel->index(factory->dest(), FileItem::Status);
            m_fileModel->setData(statusIndex, status);
        }
    }

    if (changeStatus)
    {
        setTransferChange(flags, true);
    }
}

void Metalink::slotVerified(bool isVerified)
{
    Q_UNUSED(isVerified)

    if (status() == Job::Finished)
    {
        //see if some files are NotVerified
        QStringList brokenFiles;
        foreach (DataSourceFactory *factory, m_dataSourceFactory)
        {
            if (m_fileModel) {
                QModelIndex checksumVerified = m_fileModel->index(factory->dest(), FileItem::ChecksumVerified);
                m_fileModel->setData(checksumVerified, factory->verifier()->status());
            }
            if (factory->doDownload() && (factory->verifier()->status() == Verifier::NotVerified))
            {
                brokenFiles.append(factory->dest().pathOrUrl());
            }
        }

        if (brokenFiles.count())
        {
            if (KMessageBox::warningYesNoCancelList(0,
                i18n("The download could not be verified, do you want to repair (if repairing does not work the download would be restarted) it?"),
                     brokenFiles) == KMessageBox::Yes) {
                if (repair()) {
                    return;
                }
            }
        }
    }
}

void Metalink::slotSignatureVerified()
{
    if (status() == Job::Finished)
    {
        //see if some files are NotVerified
        QStringList brokenFiles;
        foreach (DataSourceFactory *factory, m_dataSourceFactory)
        {
            if (m_fileModel) {
                QModelIndex signatureVerified = m_fileModel->index(factory->dest(), FileItem::SignatureVerified);
                m_fileModel->setData(signatureVerified, factory->signature()->status());
            }
            if (factory->doDownload() && (factory->verifier()->status() == Verifier::NotVerified))
            {
                brokenFiles.append(factory->dest().pathOrUrl());
            }
        }
/*
        if (brokenFiles.count())//TODO
        {
            if (KMessageBox::warningYesNoCancelList(0,
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

bool Metalink::repair(const KUrl &file)
{
    if (file.isValid())
    {
        if (m_dataSourceFactory.contains(file))
        {
            DataSourceFactory *broken = m_dataSourceFactory[file];
            if (broken->verifier()->status() == Verifier::NotVerified)
            {
                broken->repair();
                return true;
            }
        }
    }
    else
    {
        QList<DataSourceFactory*> broken;
        foreach (DataSourceFactory *factory, m_dataSourceFactory)
        {
            if (factory->doDownload() && (factory->verifier()->status() == Verifier::NotVerified))
            {
                broken.append(factory);
            }
        }
        if (broken.count())
        {
            foreach (DataSourceFactory *factory, broken)
            {
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

    if (!element)
    {
        return;
    }

    const QDomElement e = *element;
    m_localMetalinkLocation = KUrl(e.attribute("LocalMetalinkLocation"));
    QDomNodeList factories = e.firstChildElement("factories").elementsByTagName("factory");

    //no stored information found, stop right here
    if (!factories.count())
    {
        return;
    }

    while (factories.count())
    {
        QDomDocument doc;
        QDomElement factory = doc.createElement("factories");
        factory.appendChild(factories.item(0).toElement());
        doc.appendChild(factory);

        DataSourceFactory *file = new DataSourceFactory(this);
        file->load(&factory);
        connect(file, SIGNAL(totalSize(KIO::filesize_t)), this, SLOT(totalSizeChanged(KIO::filesize_t)));
        connect(file, SIGNAL(processedSize(KIO::filesize_t)), this, SLOT(processedSizeChanged()));
        connect(file, SIGNAL(speed(ulong)), this, SLOT(speedChanged()));
        connect(file, SIGNAL(statusChanged(Job::Status)), this, SLOT(slotStatus(Job::Status)));
        m_dataSourceFactory[file->dest()] = file;
        connect(file->verifier(), SIGNAL(verified(bool)), this, SLOT(slotVerified(bool)));
        connect(file->signature(), SIGNAL(verified(int)), this, SLOT(slotSignatureVerified()));

        //start the DataSourceFactories that were Started when KGet was closed
        if (file->status() == Job::Running) {
            if (m_currentFiles < MetalinkSettings::simultanousFiles()) {
                ++m_currentFiles;
                file->start();
            } else {
                //enough simultanous files already, so increase the number and set file to stop --> that will decrease the number again
                file->stop();
            }
        }
    }
    m_ready = !m_dataSourceFactory.isEmpty();
}

void Metalink::save(const QDomElement &element)
{
    Transfer::save(element);

    QDomElement e = element;
    e.setAttribute("LocalMetalinkLocation", m_localMetalinkLocation.url());

    foreach (DataSourceFactory *factory, m_dataSourceFactory)
    {
        factory->save(e);
    }
}

Verifier *Metalink::verifier(const KUrl &file)
{
    if (!m_dataSourceFactory.contains(file))
    {
        return 0;
    }

    return m_dataSourceFactory[file]->verifier();
}

Signature *Metalink::signature(const KUrl &file)
{
    if (!m_dataSourceFactory.contains(file)) {
        return 0;
    }

    return m_dataSourceFactory[file]->signature();
}

QList<KUrl> Metalink::files() const
{
    return m_dataSourceFactory.keys();
}

FileModel *Metalink::fileModel()
{
    if (!m_fileModel)
    {
        m_fileModel = new FileModel(files(), directory(), this);
        connect(m_fileModel, SIGNAL(rename(KUrl,KUrl)), this, SLOT(slotRename(KUrl,KUrl)));
        connect(m_fileModel, SIGNAL(checkStateChanged()), this, SLOT(filesSelected()));

        foreach (DataSourceFactory *factory, m_dataSourceFactory)
        {
            const KUrl dest = factory->dest();
            QModelIndex size = m_fileModel->index(dest, FileItem::Size);
            m_fileModel->setData(size, static_cast<qlonglong>(factory->size()));
            QModelIndex status = m_fileModel->index(dest, FileItem::Status);
            m_fileModel->setData(status, factory->status());
            QModelIndex checksumVerified = m_fileModel->index(dest, FileItem::ChecksumVerified);
            m_fileModel->setData(checksumVerified, factory->verifier()->status());
            QModelIndex signatureVerified = m_fileModel->index(dest, FileItem::SignatureVerified);
            m_fileModel->setData(signatureVerified, factory->signature()->status());
            if (!factory->doDownload())
            {
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

    //sets the CheckState of the fileModel to the according DataSourceFactories
    //and asks the user if there are existing files already
    foreach (const QModelIndex &index, files)
    {
        const KUrl dest = fileModel()->getUrl(index);
        bool doDownload = index.data(Qt::CheckStateRole).toBool();
        if (m_dataSourceFactory.contains(dest))
        {
            DataSourceFactory *factory = m_dataSourceFactory[dest];

            //check if the file at dest exists already and ask the user what to do in this case
            if (doDownload && QFile::exists(dest.toLocalFile())) {
                //usere has chosen to skip all files that exist already before
                if (autoSkip) {
                    fileModel()->setData(index, Qt::Unchecked, Qt::CheckStateRole);
                    doDownload = false;
                //ask the user, unless he has choosen overwriteAll before
                } else if (!overwriteAll) {
                    KIO::RenameDialog dlg(0, i18n("File already exists"), index.data().toString(), dest, KIO::RenameDialog_Mode(KIO::M_MULTI | KIO::M_OVERWRITE | KIO::M_SKIP));
                    const int result = dlg.exec();

                    if (result == KIO::R_RENAME) {
                        //no reason to use FileModel::rename() since the file does not exist yet, so simply skip it
                        //avoids having to deal with signals
                        const KUrl newDest = dlg.newDestUrl();
                        factory->setDoDownload(doDownload);
                        factory->setNewDestination(newDest);
                        fileModel()->setData(index, newDest.fileName(), FileItem::File);
                        ++m_numFilesSelected;

                        m_dataSourceFactory.remove(dest);
                        m_dataSourceFactory[newDest] = factory;
                        continue;
                    } else if (result == KIO::R_SKIP) {
                        fileModel()->setData(index, Qt::Unchecked, Qt::CheckStateRole);
                        doDownload = false;
                    } else if (result == KIO::R_CANCEL) {
                        cancel = true;
                        break;
                    } else if (result == KIO::R_AUTO_SKIP) {
                        autoSkip = true;
                        fileModel()->setData(index, Qt::Unchecked, Qt::CheckStateRole);
                        doDownload = false;
                    } else if (result == KIO::R_OVERWRITE_ALL) {
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

    //the user decided to cancel, so untick all files
    if (cancel) {
        m_numFilesSelected = 0;
        untickAllFiles();
        foreach (DataSourceFactory *factory, m_dataSourceFactory) {
            factory->setDoDownload(false);
        }
    }

    //make sure that the size, the downloaded size and the speed gets updated
    totalSizeChanged(0);
    speedChanged();
}

void Metalink::slotRename(const KUrl &oldUrl, const KUrl &newUrl)
{
    if (!m_dataSourceFactory.contains(oldUrl))
    {
        return;
    }

    m_dataSourceFactory[newUrl] = m_dataSourceFactory[oldUrl];
    m_dataSourceFactory.remove(oldUrl);
    m_dataSourceFactory[newUrl]->setNewDestination(newUrl);

#ifdef HAVE_NEPOMUK
    m_nepHandler->setDestinations(files());
#endif //HAVE_NEPOMUK

    setTransferChange(Tc_FileName);
}

bool Metalink::setDirectory(const KUrl &new_directory)
{
    if (new_directory == directory())
    {
        return false;
    }

    if (m_fileModel)
    {
        m_fileModel->setDirectory(new_directory);
    }

    const QString oldDirectory = directory().pathOrUrl(KUrl::AddTrailingSlash);
    const QString newDirectory = new_directory.pathOrUrl(KUrl::AddTrailingSlash);
    const QString fileName = m_dest.fileName();
    m_dest = new_directory;
    m_dest.addPath(fileName);

    QHash<KUrl, DataSourceFactory*> newStorage;
    foreach (DataSourceFactory *factory, m_dataSourceFactory)
    {
        const KUrl oldUrl = factory->dest();
        const KUrl newUrl = KUrl(oldUrl.pathOrUrl().replace(oldDirectory, newDirectory));
        factory->setNewDestination(newUrl);
        newStorage[newUrl] = factory;
    }
    m_dataSourceFactory = newStorage;

#ifdef HAVE_NEPOMUK
    m_nepHandler->setDestinations(files());
#endif //HAVE_NEPOMUK

    setTransferChange(Tc_FileName);

    return true;
}

QHash<KUrl, QPair<bool, int> > Metalink::availableMirrors(const KUrl &file) const
{
    QHash<KUrl, QPair<bool, int> > urls;

    if (m_dataSourceFactory.contains(file))
    {
        urls = m_dataSourceFactory[file]->mirrors();
    }

    return urls;
}


void Metalink::setAvailableMirrors(const KUrl &file, const QHash<KUrl, QPair<bool, int> > &mirrors)
{
    if (!m_dataSourceFactory.contains(file))
    {
        return;
    }

    m_dataSourceFactory[file]->setMirrors(mirrors);
}

#include "metalink.moc"
