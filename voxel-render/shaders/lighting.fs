#version 450 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in float Tile;
in float AO;
in vec4 FragPosLightSpace;

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
uniform sampler2D  shadowMap;
uniform vec3       sunDirW; // направление НА солнце (мир)
uniform float      shadowStrength; // 0 ночью
uniform float      debugShadow; // 1: показать тени ч/б (клавиша P)
uniform float      satU; // насыщенность из консоли
uniform float      gammaU; // гамма из консоли

// =========================================================
//  Функции расчёта для каждого типа света
// =========================================================
// =========================================================
//  Тень солнца: PCF 3x3 по depth-карте
// =========================================================
float ShadowCalculation(vec4 posLightSpace, vec3 normal)
{
    vec3 proj = posLightSpace.xyz / posLightSpace.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 0.0;
    float bias = max(0.004 * (1.0 - max(dot(normal, sunDirW), 0.0)), 0.0008);
    float shadow = 0.0;
    vec2 texel = 1.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++) {
            float closest = texture(shadowMap, proj.xy + vec2(x, y) * texel).r;
            shadow += (proj.z - bias > closest) ? 1.0 : 0.0;
        }
    shadow /= 9.0;
    // край shadow-бокса (±70): гасим к нулю чтобы не было видимой границы
    shadow *= 1.0 - smoothstep(55.0, 70.0, length(viewPos - FragPos));
    return shadow * shadowStrength;
}

vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir)
{
    vec3 lightDir = normalize(-light.direction);

    float shadow = ShadowCalculation(FragPosLightSpace, normal);

    // ambient тоже давим тенью (иначе при ядерном ambient теней не видно)
    vec3 ambient = light.ambient * vec3(texture(material.diffuse, vec3(TexCoords, Tile)));
    ambient *= 1.0 - 0.6 * shadow;

    // diffuse (wrap: скользящий свет не даёт черноты утром, Valve-style)
    float diff    = clamp((dot(normal, lightDir) + 0.4) / 1.4, 0.0, 1.0);
    vec3  diffuse = light.diffuse * diff * vec3(texture(material.diffuse, vec3(TexCoords, Tile)));

    // specular
    vec3  reflectDir = reflect(-lightDir, normal);
    float spec       = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3  specular   = light.specular * spec * vec3(texture(material.specular, fract(TexCoords)));

    return (ambient + (1.0 - shadow) * (diffuse + specular));
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

    // фейковый воксельный шейдинг граней вместо атласа: верх 1.0, бока 0.7, низ 0.55
    float fshade = abs(norm.y) > 0.9 ? (norm.y > 0.0 ? 1.0 : 0.55) : 0.7;
    // вершинное AO: 3 полный свет, 0 щель
    float aoC = AO < 0.5 ? 0.45 : (AO < 1.5 ? 0.65 : (AO < 2.5 ? 0.82 : 1.0));

    // phase 1: directional (солнце)
    vec3 result = CalcDirLight(dirLight, norm, viewDir);

    // phase 2: point lights (лампочки)
    for (int i = 0; i < NR_POINT_LIGHTS; i++)
        result += CalcPointLight(pointLights[i], norm, FragPos, viewDir);

    // phase 3: spot (фонарик)
    result += CalcSpotLight(spotLight, norm, FragPos, viewDir);

    vec3 shaded = result * fshade * aoC;
    // ядовитость дня и гамма — из консоли (sun.sat/sun.gamma)
    shaded = mix(vec3(dot(shaded, vec3(0.3333))), shaded, satU);
    if (debugShadow > 0.5) {
        float sh = ShadowCalculation(FragPosLightSpace, norm);
        FragColor = vec4(vec3(1.0 - sh), 1.0);
        return;
    }
    float fd = length(viewPos - FragPos);
    float ff = clamp((fd - fogRange.x) / (fogRange.y - fogRange.x), 0.0, 1.0);
    vec3 col = mix(shaded, fogColor, ff);
    FragColor = vec4(pow(col, vec3(1.0 / gammaU)), 1.0); // гамма (гл.34): без неё линейный свет тёмный
}