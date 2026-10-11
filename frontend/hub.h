#ifndef FRONT_HUB_H
#define FRONT_HUB_H
// Хаб проектов: сайдбар + сетка карточек (как в референсе).
// Проекты из реестра, миниатюра = первый PNG депо (или буква),
// версия и дата из project.txt / mtime. Без Q_OBJECT: лямбды.
#include <QWidget>
#include <QString>
#include <functional>

class QListWidget;
class QComboBox;
class QLineEdit;

class HubWindow : public QWidget {
public:
    using OpenFn = std::function<void(const QString &project, int backend)>;
    explicit HubWindow(OpenFn onOpen, QWidget *parent = nullptr);
    void refresh();
    // Нормальный вход: подселить проект в реестр хаба (читает name/backend
    // из project.txt). Дубли по пути игнорит. Для ED_HUB_ADD.
    void registerProject(const QString &path);

private:
    OpenFn onOpen;
    QListWidget *grid = nullptr;
    QComboBox *apiBox = nullptr;
    QComboBox *tplBox = nullptr;
    QLineEdit *nameEdit = nullptr;
    QLineEdit *searchEdit = nullptr;
    void loadRegistry();
    void saveRegistry();
    QStringList names, paths;
    QList<int> backends;
    QStringList versions;
};
#endif
