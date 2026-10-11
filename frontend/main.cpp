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
#include <QStyle>
#include "panels.h"
#include "viewport.h"
#include "hub.h"
#include "scene.h"

// Окно редактора под проект (путь) и бэкенд из хаба (1/3/2).
static QMainWindow *openEditor(const QString &project, int backend) {
    auto *win = new QMainWindow();    const char *be = backend == 1 ? "WebGPU" : (backend == 3 ? "Vulkan" : "OpenGL");
    win->setWindowTitle(QString("Engine Editor - %1 [%2]").arg(project).arg(be));
    win->resize(1500, 900);

    auto *con = new ConsolePanel();
    console_capture_stderr(con); // stderr движка -> вкладка Console
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
    // Текстовые кнопки как в референсах (стандартные иконки Qt —
    // цветастые эмодзи, нам не надо).
    Viewport *vp = nullptr;
    auto *actPlay = bar->addAction("Play");
    auto *actStop = bar->addAction("Stop");
    bar->addSeparator();
    auto *actSave = bar->addAction("Save");
    auto *actNew = bar->addAction("New");
    bar->addSeparator();
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
    // Орто-виды как в референсах (фиксированные камеры вьюпорта).
    bar->addSeparator();
    auto *vTop = bar->addAction("Top");
    auto *vSide = bar->addAction("Side");
    auto *vFront = bar->addAction("Front");
    auto *vOrbit = bar->addAction("Orbit");

    // Крайняя левая панель активности (как в референсах): только API —
    // единственный живой переключатель. Остальное позже, не врём.
    auto *activity = new QToolBar("activity", win);
    activity->setOrientation(Qt::Vertical);
    win->addToolBar(Qt::LeftToolBarArea, activity);
    activity->addWidget(new QLabel("API", win));
    auto *apiGroup = new QActionGroup(win);
    apiGroup->setExclusive(true);
    auto *apiW = activity->addAction("WebGPU");
    auto *apiG = activity->addAction("OpenGL");
    auto *apiV = activity->addAction("Vulkan");
    for (auto *a : {apiW, apiG, apiV}) {
        a->setCheckable(true);
        apiGroup->addAction(a);
    }
    if (backend == 1) apiW->setChecked(true);
    else if (backend == 3) apiV->setChecked(true);
    else apiG->setChecked(true);
    auto apiSwitch = [vp, con](int be, const char *nm) {
        if (!vp) return;
        if (vp->switchBackend(be)) con->log(QString("api: %1").arg(nm));
        else con->log(QString("api %1 FAILED (see terminal)").arg(nm));
    };
    QObject::connect(apiW, &QAction::triggered, [=]() { apiSwitch(1, "webgpu"); });
    QObject::connect(apiG, &QAction::triggered, [=]() { apiSwitch(2, "opengl"); });
    QObject::connect(apiV, &QAction::triggered, [=]() { apiSwitch(3, "vulkan"); });

    auto *hier = new QDockWidget("Hierarchy & Outliner", win);
    hier->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    scene_load(project); // проект -> мир (до построения дерева!)
    hier->setWidget(makeHierarchy(hier, con));
    win->addDockWidget(Qt::LeftDockWidgetArea, hier);

    vp = new Viewport(win);
    // Вкладки вьюпорта как в референсах (пока одна сцена).
    auto *vpTabs = new QTabWidget(win);
    vpTabs->addTab(vp, "Viewport Scene");
    vpTabs->setTabsClosable(true);
    win->setCentralWidget(vpTabs);
    if (!vp->init(backend)) {
        con->log("viewport: engine init FAILED (see terminal)");
    } else {
        con->log(QString("viewport: engine ok (%1)").arg(be));
    }

    // Право: вверху Solution Explorer (файлы проекта), внизу Details.
    auto *sol = new QDockWidget("Solution Explorer", win);
    sol->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                     QDockWidget::DockWidgetFloatable);
    sol->setWidget(makeSolution(sol, project));
    win->addDockWidget(Qt::RightDockWidgetArea, sol);
    auto *insp = new QDockWidget("Details", win);
    insp->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    insp->setWidget(makeInspector(insp, vp, con));
    win->addDockWidget(Qt::RightDockWidgetArea, insp);

    // Низ-А: терминал (вкладки Console/Terminal). Низ-Б: ассеты отдельно.
    auto *bottomL = new QDockWidget("Terminal", win);
    bottomL->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                         QDockWidget::DockWidgetFloatable);
    bottomL->setTitleBarWidget(new QWidget(bottomL));
    auto *tabs = new QTabWidget(bottomL);
    tabs->setTabsClosable(true);
    tabs->addTab(con, "Console");
    auto *term = new QLabel("Terminal: use system terminal (skeleton)", bottomL);
    term->setAlignment(Qt::AlignCenter);
    tabs->addTab(term, "Terminal");
    bottomL->setWidget(tabs);
    win->addDockWidget(Qt::BottomDockWidgetArea, bottomL);
    auto *bottomR = new QDockWidget("Asset Browser", win);
    bottomR->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                         QDockWidget::DockWidgetFloatable);
    bottomR->setWidget(makeAssets(bottomR, con, project + "/depot"));
    win->addDockWidget(Qt::BottomDockWidgetArea, bottomR);

    QObject::connect(actPlay, &QAction::triggered, [vp, con]() {
        if (vp) vp->setPlaying(true);
        con->log("play");
    });
    QObject::connect(actStop, &QAction::triggered, [vp, con]() {
        if (vp) vp->setPlaying(false);
        con->log("stop");
    });
    QObject::connect(actSave, &QAction::triggered, [con, project]() {
        scene_save(project);
        con->log(QString("saved %1/scene.txt").arg(project));
    });
    QObject::connect(actNew, &QAction::triggered, [con]() {
        struct ScWorld *w = ui_world();
        unsigned list[1024];
        int n = sc_list(w, list, 1024);
        for (int i = 0; i < n; i++) sc_kill(w, list[i]);
        sc_spawn(w, "Scene");
        sc_spawn(w, "Camera");
        sc_spawn(w, "Light");
        sc_spawn(w, "Mesh");
        ui_select(0);
        con->log("new scene: 4 defaults");
    });
    auto setView = [vp, con](int m, const char *nm) {
        if (vp) vp->setViewMode(m);
        con->log(QString("view: %1").arg(nm));
    };
    QObject::connect(vTop, &QAction::triggered, [=]() { setView(1, "top"); });
    QObject::connect(vSide, &QAction::triggered, [=]() { setView(2, "side"); });
    QObject::connect(vFront, &QAction::triggered, [=]() { setView(3, "front"); });
    QObject::connect(vOrbit, &QAction::triggered, [=]() { setView(0, "orbit"); });

    win->statusBar()->showMessage(
        QString("engine: libengine.so | %1 | %2").arg(be).arg(project));
    win->show();
    con->log("editor skeleton ready");
    // Тест видов: ED_VIEW=0..3 (орбита/верх/бок/фронт).
    if (getenv("ED_VIEW") && getenv("ED_VIEW")[0]) {
        vp->setViewMode(atoi(getenv("ED_VIEW")));
        con->log(QString("view mode %1").arg(getenv("ED_VIEW")));
    }
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
    // Рамки панелей и вкладки с крестиками как в референсах.
    // Никаких картинок: только текст, границы и отступы.
    app.setStyle("Fusion");
    app.setStyleSheet(
        "QDockWidget { border: 1px solid #2a2a2e; }"
        "QDockWidget::title { background: #2d2d30; padding: 4px; border-bottom: 1px solid #2a2a2e; }"
        "QTabWidget::pane { border: 1px solid #2a2a2e; }"
        "QTabBar::tab { background: #2d2d30; border: 1px solid #2a2a2e; padding: 4px 10px; }"
        "QTabBar::tab:selected { background: #3e3e42; }"
        "QGroupBox { border: 1px solid #2a2a2e; margin-top: 14px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
        "QTreeView, QListView, QTextEdit { border: 1px solid #2a2a2e; }"
        "QToolBar { border-bottom: 1px solid #2a2a2e; spacing: 4px; }"
        "QStatusBar { border-top: 1px solid #2a2a2e; }");
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
