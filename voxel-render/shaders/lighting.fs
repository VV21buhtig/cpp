#version 450 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in float Tile;

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

// =========================================================
//  Функции расчёта для каждого типа света
// =========================================================
vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir)
{
    vec3 lightDir = normalize(-light.direction);

    // ambient
    vec3 ambient = light.ambient * vec3(texture(material.diffuse, vec3(TexCoords, Tile)));

    // diffuse
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

    // фейковый воксельный шейдинг граней вместо атласа: верх 1.0, бока 0.8, низ 0.55
    float fshade = abs(norm.y) > 0.9 ? (norm.y > 0.0 ? 1.0 : 0.55) : 0.8;

    // phase 1: directional (солнце)
    vec3 result = CalcDirLight(dirLight, norm, viewDir);

    // phase 2: point lights (лампочки)
    for (int i = 0; i < NR_POINT_LIGHTS; i++)
        result += CalcPointLight(pointLights[i], norm, FragPos, viewDir);

    // phase 3: spot (фонарик)
    result += CalcSpotLight(spotLight, norm, FragPos, viewDir);

    vec3 shaded = result * fshade;
    float fd = length(viewPos - FragPos);
    float ff = clamp((fd - fogRange.x) / (fogRange.y - fogRange.x), 0.0, 1.0);
    FragColor = vec4(mix(shaded, fogColor, ff), 1.0);
}