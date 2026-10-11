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

QWidget *makeHierarchy(QWidget *parent, ConsolePanel *con);
QWidget *makeInspector(QWidget *parent, Viewport *vp, ConsolePanel *con);
QWidget *makeAssets(QWidget *parent, ConsolePanel *con);
#endif
