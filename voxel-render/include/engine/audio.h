#ifndef AUDIO_H
#define AUDIO_H

#include <glm/glm.hpp>

// Engine: 3D-звук (miniaudio, вендор). Всё процедурное, файлов нет.
// Нет устройства → тихий null-режим, игра не падает.
struct AudioSys {
    bool ok = false;
    float masterVol = 0.8f;

    bool init();
    void shutdown();
    void setMaster(float v);
    void listener(glm::vec3 pos, glm::vec3 front);

    void playBreak(glm::vec3 at);   // сломал блок
    void playBreakWood(glm::vec3 at); // сломал дерево
    void playPlace(glm::vec3 at);   // поставил блок
    void playStep(glm::vec3 at, int surf); // шаг: 0 трава 1 камень 2 мокро
    void playSplash(glm::vec3 at);  // вошёл в воду
    void playThunk(glm::vec3 at);   // вошёл в лаву
    void playUI();                  // клик меню
    void wind(bool on);             // фоновый эмбиент (loop)
    void waterAt(glm::vec3 at, float level, float dt); // шуршание воды рядом
};

#endif
