/* This file is part of the KDE project

   Copyright (C) 2005 Dario Massarin <nekkar@libero.it>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; version 2
   of the License.
*/


#ifndef TRANSFERHANDLER_H
#define TRANSFERHANDLER_H

#include "transfer.h"
#include "transfergroup.h"

class KMenu;

class TransferObserver;

/**
 * Class TransferHandler:
 *
 * --- Overview ---
 * This class is the rapresentation of a Transfer object from the views'
 * perspective (proxy pattern). In fact the views never handle directly the 
 * Transfer objects themselves (becouse this would break the model/view policy).
 * As a general rule, all the code strictly related to the views should placed 
 * here (and not in the transfer implementation).
 * Here we provide the same api available in the transfer class, but we change
 * the implementation of some methods.
 * Let's give an example about this:
 * The start() function of a specific Transfer (let's say TransferKio) is a 
 * low level command that really makes the transfer start and should therefore
 * be executed only by the scheduler.
 * The start() function in this class is implemented in another way, since
 * it requests the scheduler to execute the start command to this specific transfer.
 * At this point the scheduler will evaluate this request and execute, if possible,
 * the start() function directly in the TransferKio.
 *
 * --- Notifies about the transfer changes ---
 * When a view is interested in receiving notifies about the specific transfer
 * rapresented by this TransferHandler object, it should add itself to the list
 * of observers calling the addObserver(TransferObserver *) function. On the 
 * contrary call the delObserver(TransferObserver *) function to remove it.
 *
 * --- Interrogation about what has changed in the transfer ---
 * When a TransferObserver receives a notify about a change in the Transfer, it
 * can ask to the TransferHandler for the ChangesFlags.
 */

class TransferHandler
{
    friend class Model;
    friend class Transfer;
    friend class TransferFactory;
    friend class TransferGroupHandler;

    public:

        typedef Transfer::ChangesFlags ChangesFlags;

        TransferHandler(Transfer * transfer, Scheduler * scheduler);

        virtual ~TransferHandler();

        /**
         * Adds an observer to this Transfer
         *
         * @param observer Tthe new observer that should be added
         */
        void addObserver(TransferObserver * observer);

        /**
         * Removes an observer from this Transfer
         *
         * @param observer The observer that should be removed
         */
        void delObserver(TransferObserver * observer);

        /**
         * These are all Job-related functions
         */
        void start();
        void stop();
        void setDelay(int seconds);
        Job::Status status() const {return m_transfer->status();}
        int elapsedTime() const;
        int remainingTime() const;
        bool isResumable() const;

        /**
         * @return the transfer's group handler
         */
        TransferGroupHandler * group() const {return m_transfer->group()->handler();}

        /**
         * @return the source url
         */
        const KUrl & source() const {return m_transfer->source();}

        /**
         * @return the dest url
         */
        const KUrl & dest() const {return m_transfer->dest();}

        /**
         * @return the total size of the transfer in bytes
         */
        unsigned long totalSize() const;

        /**
         * @return the downloaded size of the transfer in bytes
         */
        unsigned long processedSize() const;

        /**
         * @return the progress percentage of the transfer
         */
        int percent() const;

        /**
         * @return the download speed of the transfer in bytes/sec
         */
        int speed() const;

        /**
         * @return a string describing the current transfer status
         */
        QString statusText() const {return m_transfer->statusText();}

        /**
         * @return a pixmap associated with the current transfer status
         */
        QPixmap statusPixmap() const {return m_transfer->statusPixmap();}

        /**
         * Returns a KMenu for the given list of transfers, populated with
         * the actions that can be executed on each transfer in the list.
         * If the list is null, it returns the KMenu associated with the 
         * this transfer.
         *
         * @param transfers the transfer list
         *
         * @return a KMenu for the given transfers
         */
        KMenu * popupMenu(QList<TransferHandler *> transfers);

        /**
         * Selects the current transfer. Selecting transfers means that all
         * the actions executed from the gui will apply also to the current
         * transfer.
         *
         * @param select if true the current transfer is selected
         *               if false the current transfer is deselected
         */
        void setSelected( bool select );

        /**
         * @returns true if the current transfer is selected
         * @returns false otherwise
         */
        bool isSelected() const;

        /**
         * Returns the changes flags
         *
         * @param observer The observer that makes this request
         */
        ChangesFlags changesFlags(TransferObserver * observer) const;

        /**
         * Resets the changes flags for a given TransferObserver
         *
         * @param observer The observer that makes this request
         */
        void resetChangesFlags(TransferObserver * observer);

    private:
        /**
         * Sets a change flag in the ChangesFlags variable.
         *
         * @param change The TransferChange flag to be set
         * @param postEvent if false the handler will not post an event to the observers,
         * if true the handler will post an event to the observers
         */
        void setTransferChange(ChangesFlags change, bool postEvent=false);

        /**
         * Posts a TransferChangedEvent to all the observers.
         */
        void postTransferChangedEvent();

        /**
         * Posts a deleteEvent to all the observers
         */
        void postDeleteEvent();

        Transfer * m_transfer;
        Scheduler * m_scheduler;

        QList<TransferObserver *> m_observers;
        QMap<TransferObserver *, ChangesFlags> m_changesFlags;
};

#endif
