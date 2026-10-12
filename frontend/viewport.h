#ifndef FRONT_VIEWPORT_H
#define FRONT_VIEWPORT_H
// Вьюпорт редактора: воксельная сцена + гизмо-оси в одной цели.
// GL-контекст — скрытое GLFW-окно (Qt его не касается); кадр идёт
// в FBO-цель через frame_to, пиксели забираем glReadPixels в QImage.
// VK/WebGPU — через frame_cpu (ядро рендерит и скачивает в RGBA).
// Никакого QOpenGLWidget: обмен через память, контексты не делим.
#include <QWidget>
#include <QImage>
#include <QTimer>

class Viewport : public QWidget {
public:
    explicit Viewport(QWidget *parent = nullptr);
    ~Viewport() override;
    bool init(int backend); // движок + мир + цель; false если бэкенда нет
    bool initDomain(unsigned domain, int backend); // домен из шаблона проекта
    void setPlaying(bool on);
    bool isPlaying() const { return playing; }
    double fps() const { return fpsEma; }
    void setFov(float f) { camFov = f; }
    float fov() const { return camFov; }
    void setViewMode(int m) { viewMode = m; }
    // Матричный прогон: кадров отрисовано (кадр растёт только при рендере).
    int frameCount() const { return frame; }
    // Есть ли картинка (центр ярче порога)? Для автопроверки стенда.
    bool hasPicture() const;
    // Пересадка на другой бэкенд (панель активности): ядра пересоздаются,
    // мир перезаливается. 1 ок.
    bool switchBackend(int be);
    // Один кадр сразу (чтобы вьюпорт не был чёрным до Play).
    void frameNow();
    int api() const { return backend; } // текущий бэкенд (для подсветки кнопок)
    // Режим гизмо с тулбара: 0 двигать, 1 крутить, 2 тянуть. Таскание то же.
    void setGizmoMode(int m) { gizmoMode = m < 0 ? 0 : (m > 2 ? 2 : m); }
    int gizmoModeGet() const { return gizmoMode; }

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
    uint8_t *cpuBuf = nullptr; // RGBA 1280x720 для frame_cpu (VK/WebGPU), разово
    int placeheld = 0; // 1 если кадра нет: рисуем текст
    int backend = 2; // выбранный в хабе (1 webgpu, 3 vk, 2 gl)
    unsigned domain = 1; // домен из шаблона (1 воксель, 2 sdf, 4 вектор, 16 меш)
    int gizmoMode = 0; // 0 двигать, 1 крутить, 2 тянуть (тулбар)
    int sdfFed = 0, vecFed = 0, meshFed = 0; // разовые заливки
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
    float dragRX = 0, dragRY = 0, dragRZ = 0; // rot на старте (режим крутить)
    float dragEX = 1, dragEY = 1, dragEZ = 1; // scale на старте (режим тянуть)
    // Кадр для пикинга: PV-матрица + камера + fov последнего рендера.
    float pvM[16] = {0};
    float pcamX = 0, pcamY = 0, pcamZ = 0, pfov = 1.6f;
    int havePV = 0;
    // Экранные концы осей гизмо (пиксели 1280x720) + их валидность.
    float sAx[3][4] = {{{0}}};
    int sOk[3] = {0};
    int sHave = 0;
};
#endif
