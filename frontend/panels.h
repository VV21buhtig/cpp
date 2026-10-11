#ifndef FRONT_PANELS_H
#define FRONT_PANELS_H
// Панели редактора: иерархия, инспектор, ассеты, консоль.
// Всё через C API движка (scene.h, asset.h). Без Q_OBJECT: только лямбды.
#include <QWidget>
#include <QTextEdit>

class Viewport;
class QTreeWidget;
class QDoubleSpinBox;
class QListWidget;
class QLineEdit;

// Консоль: append-лог действий (stderr движка остаётся в терминале).
class ConsolePanel : public QWidget {
public:
    explicit ConsolePanel(QWidget *parent = nullptr);
    void log(const QString &s);

private:
    QTextEdit *out = nullptr;
};

// Перехват stderr движка в панель (один раз, до ядер).
void console_capture_stderr(ConsolePanel *con);

QWidget *makeHierarchy(QWidget *parent, ConsolePanel *con);
QWidget *makeInspector(QWidget *parent, Viewport *vp, ConsolePanel *con);
QWidget *makeAssets(QWidget *parent, ConsolePanel *con, const QString &depot);
// Дерево файлов проекта (только чтение).
QWidget *makeSolution(QWidget *parent, const QString &project);
// Выбор сущности извне (иерархия зовёт сама; вьюпорту/гизмо — сюда же).
// scene.h целиком: нужен полный ScWorld (мир живёт в панелях).
#include "scene.h"
void ui_select(unsigned id);
unsigned ui_selected(void);
struct ScWorld *ui_world(void);
// Проект: scene.txt туда-обратно (имена/родители/трансформы/видимость).
// Формат строк: e <имя> <parentIdx|-1> <vis> <px py pz rx ry rz sx sy sz>
void scene_save(const QString &projectDir);
void scene_load(const QString &projectDir);
#endif
