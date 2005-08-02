/* This file is part of the KDE project

   Copyright (C) 2005 Dario Massarin <nekkar@libero.it>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; version 2
   of the License.
*/


#ifndef _JOBQUEUE_H
#define _JOBQUEUE_H

/**
 * @brief JobQueue class
 *
 * This class abstracts the concept of queue. A queue is, basically, a 
 * group of jobs that should be executed by the scheduler (if the queue
 * is marked as active). The scheduler will execute a maximum of n jobs
 * belonging to this queue at a time, where n can be set calling the 
 * setMaxSimultaneousJobs(int n)
 *
 */

#include <QList>

class Job;
class Scheduler;

class JobQueue
{
    public:
        enum Status {Running, Stopped};
        typedef QList<Job *>::iterator iterator;

        JobQueue(Scheduler * scheduler);
        virtual ~JobQueue();

        /**
         * Sets the JobQueue status
         *
         * @param queueStatus the new JobQueue status
         */
        void setStatus(Status queueStatus);

        /**
         * @return the jobQueue status
         */
        Status status()     {return m_status;}

        /**
         * @return the begin of the job's list
         */
        iterator begin()    {return m_jobs.begin();}

        /**
         * @return the end of the job's list
         */
        iterator end()      {return m_jobs.end();}

        /**
         * @return the last job in the job's list
         */
        Job * last()        {return m_jobs.last();}

        /**
         * @return a list with the running Jobs
         */
        const QList<Job *> runningJobs();

        /**
         * Sets the maximum number of jobs belonging to this queue that 
         * should executed simultaneously by the scheduler
         *
         * @param n The maximum number of jobs
         */
        void setMaxSimultaneousJobs(int n);

        /**
         * @return the maximum number of jobs the scheduler should ever
         * execute simultaneously (in this queue).
         */
        int maxSimultaneousJobs() const     {return m_maxSimultaneousJobs;}

    protected:
        /**
         * appends a job to the current queue
         *
         * @param job The job to append to the current queue
         */
        void append(Job * job);

        /**
         * prepends a job to the current queue
         *
         * @param job The job to prepend to the current queue
         */
        void prepend(Job * job);

        /**
         * removes a job from the current queue
         *
         * @param job The job to remove from the current queue
         */
        void remove(Job * job);

        /**
         * Moves a job in the queue. Both the given jobs must belong to this queue
         *
         * @param job The job to move
         * @param position The job after which we have to move the given job
         */
        void move(Job * job, Job * after);

        /**
         * @return the number of jobs in the queue
         */
        int size() const;

        Scheduler * scheduler()     {return m_scheduler;}

    private:
        QList<Job *> m_jobs;

        int m_maxSimultaneousJobs;

        Scheduler * m_scheduler;
        Status m_status;
};

#endif
