// Скелет редактора: окно по макету (иерархия, сцена, инспектор, ассеты).
// Qt — только кнопки. Движок — libengine.so через C API.
// Без Q_OBJECT: верстаем кодом, связи — лямбды.
#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QTabWidget>
#include <QLabel>
#include <QStatusBar>
#include <QTimer>
#include <QPixmap>
#include "panels.h"
#include "viewport.h"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("voxengine-editor");
    QMainWindow win;
    win.setWindowTitle("Engine Editor - Voxel/GL Core");
    win.resize(1500, 900);

    // Меню (заглушки честно пишут в консоль, не молчат).
    auto *con = new ConsolePanel();
    auto *file = win.menuBar()->addMenu("File");
    auto *edit = win.menuBar()->addMenu("Edit");
    auto *view = win.menuBar()->addMenu("View");
    auto *tools = win.menuBar()->addMenu("Tools");
    auto *assets = win.menuBar()->addMenu("Assets");
    auto *build = win.menuBar()->addMenu("Build");
    auto *help = win.menuBar()->addMenu("Help");
    (void)edit;
    (void)view;
    (void)tools;
    (void)assets;
    (void)build;
    (void)help;
    file->addAction("Save Project", [&]() { con->log("save: scene dump not yet (skeleton)"); });
    help->addAction("About", [&]() { con->log("voxengine editor skeleton on libengine.so"); });

    // Тулбар: транспорт + гизмо-тогглы (ВИЗУАЛЬНЫЕ, без математики — так решено).
    auto *bar = win.addToolBar("main");
    Viewport *vp = nullptr; // создадим ниже, Play нужен доступ
    auto *actPlay = bar->addAction("Play");
    auto *actStop = bar->addAction("Stop");
    bar->addSeparator();
    auto *gizmos = new QActionGroup(&win);
    gizmos->setExclusive(true);
    auto *gT = bar->addAction("Translate");
    auto *gR = bar->addAction("Rotate");
    auto *gS = bar->addAction("Scale");
    for (auto *g : {gT, gR, gS}) {
        g->setCheckable(true);
        gizmos->addAction(g);
    }
    gT->setChecked(true);
    QObject::connect(gizmos, &QActionGroup::triggered, [&](QAction *a) {
        con->log(QString("gizmo mode: %1 (visual only)").arg(a->text()));
    });

    // Лево: иерархия. Середина: сцена. Право: инспектор.
    auto *hier = new QDockWidget("Hierarchy", &win);
    hier->setWidget(makeHierarchy(hier, con));
    win.addDockWidget(Qt::LeftDockWidgetArea, hier);

    vp = new Viewport(&win);
    win.setCentralWidget(vp);
    if (!vp->init()) {
        con->log("viewport: engine init FAILED (see terminal)");
    } else {
        con->log("viewport: engine ok (libengine.so)");
    }

    auto *insp = new QDockWidget("Inspector", &win);
    insp->setWidget(makeInspector(insp, vp, con));
    win.addDockWidget(Qt::RightDockWidgetArea, insp);

    // Низ: ассеты + консоль вкладками.
    auto *bottom = new QDockWidget("Bottom", &win);
    bottom->setTitleBarWidget(new QWidget(bottom));
    auto *tabs = new QTabWidget(bottom);
    tabs->addTab(makeAssets(tabs, con), "Asset Browser");
    tabs->addTab(con, "Console");
    auto *term = new QLabel("Terminal: use system terminal (skeleton)", bottom);
    term->setAlignment(Qt::AlignCenter);
    tabs->addTab(term, "Terminal");
    bottom->setWidget(tabs);
    win.addDockWidget(Qt::BottomDockWidgetArea, bottom);

    QObject::connect(actPlay, &QAction::triggered, [&]() {
        if (vp) vp->setPlaying(true);
        con->log("play");
    });
    QObject::connect(actStop, &QAction::triggered, [&]() {
        if (vp) vp->setPlaying(false);
        con->log("stop");
    });

    win.statusBar()->showMessage("engine: libengine.so | backend: GL voxel-raster");
    win.show();
    con->log("editor skeleton ready");
    // ED_PLAY=1: сразу играть (скриншот-тест без кликов).
    if (getenv("ED_PLAY") && getenv("ED_PLAY")[0]) {
        vp->setPlaying(true);
        con->log("autoplay");
    }
    // Скриншот-тест: ED_SHOT=путь -> показать, снять, выйти (для offscreen).
    if (getenv("ED_SHOT") && getenv("ED_SHOT")[0]) {
        QString sp = QString::fromUtf8(getenv("ED_SHOT"));
        // sp — ЗНАЧЕНИЕМ: блок if кончится раньше, чем сработает таймер.
        QTimer::singleShot(2000, [sp, &win, &app, con]() {
            QPixmap px = win.grab();
            fprintf(stderr, "edshot: null=%d size=%dx%d -> %s\n", px.isNull(),
                    px.width(), px.height(), sp.toUtf8().constData());
            if (!px.isNull() && px.save(sp)) con->log(QString("shot: %1").arg(sp));
            app.quit();
        });
    }
    return app.exec();
}
