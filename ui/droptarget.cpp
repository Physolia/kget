/* This file is part of the KDE project

   Copyright (C) 2002 Patrick Charbonnier <pch@freeshell.org>
   Based On Caitoo v.0.7.3 (c) 1998 - 2000, Matej Koss
   Copyright (C) 2008 Urs Wolfer <uwolfer @ kde.org>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
*/

#include "ui/droptarget.h"

#include "core/kget.h"
#include "core/transfergrouphandler.h"
#include "core/transferhandler.h"
#include "core/transfertreemodel.h"
#include "mainwindow.h"
#include "settings.h"
#include "ui/newtransferdialog.h"

#include <KMessageBox>
#include <KPassivePopup>
#include <KWindowSystem>
#include <kwidgetsaddons_version.h>

#include <QBitmap>
#include <QClipboard>
#include <QDesktopWidget>
#include <QGuiApplication>
#include <QMenu>
#include <QPainter>
#include <QStringList>
#include <QTimer>
#include <QToolTip>

#include <cmath>

#define TARGET_SIZE 64
#define TARGET_ANI_MS 20
#define TARGET_TOOLTIP_MS 1000

DropTarget::DropTarget(MainWindow *mw)
    : QWidget(nullptr, Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint)
    , parentWidget(mw)
    , animTimer(nullptr)
    , showInformation(false)
{
    KWindowSystem::setState(winId(), NET::SkipTaskbar | NET::KeepAbove);

    QRect screenGeo = qApp->desktop()->screenGeometry(Settings::dropPosition());
    if ((screenGeo.x() + screenGeo.width() >= Settings::dropPosition().x() && screenGeo.y() + screenGeo.height() >= Settings::dropPosition().y())
        && Settings::dropPosition().y() >= 0 && Settings::dropPosition().x() >= 0)
        position = QPoint(Settings::dropPosition());
    else
        position = QPoint(screenGeo.x() + screenGeo.width() / 2, screenGeo.y() + screenGeo.height() / 2);
    setFixedSize(TARGET_SIZE, TARGET_SIZE);

    cachedPixmap = QIcon::fromTheme("kget").pixmap(TARGET_SIZE);
    if (!cachedPixmap.mask().isNull()) {
        QBitmap mask(size());
        mask.fill(Qt::color0);
        QBitmap pixMask = cachedPixmap.mask();
        QPainter p(&mask);
        p.drawPixmap((mask.width() - pixMask.width()) / 2, (mask.height() - pixMask.height()) / 2, pixMask);
        setMask(mask);
    } else
        setMask(QBitmap());

    // popup menu for right mouse button
    popupMenu = new QMenu(this);
    popupMenu->addSection(mw->windowTitle());

    QAction *downloadAction = mw->actionCollection()->action("start_all_download");
    popupMenu->addAction(downloadAction);
    connect(downloadAction, &QAction::toggled, this, &DropTarget::slotStartStopToggled);
    popupMenu->addSeparator();
    pop_show = popupMenu->addAction(QString(), this, &DropTarget::toggleMinimizeRestore);
    popupMenu->addAction(parentWidget->actionCollection()->action("show_drop_target"));
    pop_sticky = popupMenu->addAction(i18nc("fix position for droptarget", "Sticky"), this, &DropTarget::toggleSticky);
    pop_sticky->setCheckable(true);
    pop_sticky->setChecked(Settings::dropSticky());
    popupMenu->addSeparator();
    popupMenu->addAction(mw->actionCollection()->action("preferences"));

    auto *quitAction = new QAction(this);
    quitAction->setText(i18n("Quit KGet"));
    quitAction->setIcon(QIcon::fromTheme("system-shutdown"));
    connect(quitAction, SIGNAL(triggered()), mw, SLOT(slotQuit()));
    popupMenu->addAction(quitAction);

    isdragging = false;

    // Enable dropping
    setAcceptDrops(true);

    if (Settings::showDropTarget() && Settings::firstRun()) {
        showInformation = true;
    }

    animTimer = new QTimer(this);
    popupTimer = new QTimer(this);

    setMouseTracking(true);

    connect(KGet::model(), &TransferTreeModel::transfersChangedEvent, this, &DropTarget::slotToolTipUpdate);

    connect(popupTimer, &QTimer::timeout, this, &DropTarget::slotToolTipTimer);
}

DropTarget::~DropTarget()
{
    Settings::setDropPosition(pos());
    Settings::setShowDropTarget(!isHidden());
    Settings::self()->save();
    //    unsigned long state = KWindowSystem::windowInfo(kdrop->winId()).state();
    //    // state will be 0L if droptarget is hidden. Sigh.
    //    config->writeEntry("State", state ? state : DEFAULT_DOCK_STATE );
}

void DropTarget::setDropTargetVisible(bool shown, bool internal)
{
    if (shown == !isHidden())
        return;

    if (internal)
        Settings::setShowDropTarget(shown);

    if (!shown) {
        Settings::setDropPosition(pos());
        position = pos();
        if (Settings::animateDropTarget())
            playAnimationHide();
        else
            hide();
    } else {
        if (Settings::animateDropTarget()) {
            playAnimationShow();
        } else {
            move(position);
            show();
        }
        slotToolTipUpdate();
    }
}

void DropTarget::playAnimationShow()
{
    if (animTimer->isActive())
        animTimer->stop();
    animTimer->disconnect();
    connect(animTimer, &QTimer::timeout, this, &DropTarget::slotAnimateShow);

    move(position.x(), -TARGET_SIZE);

    ani_y = -1;
    ani_vy = 0;
    show();
    animTimer->start(TARGET_ANI_MS);
}

void DropTarget::playAnimationHide()
{
    if (animTimer->isActive())
        animTimer->stop();

    animTimer->disconnect();
    connect(animTimer, &QTimer::timeout, this, &DropTarget::slotAnimateHide);
    ani_y = (float)y();
    ani_vy = 0;
    animTimer->start(TARGET_ANI_MS);
}

void DropTarget::playAnimationSync()
{
    if (animTimer->isActive())
        animTimer->stop();

    animTimer->disconnect();
    connect(animTimer, &QTimer::timeout, this, &DropTarget::slotAnimateSync);
    ani_y = (float)y();
    ani_vy = -1;
    animTimer->start(TARGET_ANI_MS);
}

void DropTarget::slotStartStopToggled(bool started)
{
    if (started && Settings::animateDropTarget())
        playAnimationSync();
}

/** widget events */

void DropTarget::dragEnterEvent(QDragEnterEvent *event)
{
    event->setAccepted(event->mimeData()->hasUrls() || event->mimeData()->hasText());
}

void DropTarget::dropEvent(QDropEvent *event)
{
    QList<QUrl> list = event->mimeData()->urls();
    QString str;

    if (!list.isEmpty()) {
        if (list.count() == 1 && list.first().url().endsWith(QLatin1String(".kgt"))) {
#if KWIDGETSADDONS_VERSION >= QT_VERSION_CHECK(5, 100, 0)
            const int msgBoxResult = KMessageBox::questionTwoActionsCancel(this,
#else
            const int msgBoxResult = KMessageBox::questionYesNoCancel(this,
#endif
                                                                           i18n("The dropped file is a KGet Transfer List"),
                                                                           "KGet",
                                                                           KGuiItem(i18n("&Download"), QIcon::fromTheme("document-save")),
                                                                           KGuiItem(i18n("&Load transfer list"), QIcon::fromTheme("list-add")),
                                                                           KStandardGuiItem::cancel());

#if KWIDGETSADDONS_VERSION >= QT_VERSION_CHECK(5, 100, 0)
            if (msgBoxResult == KMessageBox::PrimaryAction) // Download
#else
            if (msgBoxResult == KMessageBox::Yes) // Download
#endif
                NewTransferDialogHandler::showNewTransferDialog(list.first());
#if KWIDGETSADDONS_VERSION >= QT_VERSION_CHECK(5, 100, 0)
            else if (msgBoxResult == KMessageBox::SecondaryAction) // Load
#else
            else if (msgBoxResult == KMessageBox::No) // Load
#endif
                KGet::load(list.first().url());
        } else {
            if (list.count() == 1) {
                str = event->mimeData()->text();
                NewTransferDialogHandler::showNewTransferDialog(QUrl(str));
            } else
                NewTransferDialogHandler::showNewTransferDialog(list);
        }
    } else {
        NewTransferDialogHandler::showNewTransferDialog();
    }

    if (Settings::animateDropTarget())
        playAnimationSync();
}

void DropTarget::closeEvent(QCloseEvent *e)
{
    if (qApp->isSavingSession())
        e->ignore();
    else {
        setVisible(false);
        e->accept();
    }
}

void DropTarget::mousePressEvent(QMouseEvent *e)
{
    // If the user click on the droptarget, stop any animation that is going on
    if (animTimer) {
        animTimer->stop();
    }

    if (e->button() == Qt::LeftButton) {
        isdragging = true;
        dx = e->globalPos().x() - pos().x();
        dy = e->globalPos().y() - pos().y();
    } else if (e->button() == Qt::RightButton) {
        pop_show->setText(parentWidget->isHidden() ? i18n("Show Main Window") : i18n("Hide Main Window"));
        popupMenu->popup(e->globalPos());
    } else if (e->button() == Qt::MidButton) {
        // Here we paste the transfer
        QString newtransfer = QApplication::clipboard()->text();
        newtransfer = newtransfer.trimmed();

        if (!newtransfer.isEmpty())
            KGet::addTransfer(QUrl(newtransfer), QString(), QString(), QString(), true);
    }
}

void DropTarget::mouseReleaseEvent(QMouseEvent *)
{
    isdragging = false;
}

void DropTarget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        toggleMinimizeRestore();
}

void DropTarget::mouseMoveEvent(QMouseEvent *e)
{
    Q_UNUSED(e)
    if (isdragging && !Settings::dropSticky()) {
        move(QCursor::pos().x() - dx, QCursor::pos().y() - dy);
        e->accept();
    }
}

void DropTarget::enterEvent(QEvent *event)
{
    Q_UNUSED(event)
    popupTimer->start(2000);
}

void DropTarget::leaveEvent(QEvent *event)
{
    Q_UNUSED(event)
    popupTimer->stop();
}

void DropTarget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.drawPixmap(0, 0, cachedPixmap);
}

void DropTarget::toggleSticky()
{
    Settings::setDropSticky(!Settings::dropSticky());
    pop_sticky->setChecked(Settings::dropSticky());
}

void DropTarget::toggleMinimizeRestore()
{
    bool nextState = parentWidget->isHidden();
    Settings::setShowMain(nextState);
    parentWidget->setVisible(nextState);
    if (nextState) {
        KWindowSystem::activateWindow(static_cast<KXmlGuiWindow *>(parentWidget)->winId());
    }
}

/** widget animations */
void DropTarget::slotAnimateShow()
{
    static float dT = TARGET_ANI_MS / 1000.0;

    ani_vy -= ani_y * 30 * dT;
    ani_vy *= 0.95;
    ani_y += ani_vy * dT;

    move(x(), qRound(position.y() * (1 + ani_y)));

    if (fabs(ani_y) < 0.01 && fabs(ani_vy) < 0.01 && animTimer->isActive()) {
        animTimer->stop();

        if (showInformation)
            KPassivePopup::message(i18n("Drop Target"), i18n("You can drag download links into the drop target."), this);
    }
}

void DropTarget::slotAnimateHide()
{
    static float dT = TARGET_ANI_MS / 1000.0;

    ani_vy += -2000 * dT;
    float new_y = y() + ani_vy * dT;

    if (new_y < -height()) {
        animTimer->stop();
        hide();
        move(x(), qRound(ani_y));
    } else
        move(x(), qRound(new_y));
}

void DropTarget::slotAnimateSync()
{
    static float dT = TARGET_ANI_MS / 1000.0;

    ani_vy += 4 * dT; // from -1 to 1 in 0.5 seconds
    float i = 2 * M_PI * ani_vy; // from -2PI to 2PI
    float j = (i == 0.0) ? 1 : (sin(i) / i) * (1 + fabs(ani_vy));

    if (ani_vy >= 1) {
        animTimer->stop();
        move(x(), qRound(ani_y));
    } else
        move(x(), qRound(ani_y + 6 * j));
}

void DropTarget::slotToolTipUpdate()
{
    QStringList dataList;
    QString data;

    foreach (TransferHandler *transfer, KGet::allTransfers()) {
        data.clear();
        switch (transfer->status()) {
        case Job::Finished:
            data = i18nc("%1 filename, %2 total size, %3 status",
                         "%1(%2) %3",
                         transfer->source().fileName(),
                         KIO::convertSize(transfer->totalSize()),
                         transfer->statusText());
            break;
        case Job::Running:
            data = i18nc("%1 filename, %2 percent complete, %3 downloaded out of %4 total size",
                         "%1(%2% %3/%4) Speed:%5/s",
                         transfer->source().fileName(),
                         transfer->percent(),
                         KIO::convertSize(transfer->downloadedSize()),
                         KIO::convertSize(transfer->totalSize()),
                         KIO::convertSize(transfer->downloadSpeed()));
            break;
        default:
            data = i18nc("%1 filename, %2 percent complete, %3 downloaded out of %4 total size, %5 status",
                         "%1(%2% %3/%4) %5",
                         transfer->source().fileName(),
                         transfer->percent(),
                         KIO::convertSize(transfer->downloadedSize()),
                         KIO::convertSize(transfer->totalSize()),
                         transfer->statusText());
            break;
        }
        dataList << data;
    }

    if (!dataList.empty())
        tooltipText = dataList.join("\n");
    else
        tooltipText = i18n("Ready");
}

void DropTarget::slotToolTipTimer()
{
    if (!popupMenu->isVisible() && isVisible() && mask().contains(mapFromGlobal(QCursor::pos())))
        QToolTip::showText(QCursor::pos(), tooltipText, this, rect());
}

void DropTarget::slotClose()
{
    setVisible(false);
}

#include "moc_droptarget.cpp"
