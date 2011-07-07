/**
 * UGENE - Integrated Bioinformatics Tools.
 * Copyright (C) 2008-2011 UniPro <ugene@unipro.ru>
 * http://ugene.unipro.ru
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "MainWindowImpl.h"
#include "DockManagerImpl.h"
#include "MDIManagerImpl.h"
#include "ShutdownTask.h"
#include "MenuManager.h"
#include "ToolBarManager.h"

#include "AboutDialogController.h"
#include "CheckUpdatesTask.h"


#include <U2Core/AppContext.h>
#include <U2Core/GUrlUtils.h>
#include <U2Core/DocumentModel.h>
#include <U2Gui/ObjectViewModel.h>

#include <U2Core/Settings.h>
#include <U2Core/Task.h>
#include <U2Core/ProjectModel.h>

#include <U2Core/DocumentSelection.h>

#include <U2Gui/GUIUtils.h>

#include <QtGui/QToolBar>
#include <QtGui/QAction>
#include <QtGui/QPainter>
#include <QtGui/QFont>
#include <QtGui/QPixmap>

#include <algorithm>


namespace U2 {

/* TRANSLATOR U2::MainWindowImpl */

#define SETTINGS_DIR QString("main_window/")

class MWStub : public QMainWindow {
public:
    MWStub(MainWindowImpl* _owner)  : owner(_owner){
        setAttribute(Qt::WA_NativeWindow);
        setAcceptDrops(true);
        //setWindowIcon(QIcon(":/ugene/images/ugene.icl"));
//        setWindowIcon(QIcon(":/ugene/images/ugene_16.png"));
    }
    virtual QMenu * createPopupMenu () {return NULL;} //todo: decide if we do really need this menu and fix it if yes?
protected:
	virtual void closeEvent(QCloseEvent* e); 
    virtual void dragEnterEvent(QDragEnterEvent *event);
    virtual void dropEvent ( QDropEvent * event );
protected:
	MainWindowImpl* owner;
};

void MWStub::closeEvent(QCloseEvent* e) {
    if (owner->getMDIManager() == NULL) {
        QMainWindow::closeEvent(e);
    } else {
        owner->runClosingTask();
        e->ignore();
    }
}

void MWStub::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasFormat(DocumentMimeData::MIME_TYPE)) {
        event->acceptProposedAction();
    }
}

void MWStub::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        QList<GUrl> urls = GUrlUtils::qUrls2gUrls(event->mimeData()->urls());
        QVariantMap hints;
        hints[ProjectLoaderHint_CloseActiveProject] = true;
        Task* t = AppContext::getProjectLoader()->openWithProjectTask(urls, hints);
        if (t) {
            AppContext::getTaskScheduler()->registerTopLevelTask(t);
            event->acceptProposedAction();
        }
    } else {
        if(event->mimeData()->hasFormat(DocumentMimeData::MIME_TYPE)) {
            const DocumentMimeData *docData = static_cast<const DocumentMimeData *>(event->mimeData());
            
            DocumentSelection ds; 
            ds.setSelection(QList<Document*>() << docData->objPtr);
            MultiGSelection ms; 
            ms.addSelection(&ds);
            foreach(GObjectViewFactory *f, AppContext::getObjectViewFactoryRegistry()->getAllFactories()) {
                if(f->canCreateView(ms)) {
                    AppContext::getTaskScheduler()->registerTopLevelTask(f->createViewTask(ms));
                    break;
                }
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// MainWindowController
//////////////////////////////////////////////////////////////////////////
MainWindowImpl::MainWindowImpl() {
	mw = NULL;
	mdi = NULL;
	menuManager = NULL;
	toolbarManager = NULL;
	mdiManager = NULL;
	dockManager = NULL;
	exitAction = NULL;
    visitWebAction = NULL;
    checkUpdateAction = NULL;
    aboutAction = NULL;
    downloadManualAction = NULL;
    shutDownInProcess = false;
    nStack = NULL;
}

MainWindowImpl::~MainWindowImpl() {
	assert(mw == NULL);
}

void MainWindowImpl::show() {
	nStack = new NotificationStack();
    createActions();
    prepareGUI();

	bool maximized =AppContext::getSettings()->getValue(SETTINGS_DIR + "maximized", false).toBool();
	QRect geom =AppContext::getSettings()->getValue(SETTINGS_DIR + "geometry", QRect()).toRect();

	if (maximized) {
		mw->showMaximized();
	} else {
		if (!geom.isNull()) {
			mw->setGeometry(geom);
		}
	    mw->show();
	}
}

void MainWindowImpl::close() {
	AppContext::getSettings()->setValue(SETTINGS_DIR + "maximized", mw->isMaximized());
	AppContext::getSettings()->setValue(SETTINGS_DIR + "geometry", mw->geometry());

    delete dockManager;	dockManager = NULL;
    delete menuManager;	menuManager = NULL;
	delete toolbarManager; toolbarManager = NULL;
    delete mdiManager;	mdiManager = NULL;
	delete nStack; nStack = NULL;
    delete mdi;	mdi = NULL;
    mw->close();
	delete mw;	mw = NULL;
}


void MainWindowImpl::createActions() {
    exitAction = new QAction(tr("mw_menu_file_exit"), this);
    exitAction->setShortcutContext(Qt::WindowShortcut);
    connect(exitAction, SIGNAL(triggered()), SLOT(sl_exitAction()));

    aboutAction = new QAction(tr("mw_menu_help_about"), this);
    aboutAction->setShortcut(QKeySequence(Qt::Key_F1));
    aboutAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(aboutAction, SIGNAL(triggered()), SLOT(sl_aboutAction()));

    visitWebAction = new QAction(tr("Visit UGENE's Web Site"), this);
    connect(visitWebAction, SIGNAL(triggered()), SLOT(sl_visitWeb()));

    checkUpdateAction = new QAction(tr("Check for Updates"), this);
    connect(checkUpdateAction, SIGNAL(triggered()), SLOT(sl_checkUpdatesAction()));

    downloadManualAction = new QAction(tr("Download user manual"), this);
    connect(downloadManualAction, SIGNAL(triggered()),SLOT(sl_downloadManualAction()));

}

void MainWindowImpl::sl_exitAction() {
	runClosingTask();
}

void MainWindowImpl::sl_aboutAction() {
    QWidget *p = qobject_cast<QWidget*>(getQMainWindow());
    AboutDialogController d(visitWebAction, p);
    d.exec();
}


void MainWindowImpl::sl_checkUpdatesAction() {
    AppContext::getTaskScheduler()->registerTopLevelTask(new CheckUpdatesTask());
}


void MainWindowImpl::setWindowTitle(const QString& title) {
    if (title.isEmpty()) {
        mw->setWindowTitle(tr("main_window_title"));
    } else {
	    mw->setWindowTitle(title + " " + tr("main_window_title") );
    }
}

void MainWindowImpl::prepareGUI() {
	mw = new MWStub(this); //todo: parents?
    mw->setObjectName("main_window");
    setWindowTitle("");

    mdi = new FixedMdiArea(mw);
    mdi->setObjectName("MDI_Area");

	mw->setCentralWidget(mdi);
	mw->setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
	mw->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    
	toolbarManager = new MWToolBarManagerImpl(mw);

	menuManager = new MWMenuManagerImpl(this, mw->menuBar());

	exitAction->setObjectName(ACTION__EXIT);
    exitAction->setParent(mw);
    menuManager->getTopLevelMenu(MWMENU_FILE)->addAction(exitAction);

    aboutAction->setObjectName(ACTION__ABOUT);
    aboutAction->setParent(mw);
    menuManager->getTopLevelMenu(MWMENU_HELP)->addAction(checkUpdateAction);
    menuManager->getTopLevelMenu(MWMENU_HELP)->addAction(downloadManualAction);
    menuManager->getTopLevelMenu(MWMENU_HELP)->addAction(visitWebAction);
    menuManager->getTopLevelMenu(MWMENU_HELP)->addAction(aboutAction);

	mdiManager = new MWMDIManagerImpl(this, mdi);

	dockManager = new MWDockManagerImpl(this);
}


void MainWindowImpl::runClosingTask() {
    if(!shutDownInProcess) {
	    AppContext::getTaskScheduler()->registerTopLevelTask(new ShutdownTask(this));
        shutDownInProcess = true;
    } else {
        QMessageBox *msgBox = new QMessageBox(getQMainWindow());
        msgBox->setWindowTitle(tr("UGENE"));
        msgBox->setText(tr("Shutdown already in process. Close UGENE immediately"));
        QPushButton *closeButton = msgBox->addButton(tr("Close"), QMessageBox::ActionRole);
        /*QPushButton *waitButton =*/ msgBox->addButton(tr("Wait"), QMessageBox::ActionRole);
        msgBox->exec();
        if(getQMainWindow()) {
            if(msgBox->clickedButton() == closeButton) {
                //QCoreApplication::exit();
                exit(0);
            }
        }
    }
}

void MainWindowImpl::sl_visitWeb() {
    GUIUtils::runWebBrowser("http://ugene.unipro.ru");
}

void MainWindowImpl::sl_downloadManualAction()
{
    GUIUtils::runWebBrowser("http://ugene.unipro.ru/downloads/UniproUGENE_UserManual.pdf");
}

QMenu* MainWindowImpl::getTopLevelMenu( const QString& sysName ) const
{
    return menuManager->getTopLevelMenu(sysName);
}

QToolBar* MainWindowImpl::getToolbar( const QString& sysName ) const
{
    return toolbarManager->getToolbar(sysName);
}

///////////////////////////////////////////////////////////////////

FixedMdiArea::FixedMdiArea(QWidget * parent) : QMdiArea(parent)
{
    setDocumentMode(true);
    setTabShape(QTabWidget::Rounded);
    setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
}

void FixedMdiArea::setViewMode( QMdiArea::ViewMode mode )
{
    if (mode == viewMode()) {
        return;
    }
    QMdiArea::setViewMode(mode);
    if (mode == QMdiArea::TabbedView) {
        //FIXME QTBUG-9293, Adding a close button to tabbed QMdiSubWindows
        QList<QTabBar*> tb = findChildren<QTabBar*>();
        assert(tb.size() == 1);
        if (tb.size() == 1) {
            tb.first()->setTabsClosable(true);
            connect(tb.first(), SIGNAL(tabCloseRequested(int)), SLOT(closeSubWindow(int)));
        }
    } else {
        //TODO QTBUG-3269: switching between TabbedView and SubWindowView does not preserve maximized window state
    }
}

void FixedMdiArea::closeSubWindow(int idx) {
    subWindowList().at(idx)->close();
}

//Workaround for QTBUG-17428: Superfluous RestoreAction for tabbed QMdiSubWindows
void FixedMdiArea::sysContextMenuAction(QAction* action) {
    if (viewMode() == QMdiArea::TabbedView && activeSubWindow())
    {
        QList<QAction*> lst = activeSubWindow()->actions();
        if (!lst.isEmpty() && action == lst.first() ) { //RestoreAction always comes before CloseAction
            //FIXME better to detect via shortcut or icon ???
            assert(action->icon().pixmap(32).toImage() == style()->standardIcon(QStyle::SP_TitleBarNormalButton).pixmap(32).toImage() );
            activeSubWindow()->showMaximized(); 
        }
    }
}

QMdiSubWindow* FixedMdiArea::addSubWindow(QWidget* widget)
{
    QMdiSubWindow* subWindow = QMdiArea::addSubWindow(widget);
    //Workaround for QTBUG-17428
    connect(subWindow->systemMenu(), SIGNAL(triggered(QAction*)), SLOT(sysContextMenuAction(QAction*)));
    return subWindow;
}

}//namespace
