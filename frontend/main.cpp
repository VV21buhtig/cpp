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
#include <QSettings>
#include <string>
#include <QStyle>
#include "panels.h"
#include "viewport.h"
#include "hub.h"
#include "scene.h"

// Окно редактора под проект (путь) и бэкенд из хаба (1/3/2).
// Темы из frontend/themes (цвета VS Code dark/light). Переключатель —
// в меню View, выбор помним в QSettings. Без картинок и тулчейнов.
static void applyTheme(QApplication &app, const QString &name) {
    QStringList roots = {"frontend/themes/" + name + ".qss",
                         QString(getenv("VOX_THEMES") ? getenv("VOX_THEMES") : "") + "/" + name + ".qss"};
    for (const QString &p : roots) {
        FILE *f = fopen(p.toUtf8().constData(), "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::string css((size_t)n, 0);
        if (fread(&css[0], 1, (size_t)n, f) == (size_t)n)
            app.setStyleSheet(QString::fromUtf8(css.c_str()));
        fclose(f);
        QSettings s("voxengine", "editor");
        s.setValue("theme", name);
        return;
    }
}

static QMainWindow *openEditor(const QString &project, int backend) {
    auto *win = new QMainWindow();    const char *be = backend == 1 ? "WebGPU" : (backend == 3 ? "Vulkan" : "OpenGL");
    win->setWindowTitle(QString("Engine Editor - %1 [%2]").arg(project).arg(be));
    win->resize(1500, 900);

    auto *con = new ConsolePanel();
    console_capture_stderr(con); // stderr движка -> вкладка Console
    auto *file = win->menuBar()->addMenu("File");
    auto *edit = win->menuBar()->addMenu("Edit");
    auto *view = win->menuBar()->addMenu("View");
    view->addAction("Theme: Dark", []() {
        if (auto *a = qobject_cast<QApplication *>(QApplication::instance())) applyTheme(*a, "dark");
    });
    view->addAction("Theme: Light", []() {
        if (auto *a = qobject_cast<QApplication *>(QApplication::instance())) applyTheme(*a, "light");
    });
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

    // Крайняя левая панель активности: компактные кнопки-буквы 40px
    // (без картинок и эмодзи), активная — с левой синей полосой через QSS.
    auto *activity = new QToolBar("activity", win);
    activity->setOrientation(Qt::Vertical);
    activity->setIconSize(QSize(40, 40));
    activity->setStyleSheet(
        "QToolBar { background: #252526; border: none; }"
        "QToolButton { min-width: 40px; min-height: 40px; font-weight: bold; font-size: 14px; color: #cccccc; border: none; }"
        "QToolButton:checked { color: white; border-left: 3px solid #007ACC; background: #094771; }");
    win->addToolBar(Qt::LeftToolBarArea, activity);
    activity->addWidget(new QLabel("API", win));
    auto *apiGroup = new QActionGroup(win);
    apiGroup->setExclusive(true);
    auto *apiW = activity->addAction("W");
    auto *apiG = activity->addAction("G");
    auto *apiV = activity->addAction("V");
    apiW->setToolTip("WebGPU");
    apiG->setToolTip("OpenGL");
    apiV->setToolTip("Vulkan");
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
    win->setProperty("viewport", (qulonglong)vp);
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
    // Пропорции как в референсах: центр максимум, низ компактно.
    win->resizeDocks({bottomL, bottomR}, {220, 220}, Qt::Vertical);

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
    QMainWindow *editor = nullptr;
    {
        QSettings s("voxengine", "editor");
        QString theme = s.value("theme", "dark").toString();
        // Тест тем: ED_THEME=light|dark перебивает запомненную.
        if (getenv("ED_THEME") && getenv("ED_THEME")[0])
            theme = QString::fromUtf8(getenv("ED_THEME"));
        applyTheme(app, theme);
    }
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
    // Матричный прогон: ED_MATRIX=1 -> бэкенды по 30с каждый с вердиктом.
    // Окна видно вживую (без offscreen), орбита крутится сама.
    // Вердикты в stdout с flush (stderr в этой среде теряется) + кадры фаз.
    printf("matrix: setup editor=%d\n", !!editor);
    fflush(stdout);
    if (getenv("ED_MATRIX") && getenv("ED_MATRIX")[0] && editor) {
        Viewport *mvp = (Viewport *)editor->property("viewport").toULongLong();
        struct Mx {
            Viewport *vp;
            int phases[3];
            const char *names[3];
            int pi;
            int f0;
        };
        auto *mx = new Mx{mvp, {2, 3, 1}, {"OpenGL", "Vulkan", "WebGPU"}, 0, 0};
        if (mvp) {
            mvp->setPlaying(true);
            mx->f0 = mvp->frameCount();
        }
        auto *mt = new QTimer(editor);
        QObject::connect(mt, &QTimer::timeout, [mx, &app, editor]() mutable {
            if (mx->vp) {
                int f1 = mx->vp->frameCount();
                bool adv = f1 > mx->f0;
                bool pic = mx->vp->hasPicture();
                printf("matrix %s: frames %d->%d %s %s\n", mx->names[mx->pi], mx->f0, f1,
                       adv ? "ADVANCE-OK" : "STUCK-FAIL",
                       pic ? "PICTURE-OK" : "BLACK-FAIL");
                fflush(stdout);
                // Кадр фазы для доказательства (ED_MATRIX_SHOTDIR=/tmp/x).
                if (getenv("ED_MATRIX_SHOTDIR") && getenv("ED_MATRIX_SHOTDIR")[0] && editor) {
                    QString sp = QString("%1/phase%2-%3.png")
                                     .arg(getenv("ED_MATRIX_SHOTDIR"))
                                     .arg(mx->pi)
                                     .arg(mx->names[mx->pi]);
                    QPixmap px = editor->grab();
                    if (!px.isNull() && px.save(sp)) {
                        printf("matrix shot: %s\n", sp.toUtf8().constData());
                        fflush(stdout);
                    }
                }
            }
            mx->pi++;
            if (mx->pi >= 3) {
                printf("matrix DONE\n");
                fflush(stdout);
                app.quit();
                return;
            }
            if (mx->vp && mx->vp->switchBackend(mx->phases[mx->pi])) {
                mx->vp->setPlaying(true);
                mx->f0 = mx->vp->frameCount();
            }
        });
        mt->start(getenv("ED_MATRIX_MS") && getenv("ED_MATRIX_MS")[0]
                      ? atoi(getenv("ED_MATRIX_MS"))
                      : 30000);
    }
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
