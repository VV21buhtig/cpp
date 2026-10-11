// Хаб: сайдбар BROWSE/LIBRARY + сетка карточек проектов.
// Карточка: миниатюра (первый PNG депо) + имя + версия + дата.
// Без Q_OBJECT.
#include "hub.h"
#include "../engine/render_api.h" // RC_BACKEND_* (типы, не линковка)
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QDateTime>

// Индекс комбо -> значение бэкенда (0 WebGPU, 1 Vulkan, 2 OpenGL в списке).
static int hubBackendValue(int idx) {
    if (idx == 1) return 3; // Vulkan
    if (idx == 2) return 2; // OpenGL
    return 1;               // WebGPU
}

// "2 days ago" из mtime.
static QString agoStr(const QString &path) {
    QFileInfo fi(path);
    if (!fi.exists()) return "never";
    qint64 s = fi.lastModified().secsTo(QDateTime::currentDateTime());
    if (s < 60) return "just now";
    if (s < 3600) return QString("%1 min ago").arg(s / 60);
    if (s < 86400) return QString("%1 hours ago").arg(s / 3600);
    return QString("%1 days ago").arg(s / 86400);
}

static QString projVersion(const QString &path) {
    FILE *f = fopen((path + "/project.txt").toUtf8().constData(), "rb");
    if (!f) return "0.1.0";
    char line[256];
    QString v = "0.1.0";
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "version=", 8)) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = 0;
            v = QString::fromUtf8(line + 8);
            break;
        }
    }
    fclose(f);
    return v;
}

// Миниатюра проекта: первый PNG из depot, иначе буква на тёмном.
static QIcon projThumb(const QString &path, const QString &name) {
    QDir d(path + "/depot");
    QStringList pngs = d.entryList({"*.png"}, QDir::Files);
    if (!pngs.isEmpty()) {
        QImage im(d.filePath(pngs.first()));
        if (!im.isNull())
            return QIcon(QPixmap::fromImage(
                im.scaled(200, 140, Qt::KeepAspectRatioByExpanding, Qt::FastTransformation)));
    }
    QImage ph(200, 140, QImage::Format_RGB32);
    ph.fill(QColor(45, 45, 48));
    return QIcon(QPixmap::fromImage(ph));
}

HubWindow::HubWindow(OpenFn onOpen, QWidget *parent)
    : QWidget(parent), onOpen(onOpen) {
    setWindowTitle("Engine Hub - Projects");
    resize(1100, 650);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    // Лево: фиксированный сайдбар.
    auto *side = new QWidget(this);
    side->setFixedWidth(180);
    side->setStyleSheet("background: #252526;");
    auto *sl = new QVBoxLayout(side);
    auto mkHead = [=](const char *t) -> void {
        auto *l = new QLabel(t, side);
        l->setStyleSheet("color: #888888; padding: 8px 8px 2px 8px;");
        sl->addWidget(l);
    };
    auto mkItem = [=](const char *t, bool active) -> QPushButton * {
        auto *b = new QPushButton(t, side);
        b->setFlat(true);
        b->setStyleSheet(active
                             ? "text-align: left; padding: 8px; background: #094771; color: white;"
                             : "text-align: left; padding: 8px; color: #cccccc;");
        sl->addWidget(b);
        return b;
    };
    mkHead("BROWSE");
    mkItem("Projects", true);
    mkItem("Templates", false);
    mkHead("LIBRARY");
    mkItem("Assets", false);
    mkItem("Samples", false);
    mkItem("Tutorials", false);
    sl->addStretch(1);
    lay->addWidget(side);
    // Право: верхняя панель + сетка.
    auto *main = new QWidget(this);
    auto *ml = new QVBoxLayout(main);
    auto *top = new QHBoxLayout();
    auto *btnNew = new QPushButton("+ New Project", main);
    btnNew->setStyleSheet("background: #00b3b3; color: white; padding: 6px 14px;");
    top->addWidget(btnNew);
    top->addWidget(new QLabel("Render API:", main));
    apiBox = new QComboBox(main);
    apiBox->addItems({"WebGPU", "Vulkan", "OpenGL"});
    top->addWidget(apiBox);
    top->addStretch(1);
    // Иконки справа: поиск, шестерёнка, профиль (текстом, без эмодзи).
    auto *btnFind = new QPushButton("Search", main);
    auto *btnGear = new QPushButton("Settings", main);
    auto *btnProf = new QPushButton("Profile", main);
    for (auto *b : {btnFind, btnGear, btnProf}) {
        b->setFlat(true);
        top->addWidget(b);
    }
    ml->addLayout(top);
    // Форма создания (компактно под верхней панелью).
    auto *form = new QHBoxLayout();
    nameEdit = new QLineEdit(main);
    nameEdit->setPlaceholderText("Project name...");
    tplBox = new QComboBox(main);
    tplBox->addItems({"Empty", "Voxel Terrain", "SDF Primitives", "Vector Shapes"});
    auto *btnCreate = new QPushButton("Create", main);
    form->addWidget(new QLabel("Name:", main));
    form->addWidget(nameEdit);
    form->addWidget(new QLabel("Template:", main));
    form->addWidget(tplBox);
    form->addWidget(btnCreate);
    ml->addLayout(form);
    // Сетка карточек.
    grid = new QListWidget(main);
    grid->setViewMode(QListWidget::IconMode);
    grid->setIconSize(QSize(200, 140));
    grid->setResizeMode(QListWidget::Adjust);
    grid->setMovement(QListWidget::Static);
    grid->setSpacing(12);
    ml->addWidget(grid);
    lay->addWidget(main, 1);

    loadRegistry();
    refresh();

    QObject::connect(btnNew, &QPushButton::clicked, [=]() { nameEdit->setFocus(); });
    QObject::connect(btnCreate, &QPushButton::clicked, [=]() {
        QString nm = nameEdit->text().trimmed();
        if (nm.isEmpty()) return;
        QString dir = QDir::homePath() + "/voxprojects/" + nm;
        QDir().mkpath(dir + "/depot");
        FILE *f = fopen((dir + "/project.txt").toUtf8().constData(), "w");
        if (f) {
            fprintf(f, "name=%s\ntemplate=%s\nbackend=%d\nversion=0.1.0\n", nm.toUtf8().constData(),
                    tplBox->currentText().toUtf8().constData(),
                    hubBackendValue(apiBox->currentIndex()));
            fclose(f);
        }
        names << nm;
        paths << dir;
        backends << hubBackendValue(apiBox->currentIndex());
        saveRegistry();
        refresh();
        nameEdit->clear();
    });
    auto doOpen = [=]() {
        QListWidgetItem *it = grid->currentItem();
        if (!it) return;
        int r = grid->row(it);
        if (r < 0 || r >= paths.size()) return;
        if (onOpen) onOpen(paths[r], backends[r]);
    };
    QObject::connect(grid, &QListWidget::itemDoubleClicked, [=]() { doOpen(); });
}

void HubWindow::loadRegistry() {
    QSettings s("voxengine", "hub");
    names.clear();
    paths.clear();
    backends.clear();
    int n = s.value("count", 0).toInt();
    for (int i = 0; i < n; i++) {
        names << s.value(QString("p%1/name").arg(i)).toString();
        paths << s.value(QString("p%1/path").arg(i)).toString();
        backends << s.value(QString("p%1/backend").arg(i), 1).toInt();
    }
}

void HubWindow::saveRegistry() {
    QSettings s("voxengine", "hub");
    s.setValue("count", names.size());
    for (int i = 0; i < names.size(); i++) {
        s.setValue(QString("p%1/name").arg(i), names[i]);
        s.setValue(QString("p%1/path").arg(i), paths[i]);
        s.setValue(QString("p%1/backend").arg(i), backends[i]);
    }
}

void HubWindow::refresh() {
    grid->clear();
    for (int i = 0; i < names.size(); i++) {
        // Карточка: миниатюра 70% + текст 30% (имя, версия, дата).
        auto *it = new QListWidgetItem();
        it->setIcon(projThumb(paths[i], names[i]));
        QString ver = projVersion(paths[i]);
        it->setText(QString("%1  %2\nLast Modified: %3").arg(names[i]).arg(ver).arg(agoStr(paths[i])));
        grid->addItem(it);
    }
}
