#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include <qobject.h>
#include <qvaluelist.h>
#include <qdatetime.h>
#include <qstringlist.h>
#include <kurl.h>

class QString;

class KMainWidget;
class TransferList;
class Transfer;


class GlobalStatus
{
public:
	// not yet used
	QDateTime timeStamp;
	struct {
		QString interface;
		float speed;
		float maxSpeed;
		float minSpeed;
	} connection;
	struct {
		float totalSize;
		float percentage;
		int transfersNumber;
	} files;
	QStringList others;
};



class Scheduler : public QObject
{
Q_OBJECT
public:
	Scheduler(KMainWidget * _mainWidget);
	~Scheduler();
	
	enum Operation {};
	
signals:
	void addedItems(QValueList<Transfer *>);
	void removedItems(QValueList<Transfer *>);
	void changedItems(QValueList<Transfer *>);
	void clear();
	void globalStatus(GlobalStatus *);
	
public slots:
	/**
     * Just an idea for these slots: we can handle 3 cases:
     *  1) src = empty list -> means that the src url must be inserted 
     *     manually from the user (with a dialog popping up)
     *  2) destDir = empty -> means that the destDir must be inserted
     *     manually from the user (with a dialog popping up)
     *  3) destDir = QString("KGet::default") means that the destination 
     *     is the kget default
     *  In this way we can take care of every possible situation using
     *  only a function.
     */
    void slotNewURLs(const KURL::List & src, const QString& destDir);
    
    /**
     * See the above function for details
     */
	void slotNewURL(const KURL & src, const QString& destDir);
    
	void slotRemoveItems(QValueList<Transfer *>);
	void slotRemoveItem(Transfer *);
    
	void slotSetPriority(QValueList<Transfer *>, int);
	void slotSetPriority(Transfer *, int);
    
	void slotSetOperation(QValueList<Transfer *>, Operation);
	void slotSetOperation(Transfer *, Operation);
    
	void slotSetGroup(QValueList<Transfer *>, const QString &);
	void slotSetGroup(Transfer *, const QString &);

    /**
     * This slot is called from the Transfer object when its status
     * has changed
     */
    void slotTransferStatusChanged(Transfer *, int operation);

    /**
     * This function adds the transfer copied in the clipboard
     */
    void slotPasteTransfer();
    
    /**
     * Used to import the URLS from a text file
     */
    void slotImportTextFile();
        
    /**
     * KGET TRANSFERS FILE related
     */
    
    /**
     * Used to import the transfers included in a .kgt file. 
     * If ask_for_name is true the function opens a KFileDialog 
     * where you can choose the file to read from. If ask_for_name 
     * is false, the function opens the transfers.kgt file
     * placed in the application data directory.
     */
    void slotImportTransfers(bool ask_for_name = false);
    
    /**
     * Used to export all the transfers in a .kgt file. 
     * If ask_for_name is true the function opens a KFileDialog 
     * where you can choose the file to write to. If ask_for_name 
     * is false, the function saves to the transfers.kgt file
     * placed in the application data directory.
     */
    void slotExportTransfers(bool ask_for_name = false);
  
    /** 
     * This function adds the transfers included in the file location
     * calling the readTransfer function in the transferList object.
     * It checks if the file is valid.
     */
    void slotReadTransfers(const KURL & file);
    
    
private:
    
    /**
     * This function reads the transfers included in the file location
     * calling the writeTransfer function in the transferList object.
     * It checks if the file is valid.
     */
    void writeTransfers(const QString & file);
    
    /**
     * Functions used to add Transfers from URLS
     */
         
    /**
     * Called by the KMainWidget class in the dropEvent, and in the 
     * slotImportTextFile(...) function.
     * Like the function above. 
     * You can add only a Transfer, with destDir being requested with
     * a KFileDialog
     */
    void addTransfer(const QString& src);
    
    /**
     * Low level function called by addTransfers(...) and addTransfer(...).
     * It adds a Transfer. destFile must be a file, not a directory!
     */
    void addTransferEx(const KURL& url, const KURL& destFile = KURL());

    /**
     * Checks if the given url is valid or not.
     */
    bool isValidURL( const KURL& url );
    
    /**
     * Checks if the given destination dir is valid or not.
     */
    bool isValidDest( const KURL& url);
    
    void checkQueue();
    
    
    TransferList * transfers;
    KMainWidget * mainWidget;
};

#endif
