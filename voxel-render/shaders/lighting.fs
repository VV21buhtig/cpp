#version 450 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in float Tile;
in float AO;

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

// =========================================================
//  Функции расчёта для каждого типа света
// =========================================================
vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir)
{
    vec3 lightDir = normalize(-light.direction);

    // ambient
    vec3 ambient = light.ambient * vec3(texture(material.diffuse, vec3(TexCoords, Tile)));

    // diffuse (wrap: скользящий свет не даёт черноты утром, Valve-style)
    float diff    = clamp((dot(normal, lightDir) + 0.4) / 1.4, 0.0, 1.0);
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

    // фейковый воксельный шейдинг граней вместо атласа: верх 1.0, бока 0.7, низ 0.55
    float fshade = abs(norm.y) > 0.9 ? (norm.y > 0.0 ? 1.0 : 0.55) : 0.7;
    // вершинное AO: 3 полный свет, 0 щель
    float aoC = AO < 0.5 ? 0.45 : (AO < 1.5 ? 0.65 : (AO < 2.5 ? 0.82 : 1.0));

    vec4 tileTexA = texture(material.diffuse, vec3(TexCoords, Tile));
    if (Tile > 5.5 && Tile < 6.5 && tileTexA.a < 0.5) discard; // листва с дырками
    vec3 tileTex = vec3(tileTexA);
    // лава светится сама (tile 5)
    if (Tile > 4.5) {
        float fd0 = length(viewPos - FragPos);
        float ff0 = clamp((fd0 - fogRange.x) / (fogRange.y - fogRange.x), 0.0, 1.0);
        vec3 lc = mix(tileTex * 1.8, fogColor, ff0);
        FragColor = vec4(pow(lc, vec3(1.0 / gammaU)), 1.0);
        return;
    }

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
    float fd = length(viewPos - FragPos);
    float ff = clamp((fd - fogRange.x) / (fogRange.y - fogRange.x), 0.0, 1.0);
    vec3 col = mix(shaded, fogColor, ff);
    FragColor = vec4(pow(col, vec3(1.0 / gammaU)), alphaU); // гамма (гл.34): без неё линейный свет тёмный
}