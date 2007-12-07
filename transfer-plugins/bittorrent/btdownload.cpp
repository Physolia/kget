/* This file is part of the KDE project

   Copyright (C) 2007 Lukas Appelhans <l.appelhans@gmx.de>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
*/
#include "btdownload.h"

#include <QFile>
#include <QFileInfo>

#include <KDebug>
#include <KStandardDirs>

BTDownload::BTDownload(const KUrl &srcUrl)
  : m_srcUrl(srcUrl),
    m_destUrl(KStandardDirs::locateLocal("appdata", "tmp/"))
{
        kDebug(5001) << "DownloadFile:" << m_srcUrl.url();
        KIO::TransferJob *m_copyJob = KIO::get(m_srcUrl, KIO::NoReload, KIO::HideProgressInfo);
        connect(m_copyJob, SIGNAL(data(KIO::Job*,const QByteArray &)), SLOT(slotData(KIO::Job*, const QByteArray&)));
        connect(m_copyJob, SIGNAL(result(KJob *)), SLOT(slotResult(KJob *)));
        connect(m_copyJob, SIGNAL(finished()), SLOT(setTorrentFileDownloaded()));
}

void BTDownload::slotData(KIO::Job *job, const QByteArray& data)
{
    kDebug(5001);
    /**if (data.size() == 0)
    {
        slotResult(job);
        return;
    }**/
    m_data.append(data);
}

void BTDownload::slotResult(KJob * job)
{
    kDebug(5001);
    switch (job->error())
    {
        case 0://The download has finished
        {
            kDebug(5001) << "Downloading successfully finished" << m_destUrl.url().remove("file://") + m_srcUrl.fileName();
            QFile torrentFile(m_destUrl.url().remove("file://") + m_srcUrl.fileName());
            if (!torrentFile.open(QIODevice::WriteOnly | QIODevice::Text))
                kDebug(5001) << "Thanks uwolfer";
            torrentFile.write(m_data);
            torrentFile.close();
            kDebug(5001) << m_data;
            m_data = 0;
            m_destUrl = QFileInfo(torrentFile).absoluteFilePath();
            kDebug(5001) << m_destUrl;
            emit finishedSuccessfully(m_destUrl);
            break;
        }
        case KIO::ERR_FILE_ALREADY_EXIST:
            kDebug(5001) << "ERROR - File already exists";
            m_data = 0;
            emit finishedSuccessfully(m_destUrl);
        default:
            kDebug(5001) << "That sucks";
            m_data = 0;
            emit finishedWithError();
            break;
    }
}
  
