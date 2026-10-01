#version 450 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in float Tile;
in float AO;
in vec4 ShadowPos; // карта спит (shadow.on=0), varying живёт до времён

uniform sampler2D shadowMap; // P2a карта глубины от солнца
uniform float shadowOn;      // 0 ночью/выкл — тени не считать
uniform vec2 shadowTexel;    // 1/размер карты

// P2b PCF 2x2 K-стиль + slope-scaled bias (у них PCSS/bias от наклона):
// на скользящих лучах глубина гуляет — bias растёт, acne давится ценой микроподтека.
float calcShadow(vec4 sp, vec3 norm, vec3 sunDir, vec3 camPos)
{
    vec3 p = sp.xyz / sp.w * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    float ndl = max(dot(norm, sunDir), 0.0);
    float bias = 0.001 + 0.008 * (1.0 - ndl);
    float s = 0.0;
    s += step(p.z - bias, texture(shadowMap, p.xy + vec2(-0.5, -0.5) * shadowTexel).r);
    s += step(p.z - bias, texture(shadowMap, p.xy + vec2( 0.5, -0.5) * shadowTexel).r);
    s += step(p.z - bias, texture(shadowMap, p.xy + vec2(-0.5,  0.5) * shadowTexel).r);
    s += step(p.z - bias, texture(shadowMap, p.xy + vec2( 0.5,  0.5) * shadowTexel).r);
    s *= 0.25;
    // P2c фейды: скользящие лучи (вода/полосы!) и край бокса — только мягкий baked.
    // Иначе acne-дребезг на грани порога мигает от любого движения камеры.
    float gFade = smoothstep(0.0, 0.2, ndl);
    float eFade = smoothstep(0.0, 0.05, p.x) * smoothstep(1.0, 0.95, p.x) *
                  smoothstep(0.0, 0.05, p.y) * smoothstep(1.0, 0.95, p.y);
    // P2e фейд по дистанции: тексель 7см издалека < пикселя → муар-пятна на склонах,
    // вблизи чисто. Дальше 60м только baked (как у всех: резкость лишь рядом).
    float cd = length(camPos - FragPos);
    float dFade = 1.0 - smoothstep(25.0, 60.0, cd);
    return mix(1.0, s, gFade * eFade * dFade);
}

// ========== MATERIAL ==========
struct Material {
    sampler2DArray diffuse; // tiles/ атлас-массив: 0 grass_top 1 grass_side 2 dirt 3 stone
    sampler2D specular;
    float     shininess;
};

// ========== DIRECTIONAL ==========
struct DirLight {
    vec3 direction;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

// ========== POINT ==========
struct PointLight {
    vec3  position;
    float constant;
    float linear;
    float quadratic;
    vec3  ambient;
    vec3  diffuse;
    vec3  specular;
};

// ========== SPOT ==========
struct SpotLight {
    vec3  position;
    vec3  direction;
    float cutOff;
    float outerCutOff;
    float constant;
    float linear;
    float quadratic;
    vec3  ambient;
    vec3  diffuse;
    vec3  specular;
};

#define NR_POINT_LIGHTS 4

uniform Material   material;
uniform DirLight   dirLight;
uniform PointLight pointLights[NR_POINT_LIGHTS];
uniform SpotLight  spotLight;
uniform vec3       viewPos;
uniform vec3       fogColor;
uniform vec2       fogRange; // near far
uniform float      satU; // насыщенность из консоли
uniform float      gammaU; // гамма из консоли
uniform float      alphaU; // 1.0 opaque, 0.75 вода
uniform float      uTime; // секунды, фликер факелов (K torch flicker)
uniform vec3       skyAmb; // P2j небесный ambient (зенит): день голубой, ночь тёмный
uniform vec3       gndAmb; // P2j земной ambient (отскок вниз): тёплый тёмный

// =========================================================
//  Функции расчёта для каждого типа света
// =========================================================
vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir)
{
    vec3 lightDir = normalize(-light.direction);
    vec3 texel = vec3(texture(material.diffuse, vec3(TexCoords, Tile)));

    // P2j hemispheric ambient (как у всех: небо сверху, земля снизу).
    // Плоский серый давал черноту теневых склонов — MC светит туда небом.
    vec3 ambient = mix(gndAmb, skyAmb, normal.y * 0.5 + 0.5) * texel;

    // diffuse БЕЗ wrap: грань от солнца — полная, против — ноль (только ambient).
    // wrap давал до 3.5x между соседними колонками зигзага стены — длинные полосы.
    float diff    = max(dot(normal, lightDir), 0.0);
    vec3  diffuse = light.diffuse * diff * vec3(texture(material.diffuse, vec3(TexCoords, Tile)));

    // specular
    vec3  reflectDir = reflect(-lightDir, normal);
    float spec       = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3  specular   = light.specular * spec * vec3(texture(material.specular, fract(TexCoords)));

    return (ambient + diffuse + specular);
}

vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir)
{
    vec3 lightDir = normalize(light.position - fragPos);

    vec3 ambient = light.ambient * vec3(texture(material.diffuse, vec3(TexCoords, Tile)));

    float diff    = max(dot(normal, lightDir), 0.0);
    vec3  diffuse = light.diffuse * diff * vec3(texture(material.diffuse, vec3(TexCoords, Tile)));

    vec3  reflectDir = reflect(-lightDir, normal);
    float spec       = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3  specular   = light.specular * spec * vec3(texture(material.specular, fract(TexCoords)));

    // attenuation
    float distance    = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant
                             + light.linear    * distance
                             + light.quadratic * distance * distance);
    ambient  *= attenuation;
    diffuse  *= attenuation;
    specular *= attenuation;

    return (ambient + diffuse + specular);
}

vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir)
{
    vec3 lightDir = normalize(light.position - fragPos);

    vec3 ambient = light.ambient * vec3(texture(material.diffuse, vec3(TexCoords, Tile)));

    float diff    = max(dot(normal, lightDir), 0.0);
    vec3  diffuse = light.diffuse * diff * vec3(texture(material.diffuse, vec3(TexCoords, Tile)));

    vec3  reflectDir = reflect(-lightDir, normal);
    float spec       = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3  specular   = light.specular * spec * vec3(texture(material.specular, fract(TexCoords)));

    // soft spotlight
    float theta     = dot(lightDir, normalize(-light.direction));
    float epsilon   = light.cutOff - light.outerCutOff;
    float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
    diffuse  *= intensity;
    specular *= intensity;

    // attenuation
    float distance    = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant
                             + light.linear    * distance
                             + light.quadratic * distance * distance);
    ambient  *= attenuation;
    diffuse  *= attenuation;
    specular *= attenuation;

    return (ambient + diffuse + specular);
}

// =========================================================
//  main
// =========================================================
void main()
{
    vec3 norm    = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);

    // MC-шейдинг по осям: верх 1.0, низ 0.5, X 0.6, Z 0.8. Фикс на весь день —
    // контраст не гуляет с азимутом солнца (было 0.7 всем бокам + wrap).
    float fshade = abs(norm.y) > 0.9 ? (norm.y > 0.0 ? 1.0 : 0.5)
                                     : (abs(norm.x) > abs(norm.z) ? 0.6 : 0.8);
    // вершинное AO MC-мягкое: пол 0.5 (было K 0.3 — давало 3.3x перепад
    // между соседними колонками ступеней, читалось как полосы-каша).
    float aoV = clamp(AO / 3.0, 0.0, 1.0);
    float aoC = 0.5 + 0.5 * aoV * aoV;

    vec4 tileTexA = texture(material.diffuse, vec3(TexCoords, Tile));
    if (Tile > 5.5 && Tile < 6.5 && tileTexA.a < 0.5) discard; // листва с дырками
    vec3 tileTex = vec3(tileTexA);
    // лава светится сама (tile 5). Было Tile > 4.5 — светились и листва/бревно!
    if (Tile > 4.5 && Tile < 5.5) {
        float fd0 = length(viewPos - FragPos);
        float ff0 = clamp((fd0 - fogRange.x) / (fogRange.y - fogRange.x), 0.0, 1.0);
        vec3 lc = mix(tileTex * 1.8, fogColor, ff0);
        FragColor = vec4(pow(lc, vec3(1.0 / gammaU)), 1.0);
        return;
    }

    // phase 1: directional (солнце). Флуда нет: ambient+direct полностью.
    // Карта теней припаркована (shadow.on=0): sh всегда 1, код спит до времён.
    vec3 sunFull = CalcDirLight(dirLight, norm, viewDir);
    vec3 sunAmb = mix(gndAmb, skyAmb, norm.y * 0.5 + 0.5) * tileTex; // тот же hemispheric
    vec3 sunDirect = sunFull - sunAmb;
    vec3 sunDirW = normalize(-dirLight.direction);
    float sh = (shadowOn > 0.5) ? calcShadow(ShadowPos, norm, sunDirW, viewPos) : 1.0;
    if (Tile > 3.5 && Tile < 4.5) sh = 1.0;
    vec3 result = sunAmb + sunDirect * sh;

    // phase 2: point lights (лампочки)
    for (int i = 0; i < NR_POINT_LIGHTS; i++)
        result += CalcPointLight(pointLights[i], norm, FragPos, viewDir);

    // phase 3: spot (фонарик)
    result += CalcSpotLight(spotLight, norm, FragPos, viewDir);

    vec3 shaded = result * fshade * aoC;
    // ядовитость дня и гамма — из консоли (sun.sat/sun.gamma)
    shaded = mix(vec3(dot(shaded, vec3(0.3333))), shaded, satU);
    float fd = length(viewPos - FragPos);
    float ff = clamp((fd - fogRange.x) / (fogRange.y - fogRange.x), 0.0, 1.0);
    vec3 col = mix(shaded, fogColor, ff);
    FragColor = vec4(pow(col, vec3(1.0 / gammaU)), alphaU); // гамма (гл.34): без неё линейный свет тёмный
}