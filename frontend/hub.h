#ifndef FRONT_HUB_H
#define FRONT_HUB_H
// Хаб проектов (Unity Hub): первое окно eedit. Список проектов из реестра
// (~/.config/voxengine/projects.ini), New Project (имя+шаблон+API),
// Open -> callback (projectPath, backend) наружу. Без Q_OBJECT: лямбды.
#include <QWidget>
#include <QString>
#include <functional>

class QListWidget;
class QComboBox;
class QLineEdit;
class ConsolePanel;

class HubWindow : public QWidget {
public:
    using OpenFn = std::function<void(const QString &project, int backend)>;
    explicit HubWindow(OpenFn onOpen, QWidget *parent = nullptr);
    void refresh();

private:
    OpenFn onOpen;
    QListWidget *list = nullptr;
    QComboBox *apiBox = nullptr;
    QComboBox *tplBox = nullptr;
    QLineEdit *nameEdit = nullptr;
    ConsolePanel *con = nullptr;
    void loadRegistry();
    void saveRegistry();
    // Реестр в памяти: имена + пути + бэкенды.
    QStringList names, paths;
    QList<int> backends;
};

inline int hubBackendIndex(int be) {
    if (be == 3) return 1; // VK
    if (be == 2) return 2; // GL
    return 0;              // WebGPU (дефолт)
}

inline int hubBackendValue(int idx) {
    if (idx == 1) return 3;
    if (idx == 2) return 2;
    return 1;
}
#endif
