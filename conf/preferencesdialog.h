/* This file is part of the KDE project
   Copyright (C) 2004 KGet Developers < >

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; version 2
   of the License.
*/

#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <kconfigdialog.h>

class QWidget;
class KConfigSkeleton;

class KDE_EXPORT PreferencesDialog : public KConfigDialog
{
    public:
        PreferencesDialog( QWidget * parent, KConfigSkeleton * config );

    private:
        QWidget * appearance;
        QWidget * network;
        QWidget * directories;
        QWidget * advanced;
        QWidget * plugins;
};

#endif
