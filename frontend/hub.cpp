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
#include <QMenu>
#include <QShortcut>
#include <QMessageBox>
#include <QFile>
#include <cstring>
#include "../engine/asset.h" // asset_import для стартового депо
#include "../engine/project.h" // создание файлов — бекенд, хаб витрина

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
    // Лево: фиксированный сайдбар. Цвета — из темы (#hubSide), не инлайн.
    auto *side = new QWidget(this);
    side->setObjectName("hubSide");
    side->setFixedWidth(180);
    auto *sl = new QVBoxLayout(side);
    auto mkHead = [=](const char *t) -> void {
        auto *l = new QLabel(t, side);
        l->setProperty("nav", "head");
        sl->addWidget(l);
    };
    auto mkItem = [=](const char *t, bool active) -> QPushButton * {
        auto *b = new QPushButton(t, side);
        b->setFlat(true);
        // Состояние — свойством (рисует тема), не инлайн-листом.
        b->setProperty("nav", active ? "active" : "item");
        sl->addWidget(b);
        return b;
    };
    mkHead("BROWSE");
    mkItem("Projects", true);
    // Позже — серым и честно, а не муляжом.
    auto later = [=](const char *t) {
        auto *b = mkItem(t, false);
        b->setEnabled(false);
        b->setToolTip("позже");
    };
    later("Templates");
    mkHead("LIBRARY");
    later("Assets");
    later("Samples");
    later("Tutorials");
    sl->addStretch(1);
    lay->addWidget(side);
    // Право: верхняя панель + сетка.
    auto *main = new QWidget(this);
    auto *ml = new QVBoxLayout(main);
    auto *top = new QHBoxLayout();
    auto *btnNew = new QPushButton("+ New Project", main);
    btnNew->setObjectName("hubNew");
    top->addWidget(btnNew);
    top->addWidget(new QLabel("Render API:", main));
    apiBox = new QComboBox(main);
    apiBox->addItems({"WebGPU", "Vulkan", "OpenGL"});
    top->addWidget(apiBox);
    top->addStretch(1);
    // Иконки справа: поиск живой (фильтр сетки), остальное позже.
    searchEdit = new QLineEdit(main);
    searchEdit->setPlaceholderText("Search...");
    searchEdit->setMaximumWidth(160);
    top->addWidget(searchEdit);
    // Refresh: перечитать реестр, выкинуть мёртвые пути (project.txt пропал).
    auto *btnRefresh = new QPushButton("Refresh", main);
    btnRefresh->setFlat(true);
    top->addWidget(btnRefresh);
    QObject::connect(btnRefresh, &QPushButton::clicked, [=]() {
        loadRegistry();
        for (int i = paths.size() - 1; i >= 0; i--) {
            FILE *f = fopen((paths[i] + "/project.txt").toUtf8().constData(), "rb");
            if (f) fclose(f);
            else {
                names.removeAt(i);
                paths.removeAt(i);
                backends.removeAt(i);
            }
        }
        saveRegistry();
        refresh();
    });
    auto *btnGear = new QPushButton("Settings", main);
    auto *btnProf = new QPushButton("Profile", main);
    for (auto *b : {btnGear, btnProf}) {
        b->setFlat(true);
        b->setEnabled(false);
        b->setToolTip("позже");
        top->addWidget(b);
    }
    ml->addLayout(top);
    // Форма создания (компактно под верхней панелью).
    auto *form = new QHBoxLayout();
    nameEdit = new QLineEdit(main);
    nameEdit->setPlaceholderText("Project name...");
    tplBox = new QComboBox(main);
    tplBox->addItems(
        {"Empty", "Voxel Terrain", "SDF Primitives", "Vector Shapes", "Pixel Canvas", "Mesh Soup"});
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

    QObject::connect(btnNew, &QPushButton::clicked, [=]() {
        // Новая карточка: чистая форма + фокус (создаёт Create).
        nameEdit->clear();
        tplBox->setCurrentIndex(0);
        nameEdit->setFocus();
    });
    auto doOpen = [=]() {
        QListWidgetItem *it = grid->currentItem();
        if (!it) return;
        int r = it->data(Qt::UserRole).toInt();
        if (r < 0 || r >= paths.size()) return;
        if (onOpen) onOpen(paths[r], backends[r]);
    };
    QObject::connect(btnCreate, &QPushButton::clicked, [=]() {
        // Создать по-настоящему + открыть. Ошибка — боксом, не молча.
        QString err;
        QString dir = createProject(nameEdit->text(), tplBox->currentText(),
                                    hubBackendValue(apiBox->currentIndex()), &err);
        nameEdit->clear();
        if (dir.isEmpty()) {
            QMessageBox::warning(grid, "New Project", err.isEmpty() ? "отказ" : err);
            return;
        }
        // Открыть созданное: найти карточку и даблклик программно.
        for (int i = 0; i < grid->count(); i++) {
            QListWidgetItem *it = grid->item(i);
            if (it && it->data(Qt::UserRole).toInt() >= 0 &&
                paths[it->data(Qt::UserRole).toInt()] == dir) {
                grid->setCurrentItem(it);
                break;
            }
        }
        doOpen();
    });
    QObject::connect(grid, &QListWidget::itemDoubleClicked, [=]() { doOpen(); });
    // Удаление из хаба: Del или правая кнопка. Только из реестра,
    // файлы проекта НЕ трогаем (удалять данные молча — нельзя).
    auto doDelete = [=]() {
        QListWidgetItem *it = grid->currentItem();
        if (!it) return;
        int r = it->data(Qt::UserRole).toInt();
        if (r < 0 || r >= paths.size()) return;
        names.removeAt(r);
        paths.removeAt(r);
        backends.removeAt(r);
        saveRegistry();
        refresh();
    };
    grid->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(grid, &QListWidget::customContextMenuRequested, [=](const QPoint &at) {
        QListWidgetItem *it = grid->itemAt(at);
        if (!it) return;
        grid->setCurrentItem(it);
        QMenu m(grid);
        m.addAction("Убрать из хаба", doDelete);
        m.exec(grid->mapToGlobal(at));
    });
    auto *delKey = new QShortcut(QKeySequence::Delete, grid);
    QObject::connect(delKey, &QShortcut::activated, [=]() { doDelete(); });
    // Живой поиск: фильтр карточек по имени.
    QObject::connect(searchEdit, &QLineEdit::textChanged, [=]() { refresh(); });
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

// Одно значение key= из project.txt (пусто если нет).
static QString projValue(const QString &path, const char *key) {
    FILE *f = fopen((path + "/project.txt").toUtf8().constData(), "rb");
    if (!f) return "";
    char line[256];
    size_t kl = strlen(key);
    QString v;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, key, kl) && line[kl] == '=') {
            char *nl = strchr(line, '\n');
            if (nl) *nl = 0;
            v = QString::fromUtf8(line + kl + 1);
            break;
        }
    }
    fclose(f);
    return v;
}

// fixs-02: настоящее создание проекта: валидация, файлы, шаблон, реестр.
// Файлы — через C-бекенд (eng_project_create): хаб только витрина.
QString HubWindow::createProject(const QString &rawName, const QString &tpl, int backend,
                                 QString *err) {
    if (err) err->clear();
    QString root = QDir::homePath() + "/voxprojects";
    char ebuf[128] = {0};
    int ok = eng_project_create(root.toUtf8().constData(), rawName.trimmed().toUtf8().constData(),
                                tpl.toUtf8().constData(), backend, ebuf, sizeof ebuf);
    if (!ok) {
        if (err) *err = QString::fromUtf8(ebuf[0] ? ebuf : "отказ");
        return QString();
    }
    QString nm = rawName.trimmed();
    QString dir = root + "/" + nm;
    // Вокселю — стартовые тайлы в депо (через движок: FNV+дедуп+индекс).
    if (tpl == "Voxel Terrain") {
        asset_import((dir + "/depot").toUtf8().constData(), "engine/assets/tiles/grass_top.png");
        asset_import((dir + "/depot").toUtf8().constData(), "engine/assets/tiles/dirt.png");
    }
    // В реестр (старый висяк по тому же пути — заменить).
    for (int i = 0; i < paths.size(); i++) {
        if (paths[i] == dir) {
            names.removeAt(i);
            paths.removeAt(i);
            backends.removeAt(i);
            break;
        }
    }
    if (backend < 1 || backend > 3) backend = 2;
    names << nm;
    paths << dir;
    backends << backend;
    saveRegistry();
    refresh();
    return dir;
}

void HubWindow::registerProject(const QString &path) {
    if (path.isEmpty()) return;
    {
        // Без project.txt — не проект, молча мимо.
        FILE *f = fopen((path + "/project.txt").toUtf8().constData(), "rb");
        if (!f) return;
        fclose(f);
    }
    for (int i = 0; i < paths.size(); i++)
        if (paths[i] == path) return; // уже в реестре
    QString nm = projValue(path, "name");
    if (nm.isEmpty()) nm = QDir(path).dirName();
    int be = projValue(path, "backend").toInt();
    if (be < 1 || be > 3) be = 2;
    names << nm;
    paths << path;
    backends << be;
    saveRegistry();
    refresh();
}

void HubWindow::refresh() {
    grid->clear();
    QString flt = searchEdit ? searchEdit->text().trimmed().toLower() : "";
    for (int i = 0; i < names.size(); i++) {
        if (!flt.isEmpty() && !names[i].toLower().contains(flt)) continue;
        // Карточка: миниатюра 70% + текст 30% (имя, версия, дата).
        auto *it = new QListWidgetItem();
        it->setData(Qt::UserRole, i); // индекс в реестре (фильтр сдвигает ряды)
        it->setIcon(projThumb(paths[i], names[i]));
        QString ver = projVersion(paths[i]);
        it->setText(QString("%1  %2\nLast Modified: %3").arg(names[i]).arg(ver).arg(agoStr(paths[i])));
        grid->addItem(it);
    }
}
