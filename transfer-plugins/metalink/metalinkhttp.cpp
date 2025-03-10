/* This file is part of the KDE project

   Copyright (C) 2004 Dario Massarin <nekkar@libero.it>
   Copyright (C) 2007 Manolo Valdes <nolis71cu@gmail.com>
   Copyright (C) 2009 Matthias Fuchs <mat69@gmx.net>
   Copyright (C) 2012 Aish Raj Dahal <dahalaishraj@gmail.com>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
*/

#include "metalinkhttp.h"
#include "metalinksettings.h"
#include "metalinkxml.h"

#include "core/download.h"
#include "core/filemodel.h"
#include "core/kget.h"
#include "core/signature.h"
#include "core/transferdatasource.h"
#include "core/transfergroup.h"
#include "core/urlchecker.h"
#include "core/verifier.h"

#include "kget_debug.h"

#include <KIO/DeleteJob>
#include <KIO/RenameDialog>
#include <KLocalizedString>
#include <KMessageBox>
#include <QDialog>
#include <kwidgetsaddons_version.h>

#include <KConfigGroup>
#include <QDir>
#include <QDomElement>
#include <QFile>
#include <QStandardPaths>

/**
 * @return Hex value from a base64 value
 * @note needed for hex based signature verification
 */
QString base64ToHex(const QString &b64)
{
    return QString(QByteArray::fromBase64(b64.toLatin1()).toHex());
}

MetalinkHttp::MetalinkHttp(TransferGroup *parent,
                           TransferFactory *factory,
                           Scheduler *scheduler,
                           const QUrl &source,
                           const QUrl &dest,
                           KGetMetalink::MetalinkHttpParser *httpParser,
                           const QDomElement *e)
    : AbstractMetalink(parent, factory, scheduler, source, dest, e)
    , m_signatureUrl(QUrl())
    , m_httpparser(httpParser)

{
    m_httpparser->setParent(this);
}

MetalinkHttp::~MetalinkHttp()
{
}

void MetalinkHttp::load(const QDomElement *element)
{
    qCDebug(KGET_DEBUG);
    Transfer::load(element);
    auto *fac = new DataSourceFactory(this, m_dest);
    m_dataSourceFactory.insert(m_dest, fac);

    connect(fac, &DataSourceFactory::capabilitiesChanged, this, &MetalinkHttp::slotUpdateCapabilities);
    connect(fac, &DataSourceFactory::dataSourceFactoryChange, this, &MetalinkHttp::slotDataSourceFactoryChange);
    connect(fac->verifier(), &Verifier::verified, this, &MetalinkHttp::slotVerified);
    connect(fac->signature(), SIGNAL(verified(int)), this, SLOT(slotSignatureVerified()));
    connect(fac, &DataSourceFactory::log, this, &Transfer::setLog);

    fac->load(element);

    if (fac->mirrors().isEmpty()) {
        return;
    }

    m_ready = true;
}

void MetalinkHttp::save(const QDomElement &element)
{
    qCDebug(KGET_DEBUG);
    Transfer::save(element);
    m_dataSourceFactory.begin().value()->save(element);
}

void MetalinkHttp::startMetalink()
{
    if (m_ready) {
        foreach (DataSourceFactory *factory, m_dataSourceFactory) {
            // specified number of files is downloaded simultaneously
            if (m_currentFiles < MetalinkSettings::simultaneousFiles()) {
                const Job::Status status = factory->status();

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

void MetalinkHttp::start()
{
    qDebug() << "metalinkhttp::start";

    if (!m_ready) {
        setLinks();
        setDigests();
        if (metalinkHttpInit()) {
            startMetalink();
        }
    } else {
        startMetalink();
    }
}

void MetalinkHttp::setSignature(QUrl &dest, QByteArray &data, DataSourceFactory *dataFactory)
{
    Q_UNUSED(dest);
    dataFactory->signature()->setSignature(data, Signature::AsciiDetached);
}

void MetalinkHttp::slotSignatureVerified()
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
                brokenFiles.append(factory->dest().toString());
            }
        }

        if (brokenFiles.count()) {
#if KWIDGETSADDONS_VERSION >= QT_VERSION_CHECK(5, 100, 0)
            if (KMessageBox::warningTwoActionsList(nullptr,
#else
            if (KMessageBox::warningYesNoList(nullptr,
#endif
                                                   i18n("The download could not be verified, try to repair it?"),
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
                    KGet::addTransfer(m_metalinkxmlUrl);
                    // TODO Use a Notification instead. Check kget.h for how to use it.
                }
            }
        }
    }
}

bool MetalinkHttp::metalinkHttpInit()
{
    qDebug() << "m_dest = " << m_dest;
    const QUrl tempDest = QUrl(m_dest.adjusted(QUrl::RemoveFilename));
    QUrl dest = QUrl(tempDest.toString() + m_dest.fileName());
    qDebug() << "dest = " << dest;

    // sort the urls according to their priority (highest first)
    std::stable_sort(m_linkheaderList.begin(), m_linkheaderList.end());

    auto *dataFactory = new DataSourceFactory(this, dest);
    dataFactory->setMaxMirrorsUsed(MetalinkSettings::mirrorsPerFile());

    connect(dataFactory, &DataSourceFactory::capabilitiesChanged, this, &MetalinkHttp::slotUpdateCapabilities);
    connect(dataFactory, &DataSourceFactory::dataSourceFactoryChange, this, &MetalinkHttp::slotDataSourceFactoryChange);
    connect(dataFactory->verifier(), &Verifier::verified, this, &MetalinkHttp::slotVerified);
    connect(dataFactory->signature(), SIGNAL(verified(int)), this, SLOT(slotSignatureVerified()));
    connect(dataFactory, &DataSourceFactory::log, this, &Transfer::setLog);

    // add the Mirrors Sources

    for (int i = 0; i < m_linkheaderList.size(); ++i) {
        const QUrl url = m_linkheaderList[i].url;
        if (url.isValid()) {
            if (m_linkheaderList[i].pref) {
                qDebug() << "found etag in a mirror";
                auto *eTagCher = new KGetMetalink::MetalinkHttpParser(url);
                if (eTagCher->getEtag() != m_httpparser->getEtag()) { // There is an ETag mismatch
                    continue;
                }
            }

            dataFactory->addMirror(url, MetalinkSettings::connectionsPerUrl());
        }
    }

    // no datasource has been created, so remove the datasource factory
    if (dataFactory->mirrors().isEmpty()) {
        qDebug() << "data source factory being deleted";
        delete dataFactory;
    } else {
        QHashIterator<QString, QString> itr(m_DigestList);
        while (itr.hasNext()) {
            itr.next();
            qDebug() << itr.key() << ":" << itr.value();
        }

        dataFactory->verifier()->addChecksums(m_DigestList);

        // Add OpenPGP signatures
        if (m_signatureUrl != QUrl()) {
            // make sure that the DataLocation directory exists (earlier this used to be handled by KStandardDirs)
            if (!QFileInfo::exists(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))) {
                QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
            }
            const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/metalinks/") + m_source.fileName();
            auto *signat_download = new Download(m_signatureUrl, QUrl::fromLocalFile(path));
            connect(signat_download, SIGNAL(finishedSuccessfully(QUrl, QByteArray)), SLOT(setSignature(QUrl, QByteArray)));
        }
        m_dataSourceFactory[dataFactory->dest()] = dataFactory;
    }

    if (m_dataSourceFactory.size()) {
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

    return true;
}

void MetalinkHttp::setLinks()
{
    const QMultiMap<QString, QString> *headerInf = m_httpparser->getHeaderInfo();
    const QList<QString> linkVals = headerInf->values("link");

    foreach (const QString link, linkVals) {
        const KGetMetalink::HttpLinkHeader linkheader(link);

        if (linkheader.reltype == "duplicate") {
            m_linkheaderList.append(linkheader);
        } else if (linkheader.reltype == "application/pgp-signature") {
            m_signatureUrl = linkheader.url; // There will only be one signature
        } else if (linkheader.reltype == "application/metalink4+xml") {
            m_metalinkxmlUrl = linkheader.url; // There will only be one metalink xml (metainfo URL)
        }
    }
}

void MetalinkHttp::deinit(Transfer::DeleteOptions options)
{
    foreach (DataSourceFactory *factory, m_dataSourceFactory) {
        if (options & Transfer::DeleteFiles) {
            factory->deinit();
        }
    }
}

void MetalinkHttp::setDigests()
{
    const QMultiMap<QString, QString> *digestInfo = m_httpparser->getHeaderInfo();
    const QList<QString> digestList = digestInfo->values("digest");

    foreach (const QString digest, digestList) {
        const int eqDelimiter = digest.indexOf('=');
        const QString digestType = MetalinkHttp::adaptDigestType(digest.left(eqDelimiter).trimmed());
        const QString hexDigestValue = base64ToHex(digest.mid(eqDelimiter + 1).trimmed());

        m_DigestList.insertMulti(digestType, hexDigestValue);
    }
}

QString MetalinkHttp::adaptDigestType(const QString &hashType)
{
    if (hashType == QString("SHA")) {
        return QString("sha");
    } else if (hashType == QString("MD5")) {
        return QString("md5");
    } else if (hashType == QString("SHA-256")) {
        return QString("sha256");
    } else {
        return hashType;
    }
}

#include "moc_metalinkhttp.cpp"
