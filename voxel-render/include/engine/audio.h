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
    void playPlace(glm::vec3 at);   // поставил блок
    void playStep(glm::vec3 at);    // шаг (питч джиттер)
    void playSplash(glm::vec3 at);  // вошёл в воду
    void playThunk(glm::vec3 at);   // вошёл в лаву
    void playUI();                  // клик меню
    void wind(bool on);             // фоновый ветер (loop)
};

#endif
