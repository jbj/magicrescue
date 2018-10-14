/*
 *  This file is part of the KDE Help Center
 *
 *  Copyright (C) 2002 Frerich Raabe <raabe@kde.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef HISTORY_H
#define HISTORY_H

#include <kurl.h>

#include <qobject.h>
#include <qptrlist.h>

class KActionCollection;
class KMainWindow;
class KToolBarPopupAction;
class QPopupMenu;

namespace KHC {

class View;

class History : public QObject
{
	Q_OBJECT
	public:
		friend class foo; // to make gcc shut up
		struct Entry
		{
			View *view;
			KURL url;
			QString title;
			QByteArray buffer;
                        bool search;
		};

		static History &self();

		void setupActions( KActionCollection *coll );
		void updateActions();

		void installMenuBarHook( KMainWindow *mainWindow );

		void createEntry();
		void updateCurrentEntry( KHC::View *view );

	private slots:
		void backActivated( int id );
		void fillBackMenu();
		void forwardActivated( int id );
		void fillForwardMenu();
		void goMenuActivated( int id );
		void fillGoMenu();
		void back();
		void forward();
		void goHistoryActivated( int steps );
		void goHistory( int steps );
		void goHistoryDelayed();

	private:
		History();
		History( const History &rhs );
		History &operator=( const History &rhs );
		~History();

		bool canGoBack() const;
		bool canGoForward() const;
		void fillHistoryPopup( QPopupMenu *, bool, bool, bool, uint = 0 );

		static History *m_instance;

		QPtrList<Entry> m_entries;

		KToolBarPopupAction *m_backAction;
		KToolBarPopupAction *m_forwardAction;

		int m_goBuffer;
		int m_goMenuIndex;
		int m_goMenuHistoryStartPos;
		int m_goMenuHistoryCurrentPos;
};

}

#endif // HISTORY_H
// vim:ts=2:sw=2:et
