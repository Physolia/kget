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

#include "../dbus/dbusverifierwrapper.h"
#include "settings.h"
#include "verificationmodel.h"
#include "verifier_p.h"
#include "verifieradaptor.h"

#include <QDomElement>
#include <QFile>

#include "kget_debug.h"
#include <QDebug>

#include <vector>

struct VerifierAlgo {
    QString type;
    QCryptographicHash::Algorithm qtType;
    int diggestLength;
};

const std::vector<VerifierAlgo> SUPPORTED_ALGOS = {{"sha512", QCryptographicHash::Sha512, 128},
                                                   {"sha384", QCryptographicHash::Sha384, 96},
                                                   {"sha256", QCryptographicHash::Sha256, 64},
                                                   {"sha1", QCryptographicHash::Sha1, 40},
                                                   {"md5", QCryptographicHash::Md5, 32},
                                                   {"md4", QCryptographicHash::Md4, 32}};

const int VerifierPrivate::PARTSIZE = 500 * 1024;

VerifierPrivate::~VerifierPrivate()
{
    delete model;
    qDeleteAll(partialSums.begin(), partialSums.end());
}

static QCryptographicHash::Algorithm qtAlgorithmForType(const QString &type)
{
    for (const VerifierAlgo &alg : SUPPORTED_ALGOS) {
        if (type == alg.type) {
            return alg.qtType;
        }
    }

    return QCryptographicHash::Md5;
}

QString VerifierPrivate::calculatePartialChecksum(QFile *file,
                                                  const QString &type,
                                                  KIO::fileoffset_t startOffset,
                                                  int pieceLength,
                                                  KIO::filesize_t fileSize,
                                                  bool *abortPtr)
{
    if (!file) {
        return QString();
    }

    if (!fileSize) {
        fileSize = file->size();
    }
    // longer than the file, so adapt it
    if (static_cast<KIO::fileoffset_t>(fileSize) < startOffset + pieceLength) {
        pieceLength = fileSize - startOffset;
    }

    QCryptographicHash hash(qtAlgorithmForType(type));

    // we only read 512kb each time, to save RAM
    int numData = pieceLength / PARTSIZE;
    KIO::fileoffset_t dataRest = pieceLength % PARTSIZE;

    if (!numData && !dataRest) {
        return QString();
    }

    int k = 0;
    for (k = 0; k < numData; ++k) {
        if (!file->seek(startOffset + PARTSIZE * k)) {
            return QString();
        }

        if (abortPtr && *abortPtr) {
            return QString();
        }

        QByteArray data = file->read(PARTSIZE);
        hash.addData(data);
    }

    // now read the rest
    if (dataRest) {
        if (!file->seek(startOffset + PARTSIZE * k)) {
            return QString();
        }

        QByteArray data = file->read(dataRest);
        hash.addData(data);
    }

    return hash.result().toHex();
}

QStringList VerifierPrivate::orderChecksumTypes(Verifier::ChecksumStrength strength) const
{
    QStringList checksumTypes = q->supportedVerficationTypes();
    if (strength == Verifier::Weak) {
        std::reverse(checksumTypes.begin(), checksumTypes.end());
        checksumTypes.move(0, 1); // md4 second position
    } else if (strength == Verifier::Strong) {
        std::reverse(checksumTypes.begin(), checksumTypes.end());
        checksumTypes.move(1, checksumTypes.count() - 1); // md5 second last position
        checksumTypes.move(0, checksumTypes.count() - 1); // md4 last position
    } else if (strength == Verifier::Strongest) {
        // nothing
    }

    return checksumTypes;
}

Verifier::Verifier(const QUrl &dest, QObject *parent)
    : QObject(parent)
    , d(new VerifierPrivate(this))
{
    d->dest = dest;
    d->status = NoResult;

    static int dBusObjIdx = 0;
    d->dBusObjectPath = "/KGet/Verifiers/" + QString::number(dBusObjIdx++);

    auto *wrapper = new DBusVerifierWrapper(this);
    new VerifierAdaptor(wrapper);
    QDBusConnection::sessionBus().registerObject(d->dBusObjectPath, wrapper);

    qRegisterMetaType<KIO::filesize_t>("KIO::filesize_t");
    qRegisterMetaType<KIO::fileoffset_t>("KIO::fileoffset_t");
    qRegisterMetaType<QList<KIO::fileoffset_t>>("QList<KIO::fileoffset_t>");

    d->model = new VerificationModel();
    connect(&d->thread, SIGNAL(verified(QString, bool, QUrl)), this, SLOT(changeStatus(QString, bool)));
    connect(&d->thread, SIGNAL(brokenPieces(QList<KIO::fileoffset_t>, KIO::filesize_t)), this, SIGNAL(brokenPieces(QList<KIO::fileoffset_t>, KIO::filesize_t)));
}

Verifier::~Verifier()
{
    delete d;
}

QString Verifier::dBusObjectPath() const
{
    return d->dBusObjectPath;
}

QUrl Verifier::destination() const
{
    return d->dest;
}

void Verifier::setDestination(const QUrl &destination)
{
    d->dest = destination;
}

Verifier::VerificationStatus Verifier::status() const
{
    return d->status;
}

VerificationModel *Verifier::model()
{
    return d->model;
}

QStringList Verifier::supportedVerficationTypes()
{
    static QStringList list;
    if (list.isEmpty()) {
        for (const VerifierAlgo &alg : SUPPORTED_ALGOS) {
            list << alg.type;
        }
    }
    return list;
}

int Verifier::diggestLength(const QString &type)
{
    for (const VerifierAlgo &alg : SUPPORTED_ALGOS) {
        if (type == alg.type) {
            return alg.diggestLength;
        }
    }
    return 0;
}

bool Verifier::isChecksum(const QString &type, const QString &checksum)
{
    const int length = diggestLength(type);
    const QString pattern = QString("[0-9a-z]{%1}").arg(length);
    // needs correct length and only word characters
    if (length && (checksum.length() == length) && checksum.toLower().contains(QRegExp(pattern))) {
        return true;
    }

    return false;
}

QString Verifier::cleanChecksumType(const QString &type)
{
    QString hashType = type.toUpper();
    if (hashType.contains(QRegExp("^SHA\\d+"))) {
        hashType.insert(3, '-');
    }

    return hashType;
}

bool Verifier::isVerifyable() const
{
    return QFile::exists(d->dest.toLocalFile()) && d->model->rowCount();
}

bool Verifier::isVerifyable(const QModelIndex &index) const
{
    int row = -1;
    if (index.isValid()) {
        row = index.row();
    }
    if (QFile::exists(d->dest.toLocalFile()) && (row >= 0) && (row < d->model->rowCount())) {
        return true;
    }
    return false;
}

Checksum Verifier::availableChecksum(Verifier::ChecksumStrength strength) const
{
    Checksum pair;

    // check if there is at least one entry
    QModelIndex index = d->model->index(0, 0);
    if (!index.isValid()) {
        return pair;
    }

    const QStringList available = supportedVerficationTypes();
    const QStringList supported = d->orderChecksumTypes(strength);
    for (int i = 0; i < supported.count(); ++i) {
        QModelIndexList indexList = d->model->match(index, Qt::DisplayRole, supported.at(i));
        if (!indexList.isEmpty() && available.contains(supported.at(i))) {
            QModelIndex match = d->model->index(indexList.first().row(), VerificationModel::Checksum);
            pair.first = supported.at(i);
            pair.second = match.data().toString();
            break;
        }
    }

    return pair;
}

QList<Checksum> Verifier::availableChecksums() const
{
    QList<Checksum> checksums;

    for (int i = 0; i < d->model->rowCount(); ++i) {
        const QString type = d->model->index(i, VerificationModel::Type).data().toString();
        const QString hash = d->model->index(i, VerificationModel::Checksum).data().toString();
        checksums << qMakePair(type, hash);
    }

    return checksums;
}

QPair<QString, PartialChecksums *> Verifier::availablePartialChecksum(Verifier::ChecksumStrength strength) const
{
    QPair<QString, PartialChecksums *> pair;
    QString type;
    PartialChecksums *checksum = nullptr;

    const QStringList available = supportedVerficationTypes();
    const QStringList supported = d->orderChecksumTypes(strength);
    for (int i = 0; i < supported.size(); ++i) {
        if (d->partialSums.contains(supported.at(i)) && available.contains(supported.at(i))) {
            type = supported.at(i);
            checksum = d->partialSums[type];
            break;
        }
    }

    return QPair<QString, PartialChecksums *>(type, checksum);
}

void Verifier::changeStatus(const QString &type, bool isVerified)
{
    qCDebug(KGET_DEBUG) << "Verified:" << isVerified;
    d->status = isVerified ? Verifier::Verified : Verifier::NotVerified;
    d->model->setVerificationStatus(type, d->status);
    Q_EMIT verified(isVerified);
}

void Verifier::verify(const QModelIndex &index)
{
    int row = -1;
    if (index.isValid()) {
        row = index.row();
    }

    QString type;
    QString checksum;

    if (row == -1) {
        Checksum pair = availableChecksum(static_cast<Verifier::ChecksumStrength>(Settings::checksumStrength()));
        type = pair.first;
        checksum = pair.second;
    } else if ((row >= 0) && (row < d->model->rowCount())) {
        type = d->model->index(row, VerificationModel::Type).data().toString();
        checksum = d->model->index(row, VerificationModel::Checksum).data().toString();
    }

    d->thread.verify(type, checksum, d->dest);
}

void Verifier::brokenPieces() const
{
    QPair<QString, PartialChecksums *> pair = availablePartialChecksum(static_cast<Verifier::ChecksumStrength>(Settings::checksumStrength()));
    QList<QString> checksums;
    KIO::filesize_t length = 0;
    if (pair.second) {
        checksums = pair.second->checksums();
        length = pair.second->length();
    }
    d->thread.findBrokenPieces(pair.first, checksums, length, d->dest);
}

QString Verifier::checksum(const QUrl &dest, const QString &type, bool *abortPtr)
{
    QStringList supported = supportedVerficationTypes();
    if (!supported.contains(type)) {
        return QString();
    }

    QFile file(dest.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QCryptographicHash hash(qtAlgorithmForType(type));

    char buffer[1024];
    int len;

    while ((len = file.read(reinterpret_cast<char *>(buffer), sizeof(buffer))) > 0) {
        hash.addData(buffer, len);
        if (abortPtr && *abortPtr) {
            file.close();
            return QString();
        }
    }
    QString final = hash.result().toHex();
    file.close();
    return final;
}

PartialChecksums Verifier::partialChecksums(const QUrl &dest, const QString &type, KIO::filesize_t length, bool *abortPtr)
{
    QStringList checksums;

    QStringList supported = supportedVerficationTypes();
    if (!supported.contains(type)) {
        return PartialChecksums();
    }

    QFile file(dest.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) {
        return PartialChecksums();
    }

    const KIO::filesize_t fileSize = file.size();
    if (!fileSize) {
        return PartialChecksums();
    }

    int numPieces = 0;

    // the piece length has been defined
    if (length) {
        numPieces = fileSize / length;
    } else {
        length = VerifierPrivate::PARTSIZE;
        numPieces = fileSize / length;
        if (numPieces > 100) {
            numPieces = 100;
            length = fileSize / numPieces;
        }
    }

    // there is a rest, so increase numPieces by one
    if (fileSize % length) {
        ++numPieces;
    }

    PartialChecksums partialChecksums;

    // create all the checksums for the pieces
    for (int i = 0; i < numPieces; ++i) {
        QString hash = VerifierPrivate::calculatePartialChecksum(&file, type, length * i, length, fileSize, abortPtr);
        if (hash.isEmpty()) {
            file.close();
            return PartialChecksums();
        }
        checksums.append(hash);
    }

    partialChecksums.setLength(length);
    partialChecksums.setChecksums(checksums);
    file.close();
    return partialChecksums;
}

void Verifier::addChecksum(const QString &type, const QString &checksum, int verified)
{
    d->model->addChecksum(type, checksum, verified);
}

void Verifier::addChecksums(const QHash<QString, QString> &checksums)
{
    d->model->addChecksums(checksums);
}

void Verifier::addPartialChecksums(const QString &type, KIO::filesize_t length, const QStringList &checksums)
{
    if (!d->partialSums.contains(type) && length && !checksums.isEmpty()) {
        d->partialSums[type] = new PartialChecksums(length, checksums);
    }
}

KIO::filesize_t Verifier::partialChunkLength() const
{
    for (const VerifierAlgo &alg : SUPPORTED_ALGOS) {
        if (d->partialSums.contains(alg.type)) {
            return d->partialSums[alg.type]->length();
        }
    }

    return 0;
}

void Verifier::save(const QDomElement &element)
{
    QDomElement e = element;
    e.setAttribute("verificationStatus", d->status);

    QDomElement verification = e.ownerDocument().createElement("verification");
    for (int i = 0; i < d->model->rowCount(); ++i) {
        QDomElement hash = e.ownerDocument().createElement("hash");
        hash.setAttribute("type", d->model->index(i, VerificationModel::Type).data().toString());
        hash.setAttribute("verified", d->model->index(i, VerificationModel::Verified).data(Qt::EditRole).toInt());
        QDomText value = e.ownerDocument().createTextNode(d->model->index(i, VerificationModel::Checksum).data().toString());
        hash.appendChild(value);
        verification.appendChild(hash);
    }

    QHash<QString, PartialChecksums *>::const_iterator it;
    QHash<QString, PartialChecksums *>::const_iterator itEnd = d->partialSums.constEnd();
    for (it = d->partialSums.constBegin(); it != itEnd; ++it) {
        QDomElement pieces = e.ownerDocument().createElement("pieces");
        pieces.setAttribute("type", it.key());
        pieces.setAttribute("length", (*it)->length());
        QList<QString> checksums = (*it)->checksums();
        for (int i = 0; i < checksums.size(); ++i) {
            QDomElement hash = e.ownerDocument().createElement("hash");
            hash.setAttribute("piece", i);
            QDomText value = e.ownerDocument().createTextNode(checksums[i]);
            hash.appendChild(value);
            pieces.appendChild(hash);
        }
        verification.appendChild(pieces);
    }
    e.appendChild(verification);
}

void Verifier::load(const QDomElement &e)
{
    if (e.hasAttribute("verificationStatus")) {
        const int status = e.attribute("verificationStatus").toInt();
        switch (status) {
        case NoResult:
            d->status = NoResult;
            break;
        case NotVerified:
            d->status = NotVerified;
            break;
        case Verified:
            d->status = Verified;
            break;
        default:
            d->status = NotVerified;
            break;
        }
    }

    QDomElement verification = e.firstChildElement("verification");
    QDomNodeList const hashList = verification.elementsByTagName("hash");

    for (int i = 0; i < hashList.length(); ++i) {
        const QDomElement hash = hashList.item(i).toElement();
        const QString value = hash.text();
        const QString type = hash.attribute("type");
        const int verificationStatus = hash.attribute("verified").toInt();
        if (!type.isEmpty() && !value.isEmpty()) {
            d->model->addChecksum(type, value, verificationStatus);
        }
    }

    QDomNodeList const piecesList = verification.elementsByTagName("pieces");

    for (int i = 0; i < piecesList.length(); ++i) {
        QDomElement pieces = piecesList.at(i).toElement();

        const QString type = pieces.attribute("type");
        const KIO::filesize_t length = pieces.attribute("length").toULongLong();
        QStringList partialChecksums;

        const QDomNodeList partialHashList = pieces.elementsByTagName("hash");
        for (int j = 0; j < partialHashList.size(); ++j) // TODO give this function the size of the file, to calculate how many hashs are needed as an
                                                         // additional check, do that check in addPartialChecksums?!
        {
            const QString hash = partialHashList.at(j).toElement().text();
            if (hash.isEmpty()) {
                break;
            }
            partialChecksums.append(hash);
        }

        addPartialChecksums(type, length, partialChecksums);
    }
}

#include "moc_verifier.cpp"
