#ifndef FRONT_VIEWPORT_H
#define FRONT_VIEWPORT_H
// Вьюпорт редактора: воксельная сцена движка -> QImage.
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

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void tick();
    void renderOnce();

    struct RenderCore *rc = nullptr;
    struct RcTarget *target = nullptr;
    unsigned presentProg = 0, presentVao = 0;
    QImage img;
    int placeheld = 0; // 1 если кадра нет (бэкенд без frame_to): рисуем текст
    int backend = 2; // выбранный в хабе (1 webgpu, 3 vk, 2 gl)
    QTimer *timer = nullptr;
    bool playing = false;
    int frame = 0;
    double fpsEma = 0;
    // Статика сцены: мир 5x5 + fed-флаги (стриминга в редакторе v1 нет).
    static const int GW = 5;
    void *world = nullptr; // VoxChunk[25], аллоцируем в init
    int fed[25] = {0};
    int fedLeft = 25, fedCur = 0;
    float camYaw = 0.6f, camPitch = -0.4f, camFov = 1.6f;
};
#endif
