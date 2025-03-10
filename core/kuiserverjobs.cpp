/* This file is part of the KDE project

   Copyright (C) 2007 by Javier Goday <jgoday@gmail.com>
   Copyright (C) 2009 by Dario Massarin <nekkar@libero.it>
   Copyright (C) 2010 by Matthias Fuchs <mat69@gmx.net>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
*/

#include "kuiserverjobs.h"

#include "kget.h"
#include "kgetkjobadapter.h"
#include "settings.h"
#include "transferhandler.h"

#include "kget_debug.h"
#include <KUiServerJobTracker>
#include <QDebug>

KUiServerJobs::KUiServerJobs(QObject *parent)
    : QObject(parent)
    , m_globalJob(nullptr)
{
}

KUiServerJobs::~KUiServerJobs()
{
    while (m_registeredJobs.size()) {
        unregisterJob(m_registeredJobs.begin().value(), m_registeredJobs.begin().key());
    }

    delete m_globalJob;
}

void KUiServerJobs::settingsChanged()
{
    QList<TransferHandler *> transfers = KGet::allTransfers();

    foreach (TransferHandler *transfer, transfers) {
        if (shouldBeShown(transfer))
            registerJob(transfer->kJobAdapter(), transfer);
        else
            unregisterJob(transfer->kJobAdapter(), transfer);
    }

    // GlobalJob is associated to a virtual transfer pointer of value == nullptr
    if (shouldBeShown(nullptr))
        registerJob(globalJob(), nullptr);
    else
        unregisterJob(globalJob(), nullptr);
}

void KUiServerJobs::slotTransfersAdded(QList<TransferHandler *> transfers)
{
    qCDebug(KGET_DEBUG);

    foreach (TransferHandler *transfer, transfers) {
        if (shouldBeShown(transfer))
            registerJob(transfer->kJobAdapter(), transfer);

        if (shouldBeShown(nullptr)) {
            globalJob()->update();
            registerJob(globalJob(), nullptr);
        } else
            unregisterJob(globalJob(), nullptr);
    }
}

void KUiServerJobs::slotTransfersAboutToBeRemoved(const QList<TransferHandler *> &transfers)
{
    qCDebug(KGET_DEBUG);

    m_invalidTransfers << transfers;
    foreach (TransferHandler *transfer, transfers) {
        unregisterJob(transfer->kJobAdapter(), transfer);

        if (shouldBeShown(nullptr)) {
            globalJob()->update();
            registerJob(globalJob(), nullptr);
        } else {
            unregisterJob(globalJob(), nullptr);
        }
    }
}

void KUiServerJobs::slotTransfersChanged(QMap<TransferHandler *, Transfer::ChangesFlags> transfers)
{
    qCDebug(KGET_DEBUG);

    if (!Settings::enableKUIServerIntegration())
        return;

    QMapIterator<TransferHandler *, Transfer::ChangesFlags> i(transfers);
    while (i.hasNext()) {
        i.next();
        //         if(!m_invalidTransfers.contains(i.key()))
        {
            TransferHandler *transfer = i.key();
            if (shouldBeShown(transfer)) {
                registerJob(transfer->kJobAdapter(), transfer);
            } else {
                unregisterJob(transfer->kJobAdapter(), transfer);
            }
        }
    }

    if (shouldBeShown(nullptr)) {
        globalJob()->update();
        registerJob(globalJob(), nullptr);
    } else
        unregisterJob(globalJob(), nullptr);
}

void KUiServerJobs::registerJob(KGetKJobAdapter *job, TransferHandler *transfer)
{
    if (m_registeredJobs.contains(transfer) || !job) {
        return;
    }
    connect(job, &KGetKJobAdapter::requestStop, this, &KUiServerJobs::slotRequestStop);
    connect(job, &KGetKJobAdapter::requestSuspend, this, &KUiServerJobs::slotRequestSuspend);
    connect(job, &KGetKJobAdapter::requestResume, this, &KUiServerJobs::slotRequestResume);

    KJob *j = job;
    registerJob(j, transfer);
}

void KUiServerJobs::registerJob(KJob *job, TransferHandler *transfer)
{
    if (m_registeredJobs.contains(transfer) || !job)
        return;

    KIO::getJobTracker()->registerJob(job);
    m_registeredJobs[transfer] = job;
}

bool KUiServerJobs::unregisterJob(KJob *job, TransferHandler *transfer)
{
    if (!m_registeredJobs.contains(transfer) || !job)
        return false;

    // Transfer should only be suspended, thus still show the job tracker
    if (m_suspendRequested.contains(transfer)) {
        m_suspendRequested.removeAll(transfer);
        return false;
    }

    // unregister the job if it was a single adaptor
    if (job != m_globalJob) {
        disconnect(job);
    }
    KIO::getJobTracker()->unregisterJob(m_registeredJobs[transfer]);
    m_registeredJobs.remove(transfer);

    return true;
}

void KUiServerJobs::slotRequestStop(KJob *job, TransferHandler *transfer)
{
    if (unregisterJob(job, transfer)) {
        if (transfer) {
            transfer->stop();
        } else {
            foreach (TransferHandler *t, KGet::allTransfers()) {
                t->stop();
            }
        }
    }
}

bool KUiServerJobs::shouldBeShown(TransferHandler *transfer)
{
    if (!Settings::enableKUIServerIntegration())
        return false;

    if (Settings::exportGlobalJob() && (transfer == nullptr) && existRunningTransfers())
        return true;

    if (!Settings::exportGlobalJob() && (transfer) && (transfer->status() == Job::Running))
        return true;

    return false;
}

bool KUiServerJobs::existRunningTransfers()
{
    foreach (TransferHandler *transfer, KGet::allTransfers()) {
        // if added to m_invalidTransfers it means that the job is about to be removed
        if ((transfer->status() == Job::Running) && !m_invalidTransfers.contains(transfer)) {
            return true;
        }
    }

    return false;
}

KGetGlobalJob *KUiServerJobs::globalJob()
{
    if (!m_globalJob) {
        m_globalJob = new KGetGlobalJob();
        connect(m_globalJob, &KGetGlobalJob::requestStop, this, &KUiServerJobs::slotRequestStop);
    }
    return m_globalJob;
}

void KUiServerJobs::slotRequestSuspend(KJob *job, TransferHandler *transfer)
{
    Q_UNUSED(job)
    if (transfer) {
        m_suspendRequested << transfer;
        transfer->stop();
    }
}

void KUiServerJobs::slotRequestResume(KJob *job, TransferHandler *transfer)
{
    Q_UNUSED(job)
    if (transfer) {
        transfer->start();
    }
}

#include "moc_kuiserverjobs.cpp"
