// Панели: дерево сцены, ползунки трансформов, склад ассетов. C++ без Q_OBJECT.
#include "panels.h"
#include "viewport.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTreeWidget>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QDir>
#include "scene.h"
#include "asset.h"

static ScWorld g_world;
static int g_worldInit = 0;
static Entity g_selected = 0;

static void ensureWorld(ConsolePanel *con) {
    if (g_worldInit) return;
    sc_init(&g_world);
    sc_spawn(&g_world, "Scene");
    sc_spawn(&g_world, "Camera");
    sc_spawn(&g_world, "Light");
    sc_spawn(&g_world, "Mesh");
    g_worldInit = 1;
    if (con) con->log("scene: 4 entities");
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

QWidget *makeHierarchy(QWidget *parent, ConsolePanel *con) {
    ensureWorld(con);
    auto *w = new QWidget(parent);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(2, 2, 2, 2);
    auto *tree = new QTreeWidget(w);
    tree->setHeaderLabels({"Hierarchy"});
    tree->setColumnCount(1);
    auto *root = new QTreeWidgetItem(tree, {"Scene"});
    Entity list[64];
    int n = sc_list(&g_world, list, 64);
    for (int i = 0; i < n; i++) {
        const char *nm = sc_name(&g_world, list[i]);
        if (!strcmp(nm, "Scene")) continue;
        auto *it = new QTreeWidgetItem(root, {nm});
        it->setData(0, Qt::UserRole, (qlonglong)list[i]);
        it->setCheckState(0, sc_visible(&g_world, list[i]) ? Qt::Checked : Qt::Unchecked);
    }
    root->setExpanded(true);
    lay->addWidget(tree);
    QObject::connect(tree, &QTreeWidget::itemClicked, [=](QTreeWidgetItem *it, int) {
        qlonglong id = it->data(0, Qt::UserRole).toLongLong();
        if (id > 0) {
            g_selected = (Entity)id;
            if (con) con->log(QString("select: %1").arg(sc_name(&g_world, g_selected)));
        }
    });
    QObject::connect(tree, &QTreeWidget::itemChanged, [=](QTreeWidgetItem *it, int) {
        qlonglong id = it->data(0, Qt::UserRole).toLongLong();
        if (id > 0) sc_show(&g_world, (Entity)id, it->checkState(0) == Qt::Checked);
    });
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
    auto *lay = new QFormLayout(w);
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
    auto *fov = new QSlider(Qt::Horizontal);
    fov->setRange(30, 160);
    fov->setValue(90);
    lay->addRow("fov", fov);
    QObject::connect(fov, &QSlider::valueChanged, [=](int v) {
        if (vp) vp->setFov(v / 100.0f);
        if (con) con->log(QString("fov=%1").arg(v / 100.0, 0, 'f', 2));
    });
    return w;
}

QWidget *makeAssets(QWidget *parent, ConsolePanel *con, const QString &depot) {
    auto *w = new QWidget(parent);
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
            // "<hex> <name> <size>"
            char *sp = strchr(line, ' ');
            if (!sp) continue;
            char *nm = sp + 1;
            char *sp2 = strrchr(nm, ' ');
            if (sp2) *sp2 = 0;
            char *nl = strchr(nm, '\n');
            if (nl) *nl = 0;
            QString q = QString::fromUtf8(nm);
            QString flt = search->text();
            if (!flt.isEmpty() && !q.contains(flt, Qt::CaseInsensitive)) continue;
            list->addItem(q);
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
