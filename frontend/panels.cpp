// Панели: дерево сцены, ползунки трансформов, склад ассетов. C++ без Q_OBJECT.
#include "panels.h"
#include "viewport.h"
#include <functional>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTreeWidget>
#include <QDoubleSpinBox>
#include <QMenu>
#include <QInputDialog>
#include <QSlider>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QPushButton>
#include <QFileDialog>
#include <QTreeView>
#include <QFileSystemModel>
#include <QDir>
#include "scene.h"
#include "asset.h"

static ScWorld g_world;
static int g_worldInit = 0;
static Entity g_selected = 0;
static std::function<void()> g_hierRefresh;

void ui_hierarchy_refresh(void) {
    if (g_hierRefresh) g_hierRefresh();
}
static QDoubleSpinBox *g_spins[9] = {0};

unsigned ui_selected(void) { return g_selected; }

static void ensureWorld(ConsolePanel *con);

struct ScWorld *ui_world(void) {
    ensureWorld(nullptr);
    return &g_world;
}

static void refreshInspector(void) {
    ScTransform t = sc_get(&g_world, g_selected);
    double v[9] = {t.pos.x, t.pos.y, t.pos.z, t.rot.x, t.rot.y, t.rot.z,
                   t.scale.x, t.scale.y, t.scale.z};
    for (int i = 0; i < 9; i++) {
        if (!g_spins[i]) continue;
        g_spins[i]->blockSignals(true);
        g_spins[i]->setValue(v[i]);
        g_spins[i]->blockSignals(false);
    }
}

void ui_select(unsigned id) {
    g_selected = id;
    refreshInspector();
}

static void ensureWorld(ConsolePanel *con) {
    if (g_worldInit) return;
    sc_init(&g_world);
    // Богатая сцена по умолчанию (скелет не должен выглядеть пустым):
    // дерево в два уровня, как в референсах.
    Entity scene = sc_spawn(&g_world, "Scene");
    Entity env = sc_spawn(&g_world, "Environment");
    Entity actors = sc_spawn(&g_world, "Actors");
    Entity cam = sc_spawn(&g_world, "Camera");
    Entity light = sc_spawn(&g_world, "Light");
    Entity mesh = sc_spawn(&g_world, "Mesh");
    Entity cube = sc_spawn(&g_world, "Cube");
    Entity ground = sc_spawn(&g_world, "Ground");
    sc_parent(&g_world, env, scene);
    sc_parent(&g_world, actors, scene);
    sc_parent(&g_world, cam, env);
    sc_parent(&g_world, light, env);
    sc_parent(&g_world, mesh, actors);
    sc_parent(&g_world, cube, actors);
    sc_parent(&g_world, ground, actors);
    g_worldInit = 1;
    if (con) con->log("scene: 8 entities");
}

ConsolePanel::ConsolePanel(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(2, 2, 2, 2);
    out = new QTextEdit(this);
    out->setReadOnly(true);
    out->setFontFamily("monospace");
    lay->addWidget(out);
}

void ConsolePanel::log(const QString &s) {
    if (out) out->append(s);
}

// Перехват stderr движка в консоль панели (логи ядра — в UI, а не мимо).
// Ставится один раз из main до создания ядер.
#include <QSocketNotifier>
#include <unistd.h>
static int errPipe[2] = {-1, -1};
static QSocketNotifier *errSn = nullptr;

void console_capture_stderr(ConsolePanel *con) {
    if (errPipe[0] >= 0) return;
    if (pipe(errPipe) != 0) return;
    dup2(errPipe[1], STDERR_FILENO);
    errSn = new QSocketNotifier(errPipe[0], QSocketNotifier::Read, con);
    QObject::connect(errSn, &QSocketNotifier::activated, [con]() {
        char buf[1024];
        ssize_t n = read(errPipe[0], buf, sizeof buf - 1);
        if (n > 0) {
            buf[n] = 0;
            QString s = QString::fromUtf8(buf).trimmed();
            if (!s.isEmpty()) con->log(QString("[engine] %1").arg(s));
        }
    });
}

QWidget *makeHierarchy(QWidget *parent, ConsolePanel *con) {
    ensureWorld(con);
    auto *w = new QWidget(parent);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(2, 2, 2, 2);
    auto *tree = new QTreeWidget(w);
    tree->setHeaderLabels({"Name", "Toggle"});
    tree->setColumnCount(2);
    tree->setColumnWidth(1, 52);
    // Поиск по именам как в референсах (Outliner/Explorer).
    auto *find = new QLineEdit(w);
    find->setPlaceholderText("Search...");
    lay->addWidget(find);
    lay->addWidget(tree);
    // Футер как в референсах: "N actors (1 selected)".
    auto *foot = new QLabel(w);
    lay->addWidget(foot);
    // Перестройка из движка (дерево — вид, правда — в ScWorld).
    // Двухуровневое вложение; сущность "Scene" прячем (её место — UI-корень).
    // Фильтр: виден совпадающий или чей потомок совпал.
    std::function<bool(QTreeWidgetItem *, const QString &)> match;
    match = [&](QTreeWidgetItem *it, const QString &q) -> bool {
        bool self = q.isEmpty() || it->text(0).contains(q, Qt::CaseInsensitive);
        bool kid = false;
        for (int k = 0; k < it->childCount(); k++)
            kid = match(it->child(k), q) || kid;
        bool show = self || kid;
        it->setHidden(!show);
        if (kid) it->setExpanded(true);
        return show;
    };
    std::function<void()> rebuild = [=]() {
        tree->clear();
        auto *root = new QTreeWidgetItem(tree, {"Scene"});
        Entity list[64];
        int n = sc_list(&g_world, list, 64);
        struct Row { QTreeWidgetItem *it; Entity id; };
        Row rows[64];
        int m = 0;
        for (int i = 0; i < n && m < 64; i++) {
            const char *nm = sc_name(&g_world, list[i]);
            if (!strcmp(nm, "Scene")) continue;
            auto *it = new QTreeWidgetItem({nm});
            it->setData(0, Qt::UserRole, (qlonglong)list[i]);
            it->setCheckState(1, sc_visible(&g_world, list[i]) ? Qt::Checked : Qt::Unchecked);
            rows[m].it = it;
            rows[m].id = list[i];
            m++;
        }
        for (int i = 0; i < m; i++) {
            Entity par = sc_parent_of(&g_world, rows[i].id);
            QTreeWidgetItem *dst = root;
            if (par && strcmp(sc_name(&g_world, par), "Scene")) {
                for (int k = 0; k < m; k++)
                    if (rows[k].id == par && rows[k].it) { dst = rows[k].it; break; }
            }
            dst->addChild(rows[i].it);
        }
        root->setExpanded(true);
        // Подсветить выбранного, если жив.
        if (g_selected && sc_alive(&g_world, g_selected)) {
            for (int i = 0; i < m; i++) {
                if (rows[i].id == g_selected) {
                    tree->setCurrentItem(rows[i].it);
                    break;
                }
            }
        }
        int alive = m;
        unsigned sel = ui_selected();
        foot->setText(sel ? QString("%1 actors (1 selected)").arg(alive)
                          : QString("%1 actors").arg(alive));
        // Фильтр заново (текст поиска живёт).
        QString q = find->text();
        if (!q.isEmpty())
            for (int k = 0; k < tree->topLevelItemCount(); k++) match(tree->topLevelItem(k), q);
    };
    g_hierRefresh = rebuild;

    QObject::connect(find, &QLineEdit::textChanged, [=](const QString &q) {
        for (int k = 0; k < tree->topLevelItemCount(); k++) match(tree->topLevelItem(k), q);
    });
    QObject::connect(tree, &QTreeWidget::itemClicked, [=](QTreeWidgetItem *it, int) {
        qlonglong id = it->data(0, Qt::UserRole).toLongLong();
        if (id > 0) {
            ui_select((unsigned)id);
            if (con) con->log(QString("select: %1").arg(sc_name(&g_world, g_selected)));
        }
    });
    QObject::connect(tree, &QTreeWidget::itemChanged, [=](QTreeWidgetItem *it, int) {
        qlonglong id = it->data(0, Qt::UserRole).toLongLong();
        if (id > 0) sc_show(&g_world, (Entity)id, it->checkState(1) == Qt::Checked);
    });
    // Операции: добавить/дублировать/переименовать/удалить. Всё через движок.
    auto curId = [=]() -> Entity {
        QTreeWidgetItem *it = tree->currentItem();
        if (!it) return 0;
        qlonglong id = it->data(0, Qt::UserRole).toLongLong();
        return (id > 0 && sc_alive(&g_world, (Entity)id)) ? (Entity)id : 0;
    };
    auto afterOp = [=](const char *what, Entity keep) {
        if (keep && sc_alive(&g_world, keep)) ui_select(keep);
        else ui_select(0);
        rebuild();
        if (con) con->log(QString::fromUtf8(what));
    };
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(tree, &QTreeWidget::customContextMenuRequested, [=](const QPoint &at) {
        QTreeWidgetItem *it = tree->itemAt(at);
        if (it) tree->setCurrentItem(it);
        Entity id = curId();
        QMenu m(tree);
        m.addAction("Add Entity", [=]() {
            Entity e = sc_spawn(&g_world, "Entity");
            if (e && id) sc_parent(&g_world, e, id);
            afterOp("add: Entity", e);
        });
        if (id) {
            m.addAction("Duplicate", [=]() {
                Entity cur = curId();
                if (!cur) return;
                char nm[64];
                snprintf(nm, sizeof nm, "%s-copy", sc_name(&g_world, cur));
                Entity e = sc_spawn(&g_world, nm);
                if (e) {
                    ScTransform t = sc_get(&g_world, cur);
                    sc_set(&g_world, e, &t);
                    sc_show(&g_world, e, sc_visible(&g_world, cur));
                    Entity par = sc_parent_of(&g_world, cur);
                    if (par) sc_parent(&g_world, e, par);
                }
                afterOp("duplicate", e);
            });
            m.addAction("Rename...", [=]() {
                Entity cur = curId();
                if (!cur) return;
                bool ok = false;
                QString nn = QInputDialog::getText(tree, "Rename", "Name (без пробелов):",
                                                   QLineEdit::Normal, sc_name(&g_world, cur), &ok);
                if (!ok) return;
                nn = nn.trimmed();
                if (nn.isEmpty() || nn.contains(' ')) {
                    if (con) con->log("rename: пусто/пробел — отказ");
                    return;
                }
                sc_rename(&g_world, cur, nn.toUtf8().constData());
                afterOp("rename", cur);
            });
            m.addAction("Delete", [=]() {
                Entity cur = curId();
                if (!cur) return;
                sc_kill(&g_world, cur);
                afterOp("delete", 0);
            });
        }
        m.exec(tree->mapToGlobal(at));
    });
    rebuild();
    return w;
}


static QDoubleSpinBox *mkSpin(double v, double lo, double hi) {
    auto *s = new QDoubleSpinBox();
    s->setRange(lo, hi);
    s->setDecimals(3);
    s->setSingleStep(0.5);
    s->setValue(v);
    return s;
}

QWidget *makeInspector(QWidget *parent, Viewport *vp, ConsolePanel *con) {
    ensureWorld(con);
    auto *w = new QWidget(parent);
    auto *outer = new QVBoxLayout(w);
    outer->setContentsMargins(2, 2, 2, 2);
    // Секция Transform как в референсах (рамка + заголовок).
    auto *tg = new QGroupBox("Transform", w);
    auto *lay = new QFormLayout(tg);
    QDoubleSpinBox *px, *py, *pz, *rx, *ry, *rz, *sx, *sy, *sz;
    // Читаем текущее выбранного (или нули, если никого).
    ScTransform t = sc_get(&g_world, g_selected);
    px = mkSpin(t.pos.x, -9999, 9999);
    py = mkSpin(t.pos.y, -9999, 9999);
    pz = mkSpin(t.pos.z, -9999, 9999);
    rx = mkSpin(t.rot.x, -3.2, 3.2);
    ry = mkSpin(t.rot.y, -3.2, 3.2);
    rz = mkSpin(t.rot.z, -3.2, 3.2);
    sx = mkSpin(t.scale.x, 0.01, 999);
    sy = mkSpin(t.scale.y, 0.01, 999);
    sz = mkSpin(t.scale.z, 0.01, 999);
    lay->addRow("pos.x", px);
    lay->addRow("pos.y", py);
    lay->addRow("pos.z", pz);
    lay->addRow("rot.x", rx);
    lay->addRow("rot.y", ry);
    lay->addRow("rot.z", rz);
    lay->addRow("scale", sx);
    lay->addRow("", sy);
    lay->addRow("", sz);
    QDoubleSpinBox *all[9] = {px, py, pz, rx, ry, rz, sx, sy, sz};
    for (int i = 0; i < 9; i++) g_spins[i] = all[i];
    refreshInspector();
    auto push = [=]() {
        if (!g_selected || !sc_alive(&g_world, g_selected)) return;
        ScTransform nt;
        nt.pos = {(float)px->value(), (float)py->value(), (float)pz->value()};
        nt.rot = {(float)rx->value(), (float)ry->value(), (float)rz->value()};
        nt.scale = {(float)sx->value(), (float)sy->value(), (float)sz->value()};
        sc_set(&g_world, g_selected, &nt);
        if (con)
            con->log(QString("set %1 pos=(%2,%3,%4)")
                         .arg(sc_name(&g_world, g_selected))
                         .arg(nt.pos.x, 0, 'f', 2)
                         .arg(nt.pos.y, 0, 'f', 2)
                         .arg(nt.pos.z, 0, 'f', 2));
    };
    // Каждый спин шлёт в движок сразу (как договорились: цифры гонят сцену).
    for (auto *s : {px, py, pz, rx, ry, rz, sx, sy, sz})
        QObject::connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                         [=](double) { push(); });
    // FOV-слайдер вьюпорта (камера движка через w->fov).
    auto *cg = new QGroupBox("Camera", w);
    auto *cl = new QFormLayout(cg);
    auto *fov = new QSlider(Qt::Horizontal);
    fov->setRange(30, 160);
    fov->setValue(90);
    cl->addRow("fov", fov);
    outer->addWidget(tg);
    outer->addWidget(cg);
    outer->addStretch(1);
    QObject::connect(fov, &QSlider::valueChanged, [=](int v) {
        if (vp) vp->setFov(v / 100.0f);
        if (con) con->log(QString("fov=%1").arg(v / 100.0, 0, 'f', 2));
    });
    return w;
}

QWidget *makeAssets(QWidget *parent, ConsolePanel *con, const QString &depot) {    auto *w = new QWidget(parent);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(2, 2, 2, 2);
    auto *top = new QHBoxLayout();
    auto *search = new QLineEdit(w);
    search->setPlaceholderText("Search assets...");
    auto *imp = new QPushButton("Import", w);
    top->addWidget(search);
    top->addWidget(imp);
    lay->addLayout(top);
    auto *list = new QListWidget(w);
    // Сетка миниатюр как в референсах (не список).
    list->setViewMode(QListWidget::IconMode);
    list->setIconSize(QSize(64, 64));
    list->setResizeMode(QListWidget::Adjust);
    list->setMovement(QListWidget::Static);
    lay->addWidget(list);
    // Депо проекта из хаба (папка проекта + /depot). Движок читает по ID.
    QByteArray dep = depot.toUtf8();
    QDir().mkpath(depot);
    auto refresh = [=]() {
        list->clear();
        int n = asset_count(dep.constData());
        // Список из index.txt парсим тем же форматом что движок пишет.
        FILE *f = fopen((depot + "/index.txt").toUtf8().constData(), "rb");
        if (!f) {
            if (con && n < 0) con->log("depot: empty (import files to begin)");
            return;
        }
        char line[640];
        while (fgets(line, sizeof line, f)) {
            // "<hex> <в-депо> <размер> [исходное-имя]": показываем читаемое.
            char *sp = strchr(line, ' ');
            if (!sp) continue;
            char *nm = sp + 1;
            char *sp2 = strchr(nm, ' ');
            if (!sp2) continue;
            *sp2 = 0; // конец имени в депо
            char *rest = sp2 + 1;
            char *sz = strchr(rest, ' ');
            char *orig = nm; // фолбэк: имя в депо
            if (sz) {
                *sz = 0;
                char *on = sz + 1;
                char *nl = strchr(on, '\n');
                if (nl) *nl = 0;
                if (on[0]) orig = on;
            } else {
                char *nl = strchr(nm, '\n');
                if (nl) *nl = 0;
            }
            QString q = QString::fromUtf8(orig);
            QString flt = search->text();
            if (!flt.isEmpty() && !q.contains(flt, Qt::CaseInsensitive)) continue;
            // Миниатюра: картинки грузим, остальное — без иконки.
            // Подпись в две строки: читаемое имя + FNV-ID.
            QString full = depot + "/" + QString::fromUtf8(nm);
            QImage thumb(full);
            QString stem = QString::fromUtf8(nm);
            int dot = stem.lastIndexOf('.');
            if (dot > 0) stem = stem.left(dot);
            auto *it = new QListWidgetItem(q + "\n" + stem);
            if (!thumb.isNull())
                it->setIcon(QIcon(QPixmap::fromImage(
                    thumb.scaled(64, 64, Qt::KeepAspectRatio, Qt::FastTransformation))));
            list->addItem(it);
        }
        fclose(f);
        if (con) con->log(QString("depot: %1 entries").arg(list->count()));
    };
    QObject::connect(search, &QLineEdit::textChanged, [=]() { refresh(); });
    QObject::connect(imp, &QPushButton::clicked, [=]() {
        QString fn = QFileDialog::getOpenFileName(w, "Import asset");
        if (fn.isEmpty()) return;
        uint64_t id = asset_import(dep.constData(), fn.toUtf8().constData());
        if (con) {
            if (id) con->log(QString("imported %1 id=%2").arg(fn).arg((qlonglong)id, 16, 16, QChar('0')));
            else con->log(QString("import FAILED %1").arg(fn));
        }
        refresh();
    });
    refresh();
    return w;
}

// --- Проект: scene.txt туда-обратно ---
void scene_save(const QString &projectDir) {
    ensureWorld(nullptr);
    QString fn = projectDir + "/scene.txt";
    FILE *f = fopen(fn.toUtf8().constData(), "w");
    if (!f) return;
    // Индекс в файле = порядок списка (родители — индексами в него).
    Entity list[1024];
    int n = sc_list(&g_world, list, 1024);
    int idxOf[1025];
    memset(idxOf, -1, sizeof idxOf);
    for (int i = 0; i < n; i++) {
        int id = (int)list[i];
        if (id >= 1 && id <= 1024) idxOf[id] = i;
    }
    for (int i = 0; i < n; i++) {
        // Родитель — индексом в этом же файле (-1 корень).
        int pi = -1;
        int internal = (int)list[i] - 1;
        if (internal >= 0 && internal < 1024 && g_world.parent[internal] >= 0) {
            int pe = g_world.parent[internal] + 1;
            if (pe >= 1 && pe <= 1024) pi = idxOf[pe];
        }
        ScTransform t = sc_get(&g_world, list[i]);
        fprintf(f, "e %s %d %d %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n",
                sc_name(&g_world, list[i]), pi, sc_visible(&g_world, list[i]),
                t.pos.x, t.pos.y, t.pos.z, t.rot.x, t.rot.y, t.rot.z,
                t.scale.x, t.scale.y, t.scale.z);
    }
    fclose(f);
}

void scene_load(const QString &projectDir) {
    ensureWorld(nullptr);
    QString fn = projectDir + "/scene.txt";
    FILE *f = fopen(fn.toUtf8().constData(), "rb");
    if (!f) return; // нет файла — дефолтные сущности скелета
    // Чистим текущих.
    Entity list[1024];
    int n = sc_list(&g_world, list, 1024);
    for (int i = 0; i < n; i++) sc_kill(&g_world, list[i]);
    // Проход 1: сущности. Проход 2: родители (порядок в файле любой).
    Entity ids[1024];
    int wantP[1024];
    int nn = 0;
    char name[64];
    int pidx, vis;
    ScTransform t;
    while (nn < 1024 &&
           fscanf(f, " e %63s %d %d %f %f %f %f %f %f %f %f %f", name, &pidx, &vis,
                  &t.pos.x, &t.pos.y, &t.pos.z, &t.rot.x, &t.rot.y, &t.rot.z,
                  &t.scale.x, &t.scale.y, &t.scale.z) == 12) {
        Entity e = sc_spawn(&g_world, name);
        if (!e) break;
        sc_set(&g_world, e, &t);
        sc_show(&g_world, e, vis);
        ids[nn] = e;
        wantP[nn] = pidx;
        nn++;
    }
    fclose(f);
    for (int i = 0; i < nn; i++)
        if (wantP[i] >= 0 && wantP[i] < nn) sc_parent(&g_world, ids[i], ids[wantP[i]]);
}

// Дерево файлов проекта (только чтение, без переименований).
QWidget *makeSolution(QWidget *parent, const QString &project) {
    auto *w = new QWidget(parent);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(2, 2, 2, 2);
    auto *view = new QTreeView(w);
    auto *model = new QFileSystemModel(view);
    model->setRootPath(project);
    model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    view->setModel(model);
    view->setRootIndex(model->index(project));
    view->setHeaderHidden(true);
    for (int c = 1; c < model->columnCount(); c++) view->hideColumn(c);
    lay->addWidget(view);
    return w;
}
