/**************************************************************************
 *   Copyright (C) 2009-2011 Matthias Fuchs <mat69@gmx.net>                *
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

#include "keydownloader.h"
#include "settings.h"
#include "signature_p.h"

#include "kget_debug.h"
#include <KIO/Job>
#include <KLocalizedString>
#include <KMessageBox>
#include <QDebug>

#ifdef HAVE_QGPGME
#include <gpgme++/context.h>
#include <gpgme++/data.h>
#include <gpgme++/importresult.h>
#include <qgpgme/dataprovider.h>
#endif

KeyDownloader::KeyDownloader(QObject *parent)
    : QObject(parent)
{
}

bool KeyDownloader::isValid() const
{
#ifdef HAVE_QGPGME
    return true;
#else // HAVE_QGPGME
    return false;
#endif // HAVE_QGPGME
}

void KeyDownloader::downloadKey(QString fingerprint, Signature *sig)
{
    downloadKey(fingerprint, sig, false);
}

void KeyDownloader::downloadKey(QString fingerprint, Signature *sig, bool mirrorFailed)
{
    if (fingerprint.isEmpty() || (!sig && !mirrorFailed)) {
        return;
    }

    if (!fingerprint.startsWith(QLatin1String("0x"))) {
        fingerprint = "0x" + fingerprint;
    }

    if (m_downloading.contains(fingerprint) && !mirrorFailed) {
        if (!m_downloading.contains(fingerprint, sig)) {
            m_downloading.insert(fingerprint, sig);
        }
    } else {
        const QStringList servers = Settings::signatureKeyServers();
        if (!servers.count()) {
            KMessageBox::error(nullptr, i18n("No server for downloading keys is specified in settings. Downloading aborted."), i18n("No key server"));
            return;
        }

        QString mirror;
        if (mirrorFailed) {
            const QStringList failedMirrors = m_triedMirrors.values(fingerprint);
            for (int i = 0; i < servers.count(); ++i) {
                if (!m_triedMirrors.contains(fingerprint, servers.at(i))) {
                    mirror = servers.at(i);
                    break;
                }
            }
        } else {
            mirror = servers.first();
        }

        if (mirror.isEmpty()) {
            KMessageBox::error(nullptr,
                               i18n("No useful key server found, key not downloaded. Add more servers to the settings or restart KGet and retry downloading."),
                               i18n("No key server"));
            return;
        }

        m_triedMirrors.insert(fingerprint, mirror);
        if (!mirrorFailed) {
            m_downloading.insert(fingerprint, sig);
        }

        QUrl url;
        url.setPath(mirror + "pks/lookup");
        url.setQuery("op=get&options=mr&search=" + fingerprint);
        url.setPort(11371);

        qCDebug(KGET_DEBUG) << "Downloading:" << url;

        KIO::StoredTransferJob *job = KIO::storedGet(url, KIO::Reload, KIO::HideProgressInfo);
        m_jobs[job] = fingerprint;
        connect(job, &KJob::finished, this, &KeyDownloader::slotDownloaded);
    }
}

void KeyDownloader::slotDownloaded(KJob *job)
{
#ifdef HAVE_QGPGME
    if (!m_jobs.contains(job)) {
        return;
    }

    const QString fingerprint = m_jobs[job];
    auto *transferJob = static_cast<KIO::StoredTransferJob *>(job);

    if (transferJob->isErrorPage()) {
        qCDebug(KGET_DEBUG) << "Mirror did not work, try another one.";
        downloadKey(fingerprint, nullptr, true);
        return;
    }

    QByteArray data = transferJob->data();
    if (data.isEmpty()) {
        qCDebug(KGET_DEBUG) << "Downloaded data is empty.";
        downloadKey(fingerprint, nullptr, true);
        return;
    }

    const int indexStart = data.indexOf("<pre>");
    const int indexEnd = data.indexOf("</pre>", indexStart);
    if ((indexStart == -1) || (indexEnd == -1)) {
        qCDebug(KGET_DEBUG) << "Could not find a key.";
        downloadKey(fingerprint, nullptr, true);
        return;
    }

    data = data.mid(indexStart + 6, indexEnd - indexStart - 6);

    GpgME::initializeLibrary();
    GpgME::Error err = GpgME::checkEngine(GpgME::OpenPGP);
    if (err) {
        qCDebug(KGET_DEBUG) << "Problem checking the engine.";
        return;
    }

    QScopedPointer<GpgME::Context> context(GpgME::Context::createForProtocol(GpgME::OpenPGP));
    if (!context.data()) {
        qCDebug(KGET_DEBUG) << "Could not create context.";
        return;
    }

    QGpgME::QByteArrayDataProvider keyBA(data);
    GpgME::Data key(&keyBA);
    GpgME::ImportResult importResult = context->importKeys(key);
    err = importResult.error();
    if (err) {
        qCDebug(KGET_DEBUG) << "Error while importing key.";
        ;
        return;
    }

    qCDebug(KGET_DEBUG) << "Key downloaded, notifying requesters.";

    QList<Signature *> sigs = m_downloading.values(fingerprint);
    foreach (Signature *sig, sigs) {
        sig->d->signatureDownloaded();
    }
    m_downloading.remove(fingerprint);
#else // HAVE_QGPGME
    Q_UNUSED(job)
    qCWarning(KGET_DEBUG) << "No QGPGME support.";
#endif // HAVE_QGPGME
}

#include "moc_keydownloader.cpp"
