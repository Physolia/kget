/* This file is part of the KDE project

   Copyright (C) 2005 Dario Massarin <nekkar@libero.it>
   Copyright (C) 2009 Lukas Appelhans <l.appelhans@gmx.de>
   Copyright (C) 2009 Matthias Fuchs <mat69@gmx.net>

   Based on:
       kmainwidget.{h,cpp}
       Copyright (C) 2002 by Patrick Charbonnier <pch@freeshell.org>
       that was based On Caitoo v.0.7.3 (c) 1998 - 2000, Matej Koss

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
*/

#ifndef KGET_H
#define KGET_H

#include <KActionCollection>
#include <KLocalizedString>
#include <KNotification>
#include <KPluginFactory>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QNetworkConfigurationManager>
#else
#include <QNetworkInformation>
#endif
#include <QDomElement>

#include "kget_export.h"
#include "kuiserverjobs.h"
#include "scheduler.h"
#include "transfer.h"
#include "transfergrouphandler.h"
#include "transferhandler.h"

class QDomElement;

class TransferDataSource;
class TransferGroup;
class TransferFactory;
class TransferTreeModel;
class TransferTreeSelectionModel;
class KGetPlugin;
class MainWindow;
class NewTransferDialog;
class TransferGroupScheduler;
class TransferHistoryStore;

/**
 * This is our KGet class. This is where the user's transfers and searches are
 * stored and organized.
 * Use this class from the views to add or remove transfers or searches
 * In order to organize the transfers inside categories we have a TransferGroup
 * class. By definition, a transfer must always belong to a TransferGroup. If we
 * don't want it to be displayed by the gui inside a specific group, we will put
 * it in the group named "Not grouped" (better name?).
 **/

class KGET_EXPORT KGet
{
    friend class NewTransferDialog;
    friend class NewTransferDialogHandler;
    friend class GenericObserver;
    friend class TransferTreeModel;
    friend class UrlChecker;

public:
    enum AfterFinishAction {
        Quit = 0,
        Shutdown = 1,
        Hibernate = 2,
        Suspend = 3,
    };
    enum DeleteMode {
        AutoDelete,
        DeleteFiles,
    };
    ~KGet();

    static KGet *self(MainWindow *mainWindow = nullptr);

    /**
     * Adds a new group to the KGet.
     *
     * @param groupName The name of the new group
     *
     * @returns true if the group has been successfully added, otherwise
     *          it returns false, probably because a group with that named
     *          already exists
     */
    static bool addGroup(const QString &groupName);

    /**
     * Removes a group from the KGet.
     *
     * @param group The name of the group to be deleted
     * @param askUser Whether to ask user about the deletion
     */
    static void delGroup(TransferGroupHandler *group, bool askUser = true);

    /**
     * Removes specific groups from the KGet.
     *
     * @param groups The names of the groups to be deleted.
     * @param askUser Whether to ask user about the deletion
     */
    static void delGroups(QList<TransferGroupHandler *> groups, bool askUser = true);

    /**
     * Changes the name of the group
     *
     * @param oldName the name of the group to be changed
     * @param newName the new name of the group
     */
    static void renameGroup(const QString &oldName, const QString &newName);

    /**
     * @returns the name of the available transfers groups
     */
    static QStringList transferGroupNames();

    /**
     * Adds a new transfer to the KGet
     *
     * @param srcUrl The url to be downloaded
     * @param destDir The destination directory. If empty we show a dialog
     * where the user can choose it.
     * @param suggestedFileName a suggestion of a simple filename to be saved in destDir
     * @param groupName The name of the group the new transfer will belong to
     * @param start Specifies if the newly added transfers should be started.
     * If the group queue is already in a running state, this flag does nothing
     */
    static TransferHandler *
    addTransfer(QUrl srcUrl, QString destDir = QString(), QString suggestedFileName = QString(), QString groupName = QString(), bool start = false);

    /**
     * Adds new transfers to the KGet, it is assumed that this takes place because of loading
     * that results in less checks for location etc.
     *
     * @param elements The dom elements of the transfers to add
     * @param groupName The name of the group the new transfer will belong to
     */
    static QList<TransferHandler *> addTransfers(const QList<QDomElement> &elements, const QString &groupName = QString());

    /**
     * Adds new transfers to the KGet
     *
     * @param srcUrls The urls to be downloaded
     * @param destDir The destination directory. If empty we show a dialog
     * where the user can choose it.
     * @param groupName The name of the group the new transfer will belong to
     * @param start Specifies if the newly added transfers should be started.
     * If the group queue is already in a running state, this flag does nothing
     */
    static const QList<TransferHandler *> addTransfer(QList<QUrl> srcUrls, QString destDir = QString(), QString groupName = QString(), bool start = false);

    /**
     * Removes a transfer from the KGet
     *
     * @param transfer The transfer to be removed
     * @param mode The deletion mode
     */
    static bool delTransfer(TransferHandler *transfer, DeleteMode mode = AutoDelete);

    /**
     * Removes multiple transfers from the KGet
     *
     * @param transfers The transfers to be removed
     * @param mode The deletion mode
     */
    static bool delTransfers(const QList<TransferHandler *> &transfers, DeleteMode mode = AutoDelete);

    /**
     * Moves a transfer to a new group
     *
     * @param transfer The transfer to be moved
     * @param groupName The name of the new transfer's group
     */
    static void moveTransfer(TransferHandler *transfer, const QString &groupName);

    /**
     * Redownload a transfer
     * @param transfer the transfer to redownload
     */
    static void redownloadTransfer(TransferHandler *transfer);

    /**
     * @returns the list of selected transfers
     */
    static QList<TransferHandler *> selectedTransfers();

    /**
     * @returns the list of the finished transfers
     */
    static QList<TransferHandler *> finishedTransfers();

    /**
     * @returns the list of selected groups
     */
    static QList<TransferGroupHandler *> selectedTransferGroups();

    /**
     * @returns a pointer to the TransferTreeModel object
     */
    static TransferTreeModel *model();

    /**
     * @returns a pointer to the QItemSelectionModel object
     */
    static TransferTreeSelectionModel *selectionModel();

    /**
     * Imports the transfers and groups included in the provided xml file
     *
     * @param filename the file name to
     */
    static void load(QString filename = QString());

    /**
     * Exports all the transfers and groups to the given file
     *
     * @param filename the file name
     * @param plain should list be in plain mode or kget mode
     */
    static void save(QString filename = QString(), bool plain = false);

    /**
     * @returns a list of all transferfactories
     */
    static QList<TransferFactory *> factories();

    /**
     * @returns a list of pluginInfos associated with all transferFactories
     */
    static QVector<KPluginMetaData> plugins();

    /**
     * @returns The factory of a given transfer
     *
     * @param transfer the transfer about which we want to have the factory
     */
    static TransferFactory *factory(TransferHandler *transfer);

    /**
     * @return a pointer to the KActionCollection objects
     */
    static KActionCollection *actionCollection();

    /**
     * if running == true starts the scheduler
     * if running == false stops the scheduler
     */
    static void setSchedulerRunning(bool running = true);

    /**
     * Returns true if the scheduler has running jobs.
     */
    static bool schedulerRunning();

    /**
     * true suspends the scheduler, any events that would result in a reschedule are ignored
     * false wakes up the scheduler, events result in reschedule again
     * NOTE this is a HACK for cases where the scheduler is the bottleneck, e.g. when stopping
     * a lot of running transfers, or starting a lot transfers
     */
    static void setSuspendScheduler(bool isSuspended);

    /**
     * Gets all transfers
     */
    static QList<TransferHandler *> allTransfers();

    /**
     * Gets all transfer-groups
     */
    static QList<TransferGroupHandler *> allTransferGroups();

    /**
     * Get the transfer with the given url
     * @param src the url
     */
    static TransferHandler *findTransfer(const QUrl &src);

    /**
     * Get the group with the given name
     * @param name the name
     */
    static TransferGroupHandler *findGroup(const QString &name);

    /**
     * Run this function for enabling the systemTray
     * (will be automatically done, if there is download running)
     */
    static void checkSystemTray();

    /**
     * This will be called when the settings have been changed
     */
    static void settingsChanged();

    /**
     * @return a list of the groups assigned to the filename of a transfer
     */
    static QList<TransferGroupHandler *> groupsFromExceptions(const QUrl &filename);

    /**
     * Returns @c true if sourceUrl matches any of the patterns
     */
    static bool matchesExceptions(const QUrl &sourceUrl, const QStringList &patterns);

    /**
     * Scans for all the available plugins and creates the proper
     * transfer DataSource object for transfers Containers
     *
     * @param src Source Url
     * @param type the type of the DataSource that should be created e.g. \<TransferDataSource type="search" /\>
     * this is only needed when creating a "special" TransferDataSource like the search for Urls
     * you can set additional information and the TransferDataSource will use it if it can
     * @param parent the parent QObject
     */
    static TransferDataSource *createTransferDataSource(const QUrl &src, const QDomElement &type = QDomElement(), QObject *parent = nullptr);

    /**
     * Sets the global download limit
     * @param limit the new global download limit
     */
    static void setGlobalDownloadLimit(int limit);

    /**
     * Sets the global upload limit
     * @param limit the new global upload limit
     */
    static void setGlobalUploadLimit(int limit);

    /**
     * Recalculates the global speedlimits
     */
    static void calculateGlobalSpeedLimits();

    /**
     * Recalculates the global download-limit
     */
    static void calculateGlobalDownloadLimit();

    /**
     * Recalculates the global upload-limit
     */
    static void calculateGlobalUploadLimit();

    /**
     * Shows a knotification
     * @param parent QWidget parent of the notification
     * @param eventType Notification type
     * @param text Description of the information showed by the notification
     * @param icon Pixmap showed in the notification, by default 'dialog-error'
     * @param title Notification window title
     * @param flags Notification flags
     */
    static KNotification *showNotification(QWidget *parent,
                                           const QString &eventType,
                                           const QString &text,
                                           const QString &icon = QString("dialog-error"),
                                           const QString &title = i18n("KGet"),
                                           const KNotification::NotificationFlags &flags = KNotification::CloseOnTimeout);

    static void loadPlugins();

    /**
     * Returns a download directory
     * @param preferXDGDownloadDir if true the XDG_DOWNLOAD_DIR will be taken if it is not empty
     * @note depending if the directories exist it will return them in the following order:
     * (preferXDGDownloadDirectory >) lastDirectory > XDG_DOWNLOAD_DIR
     */
    static QString generalDestDir(bool preferXDGDownloadDir = false);

private:
    KGet();

    class TransferData;

    /**
     * Scans for all the available plugins and creates the proper
     * transfer object for the given src url
     *
     * @param src the source url
     * @param dest the destination url
     * @param groupName the group name
     * @param start Specifies if the newly added transfers should be started.
     */
    static TransferHandler *
    createTransfer(const QUrl &src, const QUrl &dest, const QString &groupName = QString(), bool start = false, const QDomElement *e = nullptr);

    /**
     * Creates multiple transfers with transferData
     */
    static QList<TransferHandler *> createTransfers(const QList<TransferData> &transferData);

    static QUrl urlInputDialog();
    static QString destDirInputDialog();
    static QUrl destFileInputDialog(QString destDir = QString(), const QString &suggestedFileName = QString());

    static bool isValidSource(const QUrl &source);
    static bool isValidDestDirectory(const QString &destDir);

    static QUrl getValidDestUrl(const QUrl &destDir, const QUrl &srcUrl);

    // Plugin-related functions
    static KGetPlugin *loadPlugin(const KPluginMetaData &md);

    /**
     * Stops all downloads if there is no connection and also displays
     * a message.
     * If there is a connection, then the downloads will be started again
     */
    static void setHasNetworkConnection(bool hasConnection);

    /**
     * Deletes the given file, if possible.
     *
     * @param url The file to delete
     *
     * @return true if the file was successfully deleted: if the given url
     * is a directory or if it is not local it returns false and shows a
     * warning message.
     */
    static bool safeDeleteFile(const QUrl &url);

    // Interview models
    static TransferTreeModel *m_transferTreeModel;
    static TransferTreeSelectionModel *m_selectionModel;

    // Lists of available plugins
    static QVector<KPluginMetaData> m_pluginList;
    static QList<TransferFactory *> m_transferFactories;

    // pointer to the Main window
    static MainWindow *m_mainWindow;

    // Scheduler object
    static TransferGroupScheduler *m_scheduler;

    // pointer to the kget uiserver jobs manager
    static KUiServerJobs *m_jobManager;

    // pointer to the used TransferHistoryStore
    static TransferHistoryStore *m_store;

    static bool m_hasConnection;
};

class KGET_EXPORT KGet::TransferData
{
public:
    TransferData(const QUrl &src, const QUrl &dest, const QString &groupName = QString(), bool start = false, const QDomElement *e = nullptr);

    QUrl src;
    QUrl dest;
    QString groupName;
    bool start;
    const QDomElement *e;
};

class GenericObserver : public QObject
{
    Q_OBJECT
public:
    explicit GenericObserver(QObject *parent = nullptr);
    ~GenericObserver() override;

public Q_SLOTS:
    void groupAddedEvent(TransferGroupHandler *handler);
    void groupRemovedEvent(TransferGroupHandler *handler);
    void transfersAddedEvent(const QList<TransferHandler *> &handlers);
    void transfersRemovedEvent(const QList<TransferHandler *> &handlers);
    void transfersChangedEvent(QMap<TransferHandler *, Transfer::ChangesFlags> transfers);
    void groupsChangedEvent(QMap<TransferGroupHandler *, TransferGroup::ChangesFlags> groups);
    void transferMovedEvent(TransferHandler *, TransferGroupHandler *);

private Q_SLOTS:
    void slotSave();
    void slotAfterFinishAction();
    void slotAbortAfterFinishAction();
    void slotResolveTransferError();
    void slotNotificationClosed();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    void slotNetworkStatusChanged(bool online);
#else
    void slotNetworkStatusChanged(QNetworkInformation::Reachability reachability);
#endif

private:
    bool allTransfersFinished();

    void requestSave();

private:
    QTimer *m_save;
    QTimer *m_finishAction;
    QHash<KNotification *, TransferHandler *> m_notifications;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Silence deprecation warnings as there is no Qt 5 substitute for QNetworkConfigurationManager
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_CLANG("-Wdeprecated-declarations")
    QT_WARNING_DISABLE_GCC("-Wdeprecated-declarations")
    QNetworkConfigurationManager m_networkConfig;
    QT_WARNING_POP
#endif
};
#endif
