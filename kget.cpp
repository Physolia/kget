/***************************************************************************
*                                KGet.cpp
*                             -------------------
*
*    begin        : Tue Jan 29 2002
*    copyright    : (C) 2002 by Patrick Charbonnier
*                 : Based On Caitoo v.0.7.3 (c) 1998 - 2000, Matej Koss
*    email        : pch@freeshell.org
*    Copyright (C) 2002 Carsten Pfeiffer <pfeiffer@kde.org>
*
****************************************************************************/

/***************************************************************************
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 ***************************************************************************/

#include <qsplitter.h>
//Added by qt3to4:
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QCloseEvent>

#include <kapplication.h>
#include <kconfig.h>
#include <klocale.h>
#include <kstandarddirs.h>
#include <kwin.h>
#include <kurl.h>
#include <kurldrag.h>
#include <kaction.h>
#include <kkeydialog.h>
#include <kedittoolbar.h>
#include <kstatusbar.h>
#include <kiconloader.h>
#include <knotifyclient.h>
#include <knotifydialog.h>
#include <kfiledialog.h>

#include <qtimer.h>

#include "kget.h"
#include "core/model.h"
#include "core/transferhandler.h"
#include "conf/settings.h"
#include "conf/preferencesdialog.h"

#include "ui/sidebar.h"
#include "ui/viewscontainer.h"
#include "ui/tray.h"
#include "ui/droptarget.h"

// local defs.
enum StatusbarFields { ID_TOTAL_TRANSFERS = 1, ID_TOTAL_FILES, ID_TOTAL_SIZE,
                       ID_TOTAL_TIME         , ID_TOTAL_SPEED  };

KGet::KGet( QWidget * parent, const char * name )
    : DCOPIface( "KGet-Interface" ), KMainWindow( parent, name ),
        m_drop(0), m_dock(0)
{
    // create the model
    Model::self( this );

    // create actions
    setupActions();

    createGUI("kgetui.rc");

    m_splitter = new QSplitter(this);
    m_sidebar = new Sidebar(m_splitter, "sidebar");
    m_viewsContainer = new ViewsContainer(m_splitter);

    setCentralWidget(m_splitter);

    // restore position, size and visibility
    // MainWidget (myself)
    move( Settings::mainPosition() );
//    m_sidebar->setMinimumWidth(160);
/*    m_rightWidget->show();
    m_browserBar->show();
    m_browserBar->setMinimumHeight( 200 );*/
    //setEraseColor( palette().active().background().dark(150) );
    setMaximumHeight( 32767 );

    if ( Settings::showMain() )
        show();
    else
        hide();

    // setting up status bar
//     statusBar()->show();
//     statusBar()->insertItem( "", 0 );
//     statusBar()->insertItem( "", 1 );
//     updateStatusBar();

    // other (external) widgets creation 

    //Some of the widgets are initialized in slotDelayedInit()    

    // LogWindow
    //logWindow = new LogWindow();
    //log(i18n("Welcome to KGet2"));
    setAutoSaveSettings();

    // set window title
//    setCaption(KGETVERSION);
    setPlainCaption(i18n("KGet"));

    QTimer::singleShot( 0, this, SLOT(slotDelayedInit()) );
}

KGet::~KGet()
{
    //Save the user's transfers
    Model::save();

    slotSaveMyself();
    delete m_drop;
    delete m_dock;
    // the following call saves options set in above dtors
    Settings::writeConfig();
}


void KGet::setupActions()
{
    KActionCollection * ac = actionCollection();
    //KAction * a;
    KToggleAction * ta;

    // local - Shows a dialog asking for a new URL to down
    new KAction(i18n("&New Download..."), "filenew", "CTRL+Key_N", this,
                SLOT(slotNewTransfer()), ac, "new_transfer");

    new KAction(i18n("&Open..."), "fileopen", "CTRL+Key_O", this,
                SLOT(slotOpen()), ac, "open");

    // local - Destroys all sub-windows and exits
    KStdAction::quit(this, SLOT(slotQuit()), ac, "quit");

    new KAction(i18n("Export &Transfers List..."), 0, this,
                SLOT(slotExportTransfers()), ac, "export_transfers");

    ta = new KToggleAction(i18n("Start Downloading"), "down", 0, this, 
                           SLOT(slotDownloadToggled()), ac, "download");
    ta->setWhatsThis(i18n("<b>Start/Stop</b> the automatic download of files."));
#if KDE_IS_VERSION(3,2,91)
    KGuiItem checkedDownloadAction(i18n("Stop Downloading"), "stop");
    ta->setCheckedState( checkedDownloadAction );    
#endif
    ta->setChecked( Settings::downloadAtStartup() );

    // local - Standard configure actions
    KStdAction::preferences(this, SLOT(slotPreferences()), ac, "preferences");
    KStdAction::configureToolbars(this, SLOT( slotConfigureToolbars() ), ac, "configure_toolbars");
    KStdAction::keyBindings(this, SLOT( slotConfigureKeys() ), ac, "configure_keys");
    KStdAction::configureNotifications(this, SLOT(slotConfigureNotifications()), ac, "configure_notifications" );

/*  m_paImportText = new KAction(i18n("Import Text &File..."), 0, this, SLOT(slotImportTextFile()), ac, "import_text");
*/
    // TRANSFER ACTIONS
    new KAction( i18n("Start"), "tool_resume", "",
                 this, SLOT(slotTransfersStart()),
                 actionCollection(), "transfer_start" );

    new KAction( i18n("Stop"), "tool_pause", "",
                 this, SLOT(slotTransfersStop()),
                 actionCollection(), "transfer_stop" );

    new KAction( i18n("Delete"), "editdelete", "",
                 this, SLOT(slotTransfersDelete()),
                 actionCollection(), "transfer_remove" );

    new KAction( i18n("Open Destination"), "folder", "",
                 this, SLOT(slotTransfersOpenDest()),
                 actionCollection(), "transfer_open_dest" );

    new KAction( i18n("Show Details"), "configure", "",
                 this, SLOT(slotTransfersShowDetails()),
                 actionCollection(), "transfer_show_details" );

/*
    m_paIndividual = new KAction(i18n("&Open Individual Window"), 0, this, SLOT(slotOpenIndividual()), ac, "open_individual");
    m_paMoveToBegin = new KAction(i18n("Move to &Beginning"), 0, this, SLOT(slotMoveToBegin()), ac, "move_begin");
    m_paMoveToEnd = new KAction(i18n("Move to &End"), 0, this, SLOT(slotMoveToEnd()), ac, "move_end");

    m_paDelete = new KAction(i18n("&Delete"),"tool_delete", 0, this, SLOT(slotDeleteCurrent()), ac, "delete");
    m_paDelete->setWhatsThis(i18n("<b>Delete</b> button removes selected transfers\n" "from the list."));
    m_paRestart = new KAction(i18n("Re&start"),"tool_restart", 0, this, SLOT(slotRestartCurrent()), ac, "restart");
    m_paRestart->setWhatsThis(i18n("<b>Restart</b> button is a convenience button\n" "that simply does Pause and Resume."));

    // OPTIONS ACTIONS

    m_paExpertMode     =  new KToggleAction(i18n("&Expert Mode"),"tool_expert", 0, this, SLOT(slotToggleExpertMode()), ac, "expert_mode");
    m_paExpertMode->setWhatsThis(i18n("<b>Expert mode</b> button toggles the expert mode\n" "on and off.\n" "\n" "Expert mode is recommended for experienced users.\n" "When set, you will not be \"bothered\" by confirmation\n" "messages.\n" "<b>Important!</b>\n" "Turn it on if you are using auto-disconnect or\n" "auto-shutdown features and you want KGet to disconnect \n" "or shut down without asking."));
    m_paAutoShutdown   =  new KToggleAction(i18n("Auto-S&hutdown Mode"), "tool_shutdown", 0, this, SLOT(slotToggleAutoShutdown()), ac, "auto_shutdown");
    m_paAutoShutdown->setWhatsThis(i18n("<b>Auto shutdown</b> button toggles the auto-shutdown\n" "mode on and off.\n" "\n" "When set, KGet will quit automatically\n" "after all queued transfers are finished.\n" "<b>Important!</b>\n" "Also turn on the expert mode when you want KGet\n" "to quit without asking."));

    // VIEW ACTIONS
    
    createStandardStatusBarAction();

    m_paShowLog      = new KToggleAction(i18n("Show &Log Window"),"tool_logwindow", 0, this, SLOT(slotToggleLogWindow()), ac, "toggle_log");
    m_paShowLog->setWhatsThis(i18n("<b>Log window</b> button opens a log window.\n" "The log window records all program events that occur\n" "while KGet is running."));
    m_paDropTarget   = new KToggleAction(i18n("Drop &Target"),"tool_drop_target", 0, this, SLOT(slotToggleDropTarget()), ac, "drop_target");
    m_paDropTarget->setWhatsThis(i18n("<b>Drop target</b> button toggles the window style\n" "between a normal window and a drop target.\n" "\n" "When set, the main window will be hidden and\n" "instead a small shaped window will appear.\n" "\n" "You can show/hide a normal window with a simple click\n" "on a shaped window."));

    menuHelp = new KHelpMenu(this, KGlobal::instance()->aboutData());
    KStdAction::whatsThis(menuHelp, SLOT(contextHelpActivated()), ac, "whats_this");

    // m_paDockWindow->setWhatsThis(i18n("<b>Dock widget</b> button toggles the window style\n" "between a normal window and a docked widget.\n" "\n" "When set, the main window will be hidden and\n" "instead a docked widget will appear on the panel.\n" "\n" "You can show/hide a normal window by simply clicking\n" "on a docked widget."));
    // m_paNormal->setWhatsThis(i18n("<b>Normal window</b> button sets\n" "\n" "the window style to normal window"));    
*/
}

void KGet::slotDelayedInit()
{
    //Here we import the user's transfers.
    Model::load( locateLocal("appdata", "transfers.kgt") );

    // DropTarget
    m_drop = new DropTarget(this);
    if ( Settings::showDropTarget() || Settings::firstRun() )
        m_drop->show();
    if ( Settings::firstRun() )
        m_drop->playAnimation();

    // DockWidget
    m_dock = new Tray(this);
    m_dock->show();

    // enable dropping
    setAcceptDrops(true);

    // session management stuff
    connect(kapp, SIGNAL(saveYourself()), SLOT(slotSaveMyself()));

    // set auto-resume in kioslaverc (is there a cleaner way?)
    KConfig cfg( "kioslaverc", false, false);
    cfg.setGroup(QString::null);
    cfg.writeEntry("AutoResume", true);
    cfg.sync();

    // immediately start downloading if configured this way
    if ( Settings::downloadAtStartup() )
        slotDownloadToggled();

    // reset the FirstRun config option
    Settings::setFirstRun(false);
}

void KGet::slotNewTransfer()
{
    Model::addTransfer(KURL());
}

void KGet::slotOpen()
{
    QString filename = KFileDialog::getOpenFileName
        (0,
         "*.kgt *.torrent|" + i18n("All openable files") + " (*.kgt *.torrent)",
         this,
         i18n("Open file")
        );

    if(filename.endsWith(".kgt"))
    {
        Model::load(filename);
        return;
    }

    if(!filename.isEmpty())
        Model::addTransfer( KURL::fromPathOrURL( filename ) );
}

void KGet::slotQuit()
{
/*
    // TODO check if items in ST_RUNNING state and ask for confirmation before quitting
    if (scheduler->countRunning() > 0) {
	//include <kmessagebox.h>
        if (KMessageBox::warningYesNo(this,
                i18n("Some transfers are still running.\n"
                     "Are you sure you want to close KGet?"),
                i18n("Warning"), KStdGuiItem::yes(), KStdGuiItem::no(),
                "ExitWithActiveTransfers") == KMessageBox::No)
            return;
    }

    log(i18n("Quitting..."));

    // TODO the user want to exit, so we should save our state...
*/
    Settings::writeConfig();
    kapp->quit();
}

void KGet::slotPreferences()
{
    // an instance the dialog could be already created and could be cached, 
    // in which case you want to display the cached dialog
    if ( PreferencesDialog::showDialog( "preferences" ) ) 
        return;

    // we didn't find an instance of this dialog, so lets create it
    //TODO Re-enable this line. I have currently disabled all the settings stuff
    //becouse of problems with kconfig ported to qt4
//    PreferencesDialog * dialog = new PreferencesDialog( this, Settings::self() );

    // keep us informed when the user changes settings
/*    connect( dialog, SIGNAL(settingsChanged()), 
             this, SLOT(slotNewConfig()) );

    dialog->show();*/
}

void KGet::slotExportTransfers()
{
    QString filename = KFileDialog::getSaveFileName
        (0,
         "*.kgt|" + i18n("KGet transfer list") + " (*.kgt)",
         this,
         i18n("Export transfers")
        );

    if(!filename.isEmpty())
        Model::save(filename);
}

void KGet::slotDownloadToggled()
{
    KToggleAction * action = (KToggleAction *)actionCollection()->action("download");
    bool downloading = action->isChecked();
    //TODO find an alternative way of doing this with the model
    //schedRequestOperation( downloading ? OpRun : OpStop );
#if ! KDE_IS_VERSION(3,2,91)
    action->setText( downloading ? i18n("Stop Downloading") : i18n("Start Downloading") );
    action->setIcon( downloading ? "stop" : "down" );
#endif
    m_dock->setDownloading( downloading );
}

void KGet::slotConfigureNotifications()
{
    KNotifyDialog::configure(this);
}

void KGet::slotConfigureKeys()
{
    KKeyDialog::configure(actionCollection());
}

void KGet::slotConfigureToolbars()
{
    KEditToolbar edit( "kget_toolbar", actionCollection() );
    connect(&edit, SIGNAL( newToolbarConfig() ), this, SLOT( slotNewToolbarConfig() ));
    edit.exec();
}

void KGet::slotTransfersStart()
{
    foreach(TransferHandler * it, Model::selectedTransfers())
        it->start();
}

void KGet::slotTransfersStop()
{
    foreach(TransferHandler * it, Model::selectedTransfers())
        it->stop();
}

void KGet::slotTransfersDelete()
{
    foreach(TransferHandler * it, Model::selectedTransfers())
    {
        it->stop();
        Model::delTransfer(it);
    }
}

void KGet::slotTransfersOpenDest()
{
    QStringList openedDirs;
    foreach(TransferHandler * it, Model::selectedTransfers())
    {
        QString directory = it->dest().directory();
        if( !openedDirs.contains( directory ) )
        {
            kapp->invokeBrowser( directory );
            openedDirs.append( directory );
        }
    }
}

void KGet::slotTransfersShowDetails()
{
    foreach(TransferHandler * it, Model::selectedTransfers())
    {
        m_viewsContainer->showTransferDetails(it);
    }
}


void KGet::slotSaveMyself()
{
    // save last parameters ..
    Settings::setMainPosition( pos() );
    // .. and write config to disk
    Settings::writeConfig();
}

void KGet::slotNewToolbarConfig()
{
    createGUI();
}

void KGet::slotNewConfig()
{
    // Update here properties modified in the config dialog and not
    // parsed often by the code.. When clicking Ok or Apply of
    // PreferencesDialog, this function is called.

    if ( m_drop )
        m_drop->setShown( Settings::showDropTarget(), false );
}

void KGet::updateStatusBar()
{
//     QString transfers = i18n("Downloading %1 transfers (%2) at %3");
//     QString time = i18n("%1 remaining");
//
//     transfers = transfers.arg( 2 ).arg( "23.1MB" ).arg( "4.2kb/s" );
//     time = time.arg( "1 min 2 sec" );
//
//     statusBar()->changeItem( transfers, 0 );
//     statusBar()->changeItem( time, 1 );

/*  Transfer *item;
    QString tmpstr;

    int totalFiles = 0;
    int totalSize = 0;
    int totalSpeed = 0;
    QTime remTime;

    //FOR EACH TRANSFER ON THE TRANSFER LIST {
        item = it.current();
        if (item->getTotalSize() != 0) {
            totalSize += (item->getTotalSize() - item->getProcessedSize());
        }
        totalFiles++;
        totalSpeed += item->getSpeed();

        if (item->getRemainingTime() > remTime) {
            remTime = item->getRemainingTime();
        }
    }

    statusBar->changeItem(i18n(" Transfers: %1 ").arg( ASK TO SCHEDULER FOR NUMBER ), ID_TOTAL_TRANSFERS);
    statusBar->changeItem(i18n(" Files: %1 ").arg(totalFiles), ID_TOTAL_FILES);
    statusBar->changeItem(i18n(" Size: %1 ").arg(KIO::convertSize(totalSize)), ID_TOTAL_SIZE);
    statusBar->changeItem(i18n(" Time: %1 ").arg(remTime.toString()), ID_TOTAL_TIME);
    statusBar->changeItem(i18n(" %1/s ").arg(KIO::convertSize(totalSpeed)), ID_TOTAL_SPEED);
*/
}

void KGet::log(const QString & message, bool sb)
{
#ifdef _DEBUG
    sDebugIn <<" message= "<< message << endl;
#endif

    //The logWindow has been removed. Maybe we could implement 
    //a new one. The old one was used as follows:
    //logWindow->logGeneral(message);

    if (sb) {
        statusBar()->message(message, 1000);
    }

#ifdef _DEBUG
    sDebugOut << endl;
#endif
}

/** widget events */

void KGet::closeEvent( QCloseEvent * e )
{
    if( kapp->sessionSaving() )
        e->ignore();
    else
        hide();
}

void KGet::dragEnterEvent(QDragEnterEvent * event)
{
    event->accept(KURLDrag::canDecode(event) || Q3TextDrag::canDecode(event));
}

void KGet::dropEvent(QDropEvent * event)
{
    KURL::List list;
    QString str;

    if (KURLDrag::decode(event, list))
        Model::addTransfer(list);
    else if (Q3TextDrag::decode(event, str))
        Model::addTransfer(KURL::fromPathOrURL(str));
}


/** DCOP interface */

void KGet::addTransfers( const KURL::List& src, const QString& dest)
{
    Model::addTransfer( src, dest );
}

bool KGet::isDropTargetVisible() const
{
    return m_drop->isVisible();
}

void KGet::setDropTargetVisible( bool setVisible )
{
    if ( setVisible != Settings::showDropTarget() )
        m_drop->setShown( setVisible );
}

void KGet::setOfflineMode( bool offline )
{
    //TODO Re-enable this    
//     schedRequestOperation( offline ? OpStop : OpRun );
}

bool KGet::isOfflineMode() const
{
    //TODO Re-enable this 
//     return scheduler->isRunning();
    return false;
}

#include "kget.moc"

//BEGIN auto-disconnection 
/*
KToggleAction *m_paAutoDisconnect,
    m_paAutoDisconnect =  new KToggleAction(i18n("Auto-&Disconnect Mode"),"tool_disconnect", 0, this, SLOT(slotToggleAutoDisconnect()), ac, "auto_disconnect");
    tmp = i18n("<b>Auto disconnect</b> button toggles the auto-disconnect\n" "mode on and off.\n" "\n" "When set, KGet will disconnect automatically\n" "after all queued transfers are finished.\n" "\n" "<b>Important!</b>\n" "Also turn on the expert mode when you want KGet\n" "to disconnect without asking.");
    m_paAutoDisconnect->setWhatsThis(tmp);

    if (Settings::connectionType() != Settings::Permanent) {
        //m_paAutoDisconnect->setChecked(Settings::autoDisconnect());
    }
    setAutoDisconnect();

void KGet::slotToggleAutoDisconnect()
{
#ifdef _DEBUG
    sDebugIn << endl;
#endif

    Settings::setAutoDisconnect( !Settings::autoDisconnect() );

    if (Settings::autoDisconnect()) {
        log(i18n("Auto disconnect on."));
    } else {
        log(i18n("Auto disconnect off."));
    }
    m_paAutoDisconnect->setChecked(Settings::autoDisconnect());
    
#ifdef _DEBUG
    sDebugOut << endl;
#endif
}

void KGet::setAutoDisconnect()
{
#ifdef _DEBUG
    sDebugIn << endl;
#endif

    // disable action when we are connected permanently
    //m_paAutoDisconnect->setEnabled(Settings::connectionType() != Settings::Permanent);

#ifdef _DEBUG
    sDebugOut << endl;
#endif
}
*/
//END 

//BEGIN animations 
/*

    // Setup animation timer
    animTimer = new QTimer(this);
    animCounter = 0;
    connect(animTimer, SIGNAL(timeout()), SLOT(slotAnimTimeout()));
    
    if (Settings::useAnimation()) {
        animTimer->start(400);
    } else {
        animTimer->start(1000);
    }

    KToggleAction *m_paUseAnimation
    m_paUseAnimation->setChecked(Settings::useAnimation());
    m_paUseAnimation   =  new KToggleAction(i18n("Use &Animation"), 0, this, SLOT(slotToggleAnimation()), ac, "toggle_animation");

void KGet::slotToggleAnimation()
{
#ifdef _DEBUG
    sDebugIn << endl;
#endif

    Settings::setUseAnimation( !Settings::useAnimation() );

    if (!Settings::useAnimation() && animTimer->isActive()) {
        animTimer->stop();
        animTimer->start(1000);
        animCounter = 0;
    } else {
        animTimer->stop();
        animTimer->start(400);
    }

#ifdef _DEBUG
    sDebugOut << endl;
#endif
}

void KGet::slotAnimTimeout()
{
#ifdef _DEBUG
    //sDebugIn << endl;
#endif

    bool isTransfer;

    animCounter++;
    if (animCounter == $myTransferList$->getPhasesNum()) {
        animCounter = 0;
    }
    // update status of all items of transferList
    isTransfer = $myTransferList$->updateStatus(animCounter);

    if (this->isVisible()) {
        updateStatusBar();
    }
#ifdef _DEBUG
    //sDebugOut << endl;
#endif

}
*/
//END 

//BEGIN copy URL to clipboard 
/*
    KAction *m_paCopy,
    m_paCopy = new KAction(i18n("&Copy URL to Clipboard"), 0, this, SLOT(slotCopyToClipboard()), ac, "copy_url");
    //on updateActions() set to true if an url is selected else set to false
    m_paCopy->setEnabled(false);

void KGet::slotCopyToClipboard()
{
#ifdef _DEBUG
    //sDebugIn << endl;
#endif

    Transfer *item = CURRENT ITEM!;

    if (item) {
        QString url = item->getSrc().url();
        QClipboard *cb = QApplication::clipboard();
        cb->setText( url, QClipboard::Selection );
        cb->setText( url, QClipboard::Clipboard);
    }
    
#ifdef _DEBUG
    sDebugOut << endl;
#endif
}
*/  
//END

//BEGIN Auto saving transfer list 
/*
    // Setup autosave timer
    QTimer *autosaveTimer;      // timer for autosaving transfer list
    autosaveTimer = new QTimer(this);
    connect(autosaveTimer, SIGNAL(timeout()), SLOT(slotAutosaveTimeout()));
    setAutoSave();

void KGet::slotAutosaveTimeout()
{
#ifdef _DEBUG
    //sDebugIn << endl;
#endif

    schedRequestOperation( OpExportTransfers );

#ifdef _DEBUG
    //sDebugOut << endl;
#endif
}

void KGet::setAutoSave()
{
#ifdef _DEBUG
    sDebugIn << endl;
#endif

    autosaveTimer->stop();
    if (Settings::autoSave()) {
        autosaveTimer->start(Settings::autoSaveInterval() * 60000);
    }

#ifdef _DEBUG
    sDebugOut << endl;
#endif
}
*/
//END 

//BEGIN queue-timer-delay 
/*
//construct actions
    KRadioAction *m_paQueue, *m_paTimer, *m_paDelay;
    m_paQueue = new KRadioAction(i18n("&Queue"),"tool_queue", 0, this, SLOT(slotQueueCurrent()), ac, "queue");
    m_paTimer = new KRadioAction(i18n("&Timer"),"tool_timer", 0, this, SLOT(slotTimerCurrent()), ac, "timer");
    m_paDelay = new KRadioAction(i18n("De&lay"),"tool_delay", 0, this, SLOT(slotDelayCurrent()), ac, "delay");
    m_paQueue->setExclusiveGroup("TransferMode");
    m_paTimer->setExclusiveGroup("TransferMode");
    m_paDelay->setExclusiveGroup("TransferMode");
    tmp = i18n("<b>Queued</b> button sets the mode of selected\n" "transfers to <i>queued</i>.\n" "\n" "It is a radio button -- you can choose between\n" "three modes.");
    m_paQueue->setWhatsThis(tmp);
    tmp = i18n("<b>Scheduled</b> button sets the mode of selected\n" "transfers to <i>scheduled</i>.\n" "\n" "It is a radio button -- you can choose between\n" "three modes.");
    m_paTimer->setWhatsThis(tmp);
    tmp = i18n("<b>Delayed</b> button sets the mode of selected\n" "transfers to <i>delayed</i>." "This also causes the selected transfers to stop.\n" "\n" "It is a radio button -- you can choose between\n" "three modes.");
    m_paDelay->setWhatsThis(tmp);

    // Setup transfer timer for scheduled downloads and checkQueue()
    QTimer *transferTimer;      // timer for scheduled transfers
    transferTimer = new QTimer(this);
    connect(transferTimer, SIGNAL(timeout()), SLOT(slotTransferTimeout()));
    transferTimer->start(10000);        // 10 secs time interval

//FOLLOWING CODE WAS IN UPDATEACTIONS
    // disable all signals
    m_paQueue->blockSignals(true);
    m_paTimer->blockSignals(true);
    m_paDelay->blockSignals(true);

    // at first turn off all buttons like when nothing is selected
    m_paQueue->setChecked(false);
    m_paTimer->setChecked(false);
    m_paDelay->setChecked(false);

    m_paQueue->setEnabled(false);
    m_paTimer->setEnabled(false);
    m_paDelay->setEnabled(false);

       if ( L'ITEM E' SELECTED )
       {
            //CONTROLLA SE L'ITEM E' IL PRIMO AD ESSERE SELEZIONATO, ALTRIMENTI
	    //DEVE VEDERE SE TUTTI GLI ALTRI SELECTED HANNO LO STESSO MODO, ALTRIMENTI
	    //DISABILITANO LE AZIONI
            if (item == first_selected_item) {
                if (item->getStatus() != Transfer::ST_FINISHED) {
                    m_paQueue->setEnabled(true);
                    m_paTimer->setEnabled(true);
                    m_paDelay->setEnabled(true);

                    switch (item->getMode()) {
                    case Transfer::MD_QUEUED:
#ifdef _DEBUG
                        sDebug << "....................THE MODE  IS  MD_QUEUED " << item->getMode() << endl;
#endif
                        m_paQueue->setChecked(true);
                        break;
                    case Transfer::MD_SCHEDULED:
#ifdef _DEBUG
                        sDebug << "....................THE MODE  IS  MD_SCHEDULED " << item->getMode() << endl;
#endif
                        m_paTimer->setChecked(true);
                        break;
                    case Transfer::MD_DELAYED:
#ifdef _DEBUG
                        sDebug << "....................THE MODE  IS  MD_DELAYED " << item->getMode() << endl;
#endif
                        m_paDelay->setChecked(true);
                        break;
                    }
                }
            } else if (item->getMode() != first_item->getMode()) {
                // unset all when all selected items don't have the same mode
                m_paQueue->setChecked(false);
                m_paTimer->setChecked(false);
                m_paDelay->setChecked(false);
                m_paQueue->setEnabled(false);
                m_paTimer->setEnabled(false);
                m_paDelay->setEnabled(false);
            }
       }
    // enale all signals    
    m_paQueue->blockSignals(false);
    m_paTimer->blockSignals(false);
    m_paDelay->blockSignals(false);    
*/
//END 

//BEGIN clipboard check
/*
    QString lastClipboard (in header) = QApplication::clipboard()->text( QClipboard::Clipboard ).stripWhiteSpace();

    // Setup clipboard timer
#include <qclipboard.h>
    QTimer *clipboardTimer;     // timer for checking clipboard - autopaste function
    clipboardTimer = new QTimer(this);
    connect(clipboardTimer, SIGNAL(timeout()), SLOT(slotCheckClipboard()));
    if (Settings::autoPaste())
        clipboardTimer->start(1000);

    KToggleAction *m_paAutoPaste;   
    m_paAutoPaste =  new KToggleAction(i18n("Auto-Pas&te Mode"),"tool_clipboard", 0, this, SLOT(slotToggleAutoPaste()), ac, "auto_paste");
    tmp = i18n("<b>Auto paste</b> button toggles the auto-paste mode\n" "on and off.\n" "\n" "When set, KGet will periodically scan the clipboard\n" "for URLs and paste them automatically.");
    m_paAutoPaste->setWhatsThis(tmp);
    //m_paAutoPaste->setChecked(Settings::autoPaste());

    KAction *m_paPasteTransfer;
    (### CHG WITH schedReqOp) m_paPasteTransfer = KStdAction::paste($scheduler$, SLOT(slotPasteTransfer()), ac, "paste_transfer");
    tmp = i18n("<b>Paste transfer</b> button adds a URL from\n" "the clipboard as a new transfer.\n" "\n" "This way you can easily copy&paste URLs between\n" "applications.");
    m_paPasteTransfer->setWhatsThis(tmp);

void KGet::slotToggleAutoPaste()
{
#ifdef _DEBUG
    sDebugIn << endl;
#endif

    bool autoPaste = !Settings::autoPaste();
    Settings::setAutoPaste( autoPaste );

    if (autoPaste) {
        log(i18n("Auto paste on."));
        clipboardTimer->start(1000);
    } else {
        log(i18n("Auto paste off."));
        clipboardTimer->stop();
    }
    m_paAutoPaste->setChecked(autoPaste);

#ifdef _DEBUG
    sDebugOut << endl;
#endif
}

void KGet::slotCheckClipboard()
{
#ifdef _DEBUG
    //sDebugIn << endl;
#endif

    QString clipData = QApplication::clipboard()->text( QClipboard::Clipboard ).stripWhiteSpace();

    if (clipData != lastClipboard) {
        sDebug << "New clipboard event" << endl;

        lastClipboard = clipData;
        if ( lastClipboard.isEmpty() )
            return;

        KURL url = KURL::fromPathOrURL( lastClipboard );

        if (url.isValid() && !url.isLocalFile())
            schedRequestOperation( OpPasteTransfer );
    }

#ifdef _DEBUG
    //sDebugOut << endl;
#endif
}
*/
//END 

//BEGIN use last directory Action 
/*
KToggleAction *m_paExpertMode, *m_paUseLastDir
    m_paUseLastDir     =  new KToggleAction(i18n("&Use-Last-Folder Mode"),"tool_uselastdir", 0, this, SLOT(slotToggleUseLastDir()), ac, "use_last_dir");
    m_paUseLastDir->setWhatsThis(i18n("<b>Use last folder</b> button toggles the\n" "use-last-folder feature on and off.\n" "\n" "When set, KGet will ignore the folder settings\n" "and put all new added transfers into the folder\n" "where the last transfer was put."));
    m_paUseLastDir->setChecked(Settings::useLastDir());

void slotToggleUseLastDir();

void KGet::slotToggleUseLastDir()
{
#ifdef _DEBUG
    sDebugIn << endl;
#endif

    Settings::setUseLastDirectory( !Settings::useLastDirectory() );

    if (Settings::useLastDirectory()) {
        log(i18n("Use last folder on."));
    } else {
        log(i18n("Use last folder off."));
    }

#ifdef _DEBUG
    sDebugOut << endl;
#endif
}
*/
//END 

//BEGIN slotToggle<T> 
/*
void KGet::slotToggle<T>()
{
    bool value = !Settings::<T>();
    Settings::set<T>( value );

    if (value) {
        log(i18n("<T> mode on."));
    } else {
        log(i18n("<T> mode off."));
    }
//    m_paExpertMode->setChecked(expert);
}
*/
//END 
