/* This file is part of the KDE project

   Copyright (C) 2006 Manolo Valdes <nolis71cu@gmail.com>
   Copyright (C) 2009 Matthias Fuchs <mat69@gmx.net>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
*/

#include "segment.h"
#include "multisegkiosettings.h"

#include <cmath>

#include "kget_debug.h"
#include <KLocalizedString>
#include <QDebug>

#include <QTimer>

Segment::Segment(const QUrl &src, const QPair<KIO::fileoffset_t, KIO::fileoffset_t> &segmentSize, const QPair<int, int> &segmentRange, QObject *parent)
    : QObject(parent)
    , m_findFilesize((segmentRange.first == -1) && (segmentRange.second == -1))
    , m_canResume(true)
    , m_status(Stopped)
    , m_currentSegment(segmentRange.first)
    , m_endSegment(segmentRange.second)
    , m_errorCount(0)
    , m_offset(segmentSize.first * segmentRange.first)
    , m_currentSegSize(segmentSize.first)
    , m_bytesWritten(0)
    , m_getJob(nullptr)
    , m_url(src)
    , m_segSize(segmentSize)
{
    // last segment
    if (m_endSegment - m_currentSegment == 0) {
        m_currentSegSize = m_segSize.second;
    }

    if (m_findFilesize) {
        m_offset = 0;
        m_currentSegSize = 0;
        m_currentSegment = 0;
        m_endSegment = 0;
        m_totalBytesLeft = 0;
    } else {
        m_totalBytesLeft = m_segSize.first * (m_endSegment - m_currentSegment) + m_segSize.second;
    }
}

Segment::~Segment()
{
    if (m_getJob) {
        qCDebug(KGET_DEBUG) << "Closing transfer ...";
        m_getJob->kill(KJob::Quietly);
    }
}

bool Segment::findingFileSize() const
{
    return m_findFilesize;
}

bool Segment::createTransfer()
{
    qCDebug(KGET_DEBUG) << " -- " << m_url;
    if (m_getJob)
        return false;

    m_getJob = KIO::get(m_url, KIO::Reload, KIO::HideProgressInfo);
    m_getJob->suspend();
    m_getJob->addMetaData("errorPage", "false");
    m_getJob->addMetaData("AllowCompressedPage", "false");
    if (m_offset) {
        m_canResume = false; // FIXME set m_canResume to false by default!!
        m_getJob->addMetaData("resume", KIO::number(m_offset));
        connect(m_getJob, &KIO::TransferJob::canResume, this, &Segment::slotCanResume);
    }
#if 0 // TODO: we disable that code till it's implemented in kdelibs, also we need to think, which settings we should use
    if (Settings::speedLimit())
    {
        m_getJob->addMetaData( "speed-limit", KIO::number(Settings::transferSpeedLimit() * 1024) );
    }
#endif
    connect(m_getJob, &KJob::totalSize, this, &Segment::slotTotalSize);
    connect(m_getJob, &KIO::TransferJob::data, this, &Segment::slotData);
    connect(m_getJob, &KJob::result, this, &Segment::slotResult);
    connect(m_getJob, &KIO::TransferJob::redirection, this, &Segment::slotRedirection);
    return true;
}

void Segment::slotRedirection(KIO::Job *, const QUrl &url)
{
    m_url = url;
    Q_EMIT urlChanged(url);
}

void Segment::slotCanResume(KIO::Job *job, KIO::filesize_t offset)
{
    Q_UNUSED(job)
    Q_UNUSED(offset)
    qCDebug(KGET_DEBUG);
    m_canResume = true;
    Q_EMIT canResume();
}

void Segment::slotTotalSize(KJob *job, qulonglong size)
{
    Q_UNUSED(job)
    qCDebug(KGET_DEBUG) << "Size found for" << m_url;

    if (m_findFilesize) {
        int numSegments = size / m_segSize.first;
        KIO::fileoffset_t rest = size % m_segSize.first;
        if (rest) {
            ++numSegments;
            m_segSize.second = rest;
        }

        m_endSegment = numSegments - 1;

        m_currentSegment = 0;
        m_currentSegSize = (numSegments == 1 ? m_segSize.second : m_segSize.first);
        m_totalBytesLeft = size;

        Q_EMIT totalSize(size, qMakePair(m_currentSegment, m_endSegment));
        m_findFilesize = false;
    } else {
        Q_EMIT totalSize(size, qMakePair(-1, -1));
    }
}

bool Segment::startTransfer()
{
    qCDebug(KGET_DEBUG) << m_url;
    if (!m_getJob) {
        createTransfer();
    }
    if (m_getJob && (m_status != Running)) {
        setStatus(Running, false);
        m_getJob->resume();
        return true;
    }
    return false;
}

bool Segment::stopTransfer()
{
    qCDebug(KGET_DEBUG);

    setStatus(Stopped, false);
    if (m_getJob) {
        if (m_getJob) {
            m_getJob->kill(KJob::EmitResult);
        }
        return true;
    }
    return false;
}

void Segment::slotResult(KJob *job)
{
    qCDebug(KGET_DEBUG) << "Job:" << job << m_url << "error:" << job->error();

    m_getJob = nullptr;

    // clear the buffer as the download might be moved around
    if (m_status == Stopped) {
        m_buffer.clear();
    }
    if (!m_buffer.isEmpty()) {
        if (m_findFilesize && !job->error()) {
            qCDebug(KGET_DEBUG) << "Looping until write the buffer ..." << m_url;
            slotWriteRest();
            return;
        }
    }
    if (!m_totalBytesLeft && !m_findFilesize) {
        setStatus(Finished);
        return;
    }
    if (m_status == Killed) {
        return;
    }
    if (job->error() && (m_status == Running)) {
        Q_EMIT error(this, job->errorString(), Transfer::Log_Error);
    }
}

void Segment::slotData(KIO::Job *, const QByteArray &_data)
{
    // Check if the transfer allows resuming...
    if (m_offset && !m_canResume) {
        qCDebug(KGET_DEBUG) << m_url << "does not allow resuming.";
        stopTransfer();
        setStatus(Killed, false);
        const QString errorText = KIO::buildErrorString(KIO::ERR_CANNOT_RESUME, m_url.toString());
        Q_EMIT error(this, errorText, Transfer::Log_Warning);
        return;
    }

    m_buffer.append(_data);
    if (!m_findFilesize && m_totalBytesLeft && static_cast<uint>(m_buffer.size()) >= m_totalBytesLeft) {
        qCDebug(KGET_DEBUG) << "Segment::slotData() buffer full. Stopping transfer..."; // TODO really stop it? is this even needed?
        if (m_getJob) {
            m_getJob->kill(KJob::Quietly);
            m_getJob = nullptr;
        }
        m_buffer.truncate(m_totalBytesLeft);
        slotWriteRest();
    } else {
        /*
         write to the local file only if the buffer has more than 100kbytes
         this hack try to avoid too much cpu usage. it seems to be due KIO::Filejob
         so remove it when it works property
        */
        if (m_buffer.size() > MultiSegKioSettings::saveSegSize() * 1024)
            writeBuffer();
    }
}

bool Segment::writeBuffer()
{
    qCDebug(KGET_DEBUG) << "Segment::writeBuffer() sending:" << m_buffer.size() << "from job:" << m_getJob;
    if (m_buffer.isEmpty()) {
        return false;
    }

    bool worked = false;
    Q_EMIT data(m_offset, m_buffer, worked);

    if (worked) {
        m_currentSegSize -= m_buffer.size();
        if (!m_findFilesize) {
            m_totalBytesLeft -= m_buffer.size();
        }
        m_offset += m_buffer.size();
        m_bytesWritten += m_buffer.size();
        m_buffer.clear();
        qCDebug(KGET_DEBUG) << "Segment::writeBuffer() updating segment record of job:" << m_getJob << "--" << m_totalBytesLeft << "bytes left";
    }

    // finding filesize, so no segments defined yet
    if (m_findFilesize) {
        return worked;
    }

    // check which segments have been finished
    bool finished = false;
    // m_currentSegSize being smaller than 1 means that at least one segment has been finished
    while (m_currentSegSize <= 0 && !finished) {
        finished = (m_currentSegment == m_endSegment);
        Q_EMIT finishedSegment(this, m_currentSegment, finished);

        if (!finished) {
            ++m_currentSegment;
            m_currentSegSize += (m_currentSegment == m_endSegment ? m_segSize.second : m_segSize.first);
        }
    }

    return worked;
}

void Segment::slotWriteRest()
{
    if (m_buffer.isEmpty()) {
        return;
    }
    qCDebug(KGET_DEBUG) << this;

    if (writeBuffer()) {
        m_errorCount = 0;
        if (m_findFilesize) {
            Q_EMIT finishedDownload(m_bytesWritten);
        }
        return;
    }

    if (++m_errorCount >= 100) {
        qWarning() << "Failed to write to the file:" << m_url << this;
        Q_EMIT error(this, i18n("Failed to write to the file."), Transfer::Log_Error);
    } else {
        qCDebug(KGET_DEBUG) << "Wait 50 msec:" << this;
        QTimer::singleShot(50, this, &Segment::slotWriteRest);
    }
}

void Segment::setStatus(Status stat, bool doEmit)
{
    m_status = stat;
    if (doEmit)
        Q_EMIT statusChanged(this);
}

QPair<int, int> Segment::assignedSegments() const
{
    return QPair<int, int>(m_currentSegment, m_endSegment);
}

QPair<KIO::fileoffset_t, KIO::fileoffset_t> Segment::segmentSize() const
{
    return m_segSize;
}

int Segment::countUnfinishedSegments() const
{
    return m_endSegment - m_currentSegment;
}

QPair<int, int> Segment::split()
{
    if (m_getJob) {
        m_getJob->suspend();
    }

    QPair<int, int> freed = QPair<int, int>(-1, -1);
    const int free = std::ceil((countUnfinishedSegments() + 1) / static_cast<double>(2));

    if (!free) {
        qCDebug(KGET_DEBUG) << "None freed, start:" << m_currentSegment << "end:" << m_endSegment;

        if (m_getJob) {
            m_getJob->resume();
        }
        return freed;
    }

    const int newEnd = m_endSegment - free;
    freed = QPair<int, int>(newEnd + 1, m_endSegment);
    qCDebug(KGET_DEBUG) << "Start:" << m_currentSegment << "old end:" << m_endSegment << "new end:" << newEnd << "freed:" << freed;
    m_endSegment = newEnd;
    m_totalBytesLeft -= m_segSize.first * (free - 1) + m_segSize.second;

    // end changed, so in any case the lastSegSize should be the normal segSize
    if (free) {
        m_segSize.second = m_segSize.first;
    }

    if (m_getJob) {
        m_getJob->resume();
    }
    return freed;
}

bool Segment::merge(const QPair<KIO::fileoffset_t, KIO::fileoffset_t> &segmentSize, const QPair<int, int> &segmentRange)
{
    if (m_endSegment + 1 == segmentRange.first) {
        m_endSegment = segmentRange.second;
        m_segSize.second = segmentSize.second;
        m_totalBytesLeft += segmentSize.first * (m_endSegment - segmentRange.first) + m_segSize.second;
        return true;
    }

    return false;
}

#include "moc_segment.cpp"
