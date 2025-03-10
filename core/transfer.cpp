/* This file is part of the KDE project

   Copyright (C) 2004 Dario Massarin <nekkar@libero.it>
   Copyright (C) 2008 - 2011 Lukas Appelhans <l.appelhans@gmx.de>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
*/

#include "core/transfer.h"

#include "settings.h"

#include "core/plugin/transferfactory.h"
#include "core/scheduler.h"
#include "core/transferhandler.h"

#include <KLazyLocalizedString>
#include <KLocalizedString>
#include <QDomElement>

struct StatusStrings {
    KLazyLocalizedString name;
};

const StatusStrings STATUSTEXTS[] = {{kli18n("Downloading....")},
                                     {kli18nc("transfer state: delayed", "Delayed")},
                                     {kli18nc("transfer state: stopped", "Stopped")},
                                     {kli18nc("transfer state: aborted", "Aborted")},
                                     {kli18nc("transfer state: finished", "Finished")},
                                     {KLazyLocalizedString()}, // TODO: Add FinishedKeepAlive status
                                     {kli18nc("changing the destination of the file", "Changing destination")}};
const QStringList STATUSICONS = QStringList() << "media-playback-start"
                                              << "view-history"
                                              << "process-stop"
                                              << "dialog-error"
                                              << "dialog-ok"
                                              << "media-playback-start"
                                              << "media-playback-pause";

Transfer::Transfer(TransferGroup *parent, TransferFactory *factory, Scheduler *scheduler, const QUrl &source, const QUrl &dest, const QDomElement *e)
    : Job(scheduler, parent)
    , m_source(source)
    , m_dest(dest)
    , m_totalSize(0)
    , m_downloadedSize(0)
    , m_uploadedSize(0)
    , m_percent(0)
    , m_downloadSpeed(0)
    , m_uploadSpeed(0)
    , m_uploadLimit(0)
    , m_downloadLimit(0)
    , m_isSelected(false)
    , m_capabilities()
    , m_visibleUploadLimit(0)
    , m_visibleDownloadLimit(0)
    , m_runningSeconds(0)
    , m_ratio(0)
    , m_handler(nullptr)
    , m_factory(factory)
{
    Q_UNUSED(e)
}

Transfer::~Transfer()
{
}

void Transfer::setCapabilities(Capabilities capabilities)
{
    if (m_capabilities != capabilities) {
        m_capabilities = capabilities;
        Q_EMIT capabilitiesChanged();
    }
}

void Transfer::create()
{
    init();
}

void Transfer::destroy(DeleteOptions options)
{
    deinit(options);
}

void Transfer::init() // TODO think about e, maybe not have it at all in the constructor?
{
}

bool Transfer::setDirectory(const QUrl &newDirectory)
{
    Q_UNUSED(newDirectory)

    // the standard implementation always returns false
    return false;
}

int Transfer::elapsedTime() const
{
    if (status() == Job::Running)
        return m_runningTime.elapsed() / 1000;

    return m_runningSeconds;
}

int Transfer::averageDownloadSpeed() const
{
    const int runningSeconds = elapsedTime();
    if (runningSeconds) {
        return m_totalSize / runningSeconds;
    }

    return 0;
}

QHash<QUrl, QPair<bool, int>> Transfer::availableMirrors(const QUrl &file) const
{
    Q_UNUSED(file)

    QHash<QUrl, QPair<bool, int>> available;
    available[m_source] = QPair<bool, int>(true, 1);
    return available;
}

void Transfer::setUploadLimit(int ulLimit, SpeedLimit limit)
{
    if (limit == Transfer::VisibleSpeedLimit) {
        m_visibleUploadLimit = ulLimit;
        if (ulLimit < m_uploadLimit || m_uploadLimit == 0)
            m_uploadLimit = ulLimit;
    } else {
        m_uploadLimit = ulLimit;
    }

    setSpeedLimits(m_uploadLimit, m_downloadLimit);
}

void Transfer::setDownloadLimit(int dlLimit, SpeedLimit limit)
{
    if (limit == Transfer::VisibleSpeedLimit) {
        m_visibleDownloadLimit = dlLimit;
        if (dlLimit < m_downloadLimit || m_downloadLimit == 0)
            m_downloadLimit = dlLimit;
    } else {
        m_downloadLimit = dlLimit;
    }

    setSpeedLimits(m_uploadLimit, m_downloadLimit);
}

int Transfer::uploadLimit(SpeedLimit limit) const
{
    if (limit == Transfer::VisibleSpeedLimit)
        return m_visibleUploadLimit;

    return m_uploadLimit;
}

int Transfer::downloadLimit(SpeedLimit limit) const
{
    if (limit == Transfer::VisibleSpeedLimit)
        return m_visibleDownloadLimit;

    return m_downloadLimit;
}

void Transfer::setMaximumShareRatio(double ratio)
{
    m_ratio = ratio;
    checkShareRatio();
}

void Transfer::checkShareRatio()
{
    if (m_downloadedSize == 0 || m_ratio == 0)
        return;

    if ((double)m_uploadedSize / m_downloadedSize >= m_ratio)
        setDownloadLimit(1, Transfer::InvisibleSpeedLimit); // If we set it to 0 we would have no limit xD
    else
        setDownloadLimit(0, Transfer::InvisibleSpeedLimit);
}

void Transfer::setLog(const QString &message, Transfer::LogLevel level)
{
    QString msg("<font color=\"blue\">" + QTime::currentTime().toString() + "</font> : ");
    if (level == Log_Error) {
        msg += "<font color=\"red\">" + message + "</font>";
    }
    if (level == Log_Warning) {
        msg += "<font color=\"yellow\">" + message + "</font>";
    } else {
        msg += message;
    }
    m_log << msg;
}

TransferHandler *Transfer::handler()
{
    if (!m_handler)
        m_handler = m_factory->createTransferHandler(this, scheduler());

    return m_handler;
}

TransferTreeModel *Transfer::model()
{
    return group()->model();
}

void Transfer::save(const QDomElement &element)
{
    QDomElement e = element;
    e.setAttribute("Source", m_source.url());
    e.setAttribute("Dest", m_dest.url());
    e.setAttribute("TotalSize", m_totalSize);
    e.setAttribute("DownloadedSize", m_downloadedSize);
    e.setAttribute("UploadedSize", m_uploadedSize);
    e.setAttribute("DownloadLimit", m_visibleDownloadLimit);
    e.setAttribute("UploadLimit", m_visibleUploadLimit);
    e.setAttribute("ElapsedTime", status() == Job::Running ? m_runningTime.elapsed() / 1000 : m_runningSeconds);
    e.setAttribute("Policy", policy() == Job::Start ? "Start" : (policy() == Job::Stop ? "Stop" : "None"));
}

void Transfer::load(const QDomElement *element)
{
    if (!element) {
        setStatus(status(), i18nc("transfer state: stopped", "Stopped"), "process-stop");
        setStartStatus(status());
        return;
    }

    const QDomElement e = *element;

    m_source = QUrl(e.attribute("Source"));
    m_dest = QUrl(e.attribute("Dest"));

    m_totalSize = e.attribute("TotalSize").toULongLong();
    m_downloadedSize = e.attribute("DownloadedSize").toULongLong();
    m_uploadedSize = e.attribute("UploadedSize").toULongLong();
    m_percent = (m_totalSize ? ((100.0 * m_downloadedSize) / m_totalSize) : 0);

    if ((m_totalSize == m_downloadedSize) && (m_totalSize != 0)) {
        setStartStatus(Job::Finished);
        setStatus(startStatus());
    } else {
        setStatus(status(), i18nc("transfer state: stopped", "Stopped"), "process-stop");
        setStartStatus(status());
    }
    setUploadLimit(e.attribute("UploadLimit").toInt(), Transfer::VisibleSpeedLimit);
    setDownloadLimit(e.attribute("DownloadLimit").toInt(), Transfer::VisibleSpeedLimit);
    m_runningSeconds = e.attribute("ElapsedTime").toInt();
    if (Settings::startupAction() == 1) {
        setPolicy(Job::Start);
    } else if (Settings::startupAction() == 2) {
        setPolicy(Job::Stop);
    } else {
        if (e.attribute("Policy") == "Start")
            setPolicy(Job::Start);
        else if (e.attribute("Policy") == "Stop")
            setPolicy(Job::Stop);
        else
            setPolicy(Job::None);
    }
}

void Transfer::setStatus(Job::Status jobStatus, const QString &text, const QString &pix)
{
    const bool statusChanged = (status() != jobStatus);
    QString statusText = text;
    if (statusText.isEmpty()) {
        statusText = KLocalizedString(STATUSTEXTS[jobStatus].name).toString();
    }

    // always prefer pix, if it is set
    if (!pix.isNull()) {
        m_statusIconName = pix;
    } else if (statusChanged || m_statusIconName.isNull()) {
        m_statusIconName = STATUSICONS[jobStatus];
    }

    m_statusText = statusText;

    if (jobStatus == Job::Running && status() != Job::Running) {
        m_runningTime.restart();
        m_runningTime.addSecs(m_runningSeconds);
    }
    if (jobStatus != Job::Running && status() == Job::Running)
        m_runningSeconds = m_runningTime.elapsed() / 1000;
    /**
     * It's important to call job::setStatus AFTER having changed the
     * icon or the text or whatever.
     * This because this function also notifies about this change
     * the scheduler which could also decide to change it another time
     * as well. For example if a job status is set to Aborted, the scheduler
     * could mark it to Delayed. This could trigger another icon or text
     * change which would be the right one since the status of the Job
     * has changed. If we set the icon or text after calling setStatus(),
     * we can overwrite the last icon or text change.
     */
    Job::setStatus(jobStatus);
}

void Transfer::setTransferChange(ChangesFlags change, bool postEvent)
{
    if (change & Tc_DownloadedSize || change & Tc_Status) {
        change = change | Tc_RemainingTime;
    }
    handler()->setTransferChange(change, postEvent);
}

QString Transfer::statusText(Job::Status status)
{
    return STATUSTEXTS[status].name.toString();
}

QString Transfer::statusIconName(Job::Status status)
{
    return STATUSICONS[status];
}

#include "moc_transfer.cpp"
