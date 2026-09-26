#include "game/player.h"
#include "engine/world.h"
#include <cmath>

static bool solidAt(const World& w, float x, float y, float z) {
    return w.getBlock((int)floor(x), (int)floor(y), (int)floor(z)) != 0;
}

static bool collides(const World& w, glm::vec3 p, float hw, float h) {
    // AABB: x±hw, y..y+h, z±hw. Чуть вжимаем края чтобы не цепляться за швы.
    const float e = 0.001f;
    float x0 = p.x - hw + e, x1 = p.x + hw - e;
    float y0 = p.y + e, y1 = p.y + h - e;
    float z0 = p.z - hw + e, z1 = p.z + hw - e;
    for (int xi = (int)floor(x0); xi <= (int)floor(x1); xi++)
        for (int yi = (int)floor(y0); yi <= (int)floor(y1); yi++)
            for (int zi = (int)floor(z0); zi <= (int)floor(z1); zi++)
                if (w.getBlock(xi, yi, zi)) return true;
    (void)solidAt;
    return false;
}

void Player::spawn(const World& w, int wx, int wz) {
    int h = 0;
    for (int y = Chunk::SY - 1; y >= 0; y--)
        if (w.getBlock(wx, y, wz)) { h = y; break; }
    pos = glm::vec3(wx + 0.5f, h + 1.01f, wz + 0.5f);
    vel = glm::vec3(0.0f);
}

void Player::update(float dt, const World& w, glm::vec2 move, float yaw,
                    bool jump, bool down) {
    glm::vec3 fwd(cos(yaw), 0.0f, sin(yaw));
    glm::vec3 right(-fwd.z, 0.0f, fwd.x);
    // Camera yaw у нас: front=(cos yaw, ..., sin yaw)? yaw=-90 => front -z. fwd совпадает.
    float speed = fly ? 8.0f : 4.5f;
    glm::vec3 wish = (fwd * move.x + right * move.y) * speed;

    if (fly) {
        vel.x = wish.x; vel.z = wish.z;
        vel.y = (jump ? speed : 0.0f) + (down ? -speed : 0.0f);
        glm::vec3 np = pos + vel * dt;
        // в fly тоже не влетаем в блоки: по осям
        glm::vec3 t = pos;
        t.x = np.x; if (collides(w, t, halfW, height)) t.x = pos.x;
        t.z = np.z; if (collides(w, t, halfW, height)) t.z = pos.z;
        t.y = np.y; if (collides(w, t, halfW, height)) t.y = pos.y;
        pos = t;
        onGround = false;
        return;
    }

    const float g = 22.0f;
    vel.x = wish.x; vel.z = wish.z;
    vel.y -= g * dt;
    if (vel.y < -30.0f) vel.y = -30.0f;
    if (onGround && jump) { vel.y = 7.5f; onGround = false; }

    glm::vec3 np = pos + vel * dt;
    glm::vec3 t = pos;
    t.x = np.x;
    if (collides(w, t, halfW, height)) { t.x = pos.x; vel.x = 0; }
    t.z = np.z;
    if (collides(w, t, halfW, height)) { t.z = pos.z; vel.z = 0; }
    t.y = np.y;
    if (collides(w, t, halfW, height)) {
        if (vel.y <= 0) { // приземление: ставим ровно на блок
            t.y = floor(np.y) + 1.0f + 0.001f;
            // если всё ещё коллизия (потолок низкий) — откат
            if (collides(w, t, halfW, height)) t.y = pos.y;
            else onGround = true;
        } else {
            t.y = pos.y; // головой в потолок
        }
        vel.y = 0;
    } else {
        onGround = false;
    }
    // провалился под мир — респаун
    if (t.y < -10.0f) { spawn(w, 24, 24); return; }
    pos = t;
}
