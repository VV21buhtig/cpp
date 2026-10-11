#ifndef FRONT_VIEWPORT_H
#define FRONT_VIEWPORT_H
// Вьюпорт редактора: воксельная сцена + гизмо-оси в одной цели.
// GL-контекст — скрытое GLFW-окно (Qt его не касается); кадр идёт
// в FBO-цель через frame_to, пиксели забираем glReadPixels в QImage.
// Никакого QOpenGLWidget: обмен через память, контексты не делим.
#include <QWidget>
#include <QImage>
#include <QTimer>

class Viewport : public QWidget {
public:
    explicit Viewport(QWidget *parent = nullptr);
    ~Viewport() override;
    bool init(int backend); // движок + мир + цель; false если бэкенда нет
    void setPlaying(bool on);
    bool isPlaying() const { return playing; }
    double fps() const { return fpsEma; }
    void setFov(float f) { camFov = f; }
    float fov() const { return camFov; }
    void setViewMode(int m) { viewMode = m; }
    // Пересадка на другой бэкенд (панель активности): ядра пересоздаются,
    // мир перезаливается. 1 ок.
    bool switchBackend(int be);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    void tick();
    void renderOnce();

    struct RenderCore *rc = nullptr;
    struct RenderCore *vrc = nullptr; // vec-ядро для гизмо-оверлея (та же цель)
    struct RcTarget *target = nullptr;
    unsigned presentProg = 0, presentVao = 0;
    QImage img;
    int placeheld = 0; // 1 если кадра нет (бэкенд без frame_to): рисуем текст
    int backend = 2; // выбранный в хабе (1 webgpu, 3 vk, 2 gl)
    QTimer *timer = nullptr;
    bool playing = false;
    int frame = 0;
    double fpsEma = 0;
    void *glfwWin = nullptr; // скрытое окно (пересоздаём при смене API)
    // Статика сцены: мир 5x5 + fed-флаги (стриминга в редакторе v1 нет).
    static const int GW = 5;
    void *world = nullptr; // VoxChunk[25], аллоцируем в init
    int fed[25] = {0};
    int fedLeft = 25, fedCur = 0;
    float camYaw = 0.6f, camPitch = -0.4f, camFov = 1.6f;
    int viewMode = 0; // 0 орбита, 1 верх, 2 бок, 3 фронт (орто-кнопки тулбара)
    // Гизмо-таскание: -1 нет, 0/1/2 ось XYZ. Точка старта и якорь мыши.
    int dragAxis = -1;
    float dragSX = 0, dragSY = 0;
    float dragPX = 0, dragPY = 0, dragPZ = 0;
    // Кадр для пикинга: PV-матрица + камера + fov последнего рендера.
    float pvM[16] = {0};
    float pcamX = 0, pcamY = 0, pcamZ = 0, pfov = 1.6f;
    int havePV = 0;
};
#endif
