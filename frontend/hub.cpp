// Хаб: реестр проектов, шаблоны, выбор API. C++ без Q_OBJECT.
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

HubWindow::HubWindow(OpenFn onOpen, QWidget *parent)
    : QWidget(parent), onOpen(onOpen) {
    setWindowTitle("Engine Hub - Projects");
    resize(900, 600);
    auto *lay = new QVBoxLayout(this);
    // Верх: New Project + API.
    auto *top = new QHBoxLayout();
    auto *btnNew = new QPushButton("+ New Project", this);
    top->addWidget(btnNew);
    top->addWidget(new QLabel("Render API:", this));
    apiBox = new QComboBox(this);
    apiBox->addItems({"WebGPU", "Vulkan", "OpenGL"});
    top->addWidget(apiBox);
    top->addStretch(1);
    lay->addLayout(top);
    // Середина: список проектов + форма.
    auto *mid = new QHBoxLayout();
    list = new QListWidget(this);
    mid->addWidget(list, 2);
    auto *form = new QGroupBox("New project", this);
    auto *fl = new QVBoxLayout(form);
    nameEdit = new QLineEdit(form);
    nameEdit->setPlaceholderText("Project name...");
    tplBox = new QComboBox(form);
    tplBox->addItems({"Empty", "Voxel Terrain", "SDF Primitives", "Vector Shapes"});
    auto *btnCreate = new QPushButton("Create", form);
    fl->addWidget(new QLabel("Name:", form));
    fl->addWidget(nameEdit);
    fl->addWidget(new QLabel("Template:", form));
    fl->addWidget(tplBox);
    fl->addWidget(btnCreate);
    fl->addStretch(1);
    mid->addWidget(form, 1);
    lay->addLayout(mid);
    // Низ: Open/Remove.
    auto *bot = new QHBoxLayout();
    auto *btnOpen = new QPushButton("Open Project", this);
    auto *btnDel = new QPushButton("Remove", this);
    bot->addWidget(btnOpen);
    bot->addWidget(btnDel);
    bot->addStretch(1);
    lay->addLayout(bot);

    QObject::connect(btnNew, &QPushButton::clicked, [=]() { nameEdit->setFocus(); });
    QObject::connect(btnCreate, &QPushButton::clicked, [=]() {
        QString nm = nameEdit->text().trimmed();
        if (nm.isEmpty()) return;
        QString dir = QDir::homePath() + "/voxprojects/" + nm;
        QDir().mkpath(dir + "/depot");
        // Проект: depot/ пустой, шаблон — запись (контент шаблонов позже).
        FILE *f = fopen((dir + "/project.txt").toUtf8().constData(), "w");
        if (f) {
            fprintf(f, "name=%s\ntemplate=%s\nbackend=%d\n",
                    nm.toUtf8().constData(), tplBox->currentText().toUtf8().constData(),
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
        int r = list->currentRow();
        if (r < 0 || r >= paths.size()) return;
        if (onOpen) onOpen(paths[r], backends[r]);
    };
    QObject::connect(btnOpen, &QPushButton::clicked, [=]() { doOpen(); });
    QObject::connect(list, &QListWidget::itemDoubleClicked, [=]() { doOpen(); });
    QObject::connect(btnDel, &QPushButton::clicked, [=]() {
        int r = list->currentRow();
        if (r < 0 || r >= paths.size()) return;
        names.removeAt(r);
        paths.removeAt(r);
        backends.removeAt(r);
        saveRegistry();
        refresh();
    });
    loadRegistry();
    refresh();
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
    list->clear();
    for (int i = 0; i < names.size(); i++) {
        const char *be = "?";
        if (backends[i] == 1) be = "WebGPU";
        else if (backends[i] == 3) be = "Vulkan";
        else if (backends[i] == 2) be = "OpenGL";
        list->addItem(QString("%1  [%2]").arg(names[i]).arg(be));
    }
}
