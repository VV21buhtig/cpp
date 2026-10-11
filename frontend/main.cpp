// Скелет редактора: хаб первым, окно редактора — по открытию проекта.
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
#include "hub.h"

// Окно редактора под проект (путь) и бэкенд из хаба (1/3/2).
static QMainWindow *openEditor(const QString &project, int backend) {
    auto *win = new QMainWindow();
    const char *be = backend == 1 ? "WebGPU" : (backend == 3 ? "Vulkan" : "OpenGL");
    win->setWindowTitle(QString("Engine Editor - %1 [%2]").arg(project).arg(be));
    win->resize(1500, 900);

    auto *con = new ConsolePanel();
    auto *file = win->menuBar()->addMenu("File");
    auto *edit = win->menuBar()->addMenu("Edit");
    auto *view = win->menuBar()->addMenu("View");
    auto *tools = win->menuBar()->addMenu("Tools");
    auto *assets = win->menuBar()->addMenu("Assets");
    auto *build = win->menuBar()->addMenu("Build");
    auto *help = win->menuBar()->addMenu("Help");
    (void)edit;
    (void)view;
    (void)tools;
    (void)assets;
    (void)build;
    (void)help;
    file->addAction("Save Project", [con, project]() {
        scene_save(project);
        con->log(QString("saved %1/scene.txt").arg(project));
    });
    help->addAction("About", [con]() { con->log("voxengine editor skeleton on libengine.so"); });

    auto *bar = win->addToolBar("main");
    Viewport *vp = nullptr;
    auto *actPlay = bar->addAction("Play");
    auto *actStop = bar->addAction("Stop");
    bar->addSeparator();
    auto *gizmos = new QActionGroup(win);
    gizmos->setExclusive(true);
    auto *gT = bar->addAction("Translate");
    auto *gR = bar->addAction("Rotate");
    auto *gS = bar->addAction("Scale");
    for (auto *g : {gT, gR, gS}) {
        g->setCheckable(true);
        gizmos->addAction(g);
    }
    gT->setChecked(true);
    QObject::connect(gizmos, &QActionGroup::triggered, [con](QAction *a) {
        con->log(QString("gizmo mode: %1 (visual only)").arg(a->text()));
    });

    auto *hier = new QDockWidget("Hierarchy", win);
    scene_load(project); // проект -> мир (до построения дерева!)
    hier->setWidget(makeHierarchy(hier, con));
    win->addDockWidget(Qt::LeftDockWidgetArea, hier);

    vp = new Viewport(win);
    // Вкладки вьюпорта как в референсах (пока одна сцена).
    auto *vpTabs = new QTabWidget(win);
    vpTabs->addTab(vp, "Viewport Scene");
    vpTabs->setTabsClosable(false);
    win->setCentralWidget(vpTabs);
    if (!vp->init(backend)) {
        con->log("viewport: engine init FAILED (see terminal)");
    } else {
        con->log(QString("viewport: engine ok (%1)").arg(be));
    }

    auto *insp = new QDockWidget("Inspector", win);
    insp->setWidget(makeInspector(insp, vp, con));
    win->addDockWidget(Qt::RightDockWidgetArea, insp);

    auto *bottom = new QDockWidget("Bottom", win);
    bottom->setTitleBarWidget(new QWidget(bottom));
    auto *tabs = new QTabWidget(bottom);
    tabs->addTab(makeAssets(tabs, con, project + "/depot"), "Asset Browser");
    tabs->addTab(con, "Console");
    auto *term = new QLabel("Terminal: use system terminal (skeleton)", bottom);
    term->setAlignment(Qt::AlignCenter);
    tabs->addTab(term, "Terminal");
    bottom->setWidget(tabs);
    win->addDockWidget(Qt::BottomDockWidgetArea, bottom);

    QObject::connect(actPlay, &QAction::triggered, [vp, con]() {
        if (vp) vp->setPlaying(true);
        con->log("play");
    });
    QObject::connect(actStop, &QAction::triggered, [vp, con]() {
        if (vp) vp->setPlaying(false);
        con->log("stop");
    });

    win->statusBar()->showMessage(
        QString("engine: libengine.so | %1 | %2").arg(be).arg(project));
    win->show();
    con->log("editor skeleton ready");
    if (getenv("ED_PLAY") && getenv("ED_PLAY")[0]) {
        vp->setPlaying(true);
        con->log("autoplay");
    }
    // Тест выбора: ED_SELECT=имя -> выбрать сущность после загрузки.
    if (getenv("ED_SELECT") && getenv("ED_SELECT")[0]) {
        QString nm = QString::fromUtf8(getenv("ED_SELECT"));
        QTimer::singleShot(500, [nm, con]() {
            extern void ui_select(unsigned);
            extern struct ScWorld *ui_world(void);
            extern int sc_list(const struct ScWorld *, unsigned *, int);
            extern const char *sc_name(const struct ScWorld *, unsigned);
            struct ScWorld *w = ui_world();
            unsigned list[64];
            int n = sc_list(w, list, 64);
            for (int i = 0; i < n; i++) {
                if (nm == QString::fromUtf8(sc_name(w, list[i]))) {
                    ui_select(list[i]);
                    con->log(QString("select test: %1").arg(nm));
                    break;
                }
            }
        });
    }
    // Тест сейва: ED_SAVE=1 -> сохранить проект через секунду.
    if (getenv("ED_SAVE") && getenv("ED_SAVE")[0]) {
        QString pr = project;
        QTimer::singleShot(1000, [pr, con]() {
            scene_save(pr);
            con->log(QString("saved %1/scene.txt").arg(pr));
        });
    }
    return win;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("voxengine-editor");
    // Тёмная тема как в референсах (UE/VSCode): палитра Fusion, без зависимостей.
    app.setStyle("Fusion");
    {
        QPalette p;
        p.setColor(QPalette::Window, QColor(45, 45, 48));
        p.setColor(QPalette::WindowText, QColor(220, 220, 220));
        p.setColor(QPalette::Base, QColor(30, 30, 30));
        p.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
        p.setColor(QPalette::Text, QColor(220, 220, 220));
        p.setColor(QPalette::Button, QColor(60, 60, 63));
        p.setColor(QPalette::ButtonText, QColor(220, 220, 220));
        p.setColor(QPalette::Highlight, QColor(0, 122, 204));
        p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        app.setPalette(p);
    }
    QMainWindow *editor = nullptr;
    HubWindow hub([&](const QString &project, int backend) {
        // Эстафета: параметры из хаба -> окно редактора.
        editor = openEditor(project, backend);
    });
    hub.show();
    // Автооткрытие для тестов: ED_PROJECT=путь ED_BACKEND=1/3/2.
    if (getenv("ED_PROJECT") && getenv("ED_PROJECT")[0]) {
        QString pr = QString::fromUtf8(getenv("ED_PROJECT"));
        int be = 2;
        if (getenv("ED_BACKEND") && getenv("ED_BACKEND")[0]) be = atoi(getenv("ED_BACKEND"));
        editor = openEditor(pr, be);
    }
    // Скриншот-тест хаба: ED_HUBSHOT=путь -> снять хаб, выйти.
    if (getenv("ED_HUBSHOT") && getenv("ED_HUBSHOT")[0]) {
        QString sp = QString::fromUtf8(getenv("ED_HUBSHOT"));
        QTimer::singleShot(1500, [sp, &hub, &app]() {
            QPixmap px = hub.grab();
            if (!px.isNull() && px.save(sp))
                fprintf(stderr, "hubshot: %s\n", sp.toUtf8().constData());
            app.quit();
        });
    }
    // Скриншот-тест редактора: ED_SHOT=путь -> снять активное окно, выйти.
    if (getenv("ED_SHOT") && getenv("ED_SHOT")[0]) {
        QString sp = QString::fromUtf8(getenv("ED_SHOT"));
        QTimer::singleShot(4000, [sp, &app, &editor]() {
            if (editor) {
                QPixmap px = editor->grab();
                if (!px.isNull() && px.save(sp))
                    fprintf(stderr, "edshot: %s\n", sp.toUtf8().constData());
            }
            app.quit();
        });
    }
    return app.exec();
}
