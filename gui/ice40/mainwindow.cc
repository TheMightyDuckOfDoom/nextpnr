#include "mainwindow.h"
#include <QAction>
#include <QFileDialog>
#include <QIcon>
#include "bitstream.h"
#include "design_utils.h"
#include "jsonparse.h"
#include "log.h"
#include "pack.h"
#include "pcf.h"
#include "place_sa.h"
#include "route.h"

static void initMainResource() { Q_INIT_RESOURCE(nextpnr); }

NEXTPNR_NAMESPACE_BEGIN

MainWindow::MainWindow(Context *_ctx, QWidget *parent)
        : BaseMainWindow(_ctx, parent)
{
    initMainResource();

    std::string title = "nextpnr-ice40 - " + ctx->getChipName();
    setWindowTitle(title.c_str());

    task = new TaskManager(_ctx);
    connect(task, SIGNAL(log(std::string)), this, SLOT(writeInfo(std::string)));

    connect(task, SIGNAL(loadfile_finished(bool)), this, SLOT(loadfile_finished(bool)));
    connect(task, SIGNAL(pack_finished(bool)), this, SLOT(pack_finished(bool)));
    connect(task, SIGNAL(place_finished(bool)), this, SLOT(place_finished(bool)));
    connect(task, SIGNAL(route_finished(bool)), this, SLOT(route_finished(bool)));

    connect(task, SIGNAL(taskCanceled()), this, SLOT(taskCanceled()));
    connect(task, SIGNAL(taskStarted()), this, SLOT(taskStarted()));
    connect(task, SIGNAL(taskPaused()), this, SLOT(taskPaused()));    

    createMenu();
}

MainWindow::~MainWindow() { delete task; }

void MainWindow::createMenu()
{
    QMenu *menu_Design = new QMenu("&Design", menuBar);
    menuBar->addAction(menu_Design->menuAction());

    actionPack = new QAction("Pack", this);
    QIcon iconPack;
    iconPack.addFile(QStringLiteral(":/icons/resources/pack.png"));
    actionPack->setIcon(iconPack);
    actionPack->setStatusTip("Pack current design");
    connect(actionPack, SIGNAL(triggered()), task, SIGNAL(pack()));
    actionPack->setEnabled(false);

    actionPlace = new QAction("Place", this);
    QIcon iconPlace;
    iconPlace.addFile(QStringLiteral(":/icons/resources/place.png"));
    actionPlace->setIcon(iconPlace);
    actionPlace->setStatusTip("Place current design");
    connect(actionPlace, SIGNAL(triggered()), task, SIGNAL(place()));
    actionPlace->setEnabled(false);

    actionRoute = new QAction("Route", this);
    QIcon iconRoute;
    iconRoute.addFile(QStringLiteral(":/icons/resources/route.png"));
    actionRoute->setIcon(iconRoute);
    actionRoute->setStatusTip("Route current design");
    connect(actionRoute, SIGNAL(triggered()), task, SIGNAL(route()));
    actionRoute->setEnabled(false);

    QToolBar *taskFPGABar = new QToolBar();
    addToolBar(Qt::TopToolBarArea, taskFPGABar);

    taskFPGABar->addAction(actionPack);
    taskFPGABar->addAction(actionPlace);
    taskFPGABar->addAction(actionRoute);

    menu_Design->addAction(actionPack);
    menu_Design->addAction(actionPlace);
    menu_Design->addAction(actionRoute);

    actionPlay = new QAction("Play", this);
    QIcon iconPlay;
    iconPlay.addFile(QStringLiteral(":/icons/resources/control_play.png"));
    actionPlay->setIcon(iconPlay);
    actionPlay->setStatusTip("Continue running task");
    connect(actionPlay, SIGNAL(triggered()), task, SLOT(continue_thread()));
    actionPlay->setEnabled(false);

    actionPause = new QAction("Pause", this);
    QIcon iconPause;
    iconPause.addFile(QStringLiteral(":/icons/resources/control_pause.png"));
    actionPause->setIcon(iconPause);
    actionPause->setStatusTip("Pause running task");
    connect(actionPause, SIGNAL(triggered()), task, SLOT(pause_thread()));
    actionPause->setEnabled(false);

    actionStop = new QAction("Stop", this);
    QIcon iconStop;
    iconStop.addFile(QStringLiteral(":/icons/resources/control_stop.png"));
    actionStop->setIcon(iconStop);
    actionStop->setStatusTip("Stop running task");
    connect(actionStop, SIGNAL(triggered()), task, SLOT(terminate_thread()));
    actionStop->setEnabled(false);

    QToolBar *taskToolBar = new QToolBar();
    addToolBar(Qt::TopToolBarArea, taskToolBar);

    taskToolBar->addAction(actionPlay);
    taskToolBar->addAction(actionPause);
    taskToolBar->addAction(actionStop);
}

void MainWindow::open()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString(), QString(),
                                                    QString("*.json"));
    if (!fileName.isEmpty()) {
        tabWidget->setCurrentWidget(info);

        std::string fn = fileName.toStdString();
        disableActions();
        Q_EMIT task->loadfile(fn);
    }
}

bool MainWindow::save() { return false; }

void  MainWindow::disableActions()
{
    actionPack->setEnabled(false);
    actionPlace->setEnabled(false);
    actionRoute->setEnabled(false);

    actionPlay->setEnabled(false);
    actionPause->setEnabled(false);
    actionStop->setEnabled(false);
}

void MainWindow::loadfile_finished(bool status)
{
    disableActions();
    if (status) {
        log("Loading design successful.\n");
        actionPack->setEnabled(true);
    }
    else {
        log("Loading design failed.\n");
    }
}
void MainWindow::pack_finished(bool status)
{
    disableActions();
    if (status) {
        log("Packing design successful.\n");
        actionPlace->setEnabled(true);
    }
    else {
        log("Packing design failed.\n");
    }
}
void MainWindow::place_finished(bool status)
{
    disableActions();
    if (status) {
        log("Placing design successful.\n");
        actionRoute->setEnabled(true);
    }
    else {
        log("Placing design failed.\n");
    }
}
void MainWindow::route_finished(bool status)
{
    disableActions();
    if (status)
        log("Routing design successful.\n");
    else
        log("Routing design failed.\n");
}

void MainWindow::taskCanceled()
{
    log("CANCELED\n");
    disableActions();
}

void MainWindow::taskStarted()
{
    disableActions();
    actionPause->setEnabled(true);
    actionStop->setEnabled(true);
}

void MainWindow::taskPaused()
{
    disableActions();
    actionPlay->setEnabled(true);
    actionStop->setEnabled(true);    
}

NEXTPNR_NAMESPACE_END