// main.cpp

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <string>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Vertex shader
const char* vertexShaderSrc = R"(
#version 330 core
layout (location=0) in vec3 aPos;
layout (location=1) in vec3 aNormal;
layout (location=2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

// Fragment shader
const char* fragmentShaderSrc = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

uniform vec3 viewPos;
uniform sampler2D albedoMap;

struct Light {
    vec3 position;
    vec3 color;
    float intensity;
};
uniform Light dirLight;
uniform Light pointLights[4];
uniform Light spotLights[2];

uniform float shininess;

void main() {
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 albedo = texture(albedoMap, TexCoord).rgb;

    vec3 ambient = 0.1 * albedo;
    vec3 result = ambient;

    // Directional light
    vec3 lightDir = normalize(-vec3(0.0, -1.0, -1.0));
    float diff = max(dot(norm, lightDir), 0.0);
    result += diff * dirLight.color * dirLight.intensity;

    for (int i=0; i<4; ++i) {
        vec3 toLight = normalize(pointLights[i].position - FragPos);
        float diffPL = max(dot(norm, toLight), 0.0);
        vec3 diffusePL = diffPL * pointLights[i].color * pointLights[i].intensity;
        vec3 halfwayDir = normalize(toLight + viewDir);
        float spec = pow(max(dot(norm, halfwayDir), 0.0), shininess);
        vec3 specular = spec * pointLights[i].color * pointLights[i].intensity;
        result += diffusePL + specular;
    }

    for (int i=0; i<2; ++i) {
        vec3 toLight = normalize(spotLights[i].position - FragPos);
        float theta = dot(toLight, normalize(-spotLights[i].position));
        float cutoff = cos(radians(12.5));
        float outerCutoff = cos(radians(17.5));
        float intensityFactor = smoothstep(cutoff, outerCutoff, theta);
        float diffSpot = max(dot(norm, toLight), 0.0);
        vec3 diffuseSpot = diffSpot * spotLights[i].color * spotLights[i].intensity * intensityFactor;
        vec3 halfwayDir = normalize(toLight + viewDir);
        float spec = pow(max(dot(norm, halfwayDir), 0.0), shininess);
        vec3 specularSpot = spec * spotLights[i].color * spotLights[i].intensity * intensityFactor;
        result += diffuseSpot + specularSpot;
    }

    FragColor = vec4(result,1.0);
}
)";

// Structures
struct Mesh {
    std::vector<GLuint> VAOs, VBOs, EBOs;
    std::vector<unsigned int> indexCounts;
};

struct SceneObject {
    Mesh mesh;
    glm::vec3 pos;
    glm::vec3 rot;
    glm::vec3 scale;
    std::string name;
    GLuint textureID;
};

// Camera parameters
float cameraDistance = 50.0f;
float cameraYaw = 45.0f;
float cameraPitch = -20.0f;

// Globals
GLFWwindow* window;
unsigned int SCR_WIDTH=1280, SCR_HEIGHT=720;
GLuint shaderProgram;

// Function prototypes
GLuint compileShader(GLenum type, const char* src);
GLuint createShaderProgram();
Mesh createCubeMesh();
GLuint LoadTexture(const char* filepath);
void setupScene(std::vector<SceneObject>& sceneObjects, Mesh& cubeMesh);
void drawScene(const std::vector<SceneObject>& sceneObjects, const glm::mat4& view, const glm::mat4& projection, GLuint shader);
void processInput(GLFWwindow* window);

// Main
int main() {
    // Init GLFW
    if (!glfwInit()) { std::cerr<<"Failed to init GLFW"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    window=glfwCreateWindow(SCR_WIDTH,SCR_HEIGHT,"Crowded Restaurant Scene",nullptr,nullptr);
    if (!window){ std::cerr<<"Failed to create GLFW"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glewExperimental=true; glewInit();

    // Shader
    GLuint shader=createShaderProgram();

    // Mesh
    Mesh cubeMesh = createCubeMesh();

    // Textures (replace paths with your images)
    GLuint wallTex=LoadTexture("textures/wall.jpg");
    GLuint floorTex=LoadTexture("textures/floor.jpg");
    GLuint tableTex=LoadTexture("textures/table.jpg");
    GLuint chairTex=LoadTexture("textures/chair.jpg");
    GLuint barTex=LoadTexture("textures/bar.jpg");
    GLuint kitchenTex=LoadTexture("textures/kitchen.jpg");
    GLuint sofaTex=LoadTexture("textures/sofa.jpg");
    GLuint plantTex=LoadTexture("textures/plant.jpg");

    // Build scene with many objects
    std::vector<SceneObject> sceneObjects;
    setupScene(sceneObjects, cubeMesh);

    // Assign textures based on object names
    for (auto& obj : sceneObjects) {
        if (obj.name.find("Wall") != std::string::npos) obj.textureID=wallTex;
        else if (obj.name.find("Floor") != std::string::npos) obj.textureID=floorTex;
        else if (obj.name.find("Table") != std::string::npos) obj.textureID=tableTex;
        else if (obj.name.find("Chair") != std::string::npos) obj.textureID=chairTex;
        else if (obj.name.find("Bar") != std::string::npos) obj.textureID=barTex;
        else if (obj.name.find("Kitchen") != std::string::npos) obj.textureID=kitchenTex;
        else if (obj.name.find("Sofa") != std::string::npos) obj.textureID=sofaTex;
        else if (obj.name.find("Plant") != std::string::npos) obj.textureID=plantTex;
        else obj.textureID=wallTex;
    }

    glEnable(GL_DEPTH_TEST);

    // Main loop
    while (!glfwWindowShouldClose(window)){
        processInput(window);

        // Camera position with mouse control optional
        float camX = cameraDistance * cos(glm::radians(cameraYaw)) * cos(glm::radians(cameraPitch));
        float camY = cameraDistance * sin(glm::radians(cameraPitch));
        float camZ = cameraDistance * sin(glm::radians(cameraYaw)) * cos(glm::radians(cameraPitch));
        glm::vec3 cameraPos(camX, camY, camZ);
        glm::mat4 view=glm::lookAt(cameraPos, glm::vec3(0), glm::vec3(0,1,0));
        glm::mat4 projection=glm::perspective(glm::radians(45.0f),(float)SCR_WIDTH/SCR_HEIGHT,0.1f,200.0f);

        // Clear
        if(true) glClearColor(0.5f,0.8f,1.0f,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        // Draw scene
        drawScene(sceneObjects, view, projection, shader);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    for (auto& obj : sceneObjects) {
        for (auto vao : obj.mesh.VAOs) glDeleteVertexArrays(1, &vao);
        for (auto vbo : obj.mesh.VBOs) glDeleteBuffers(1, &vbo);
        for (auto ebo : obj.mesh.EBOs) glDeleteBuffers(1, &ebo);
    }
    glDeleteProgram(shader);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// Input handling
void processInput(GLFWwindow* window){
    if (glfwGetKey(window, GLFW_KEY_ESCAPE)==GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    // Camera controls
    if (glfwGetKey(window, GLFW_KEY_W)==GLFW_PRESS)
        cameraDistance -= 0.5f;
    if (glfwGetKey(window, GLFW_KEY_S)==GLFW_PRESS)
        cameraDistance += 0.5f;
    if (glfwGetKey(window, GLFW_KEY_A)==GLFW_PRESS)
        cameraYaw -= 1;
    if (glfwGetKey(window, GLFW_KEY_D)==GLFW_PRESS)
        cameraYaw += 1;
    if (cameraDistance<10) cameraDistance=10;
    if (cameraDistance>150) cameraDistance=150;
}

// Shader functions
GLuint compileShader(GLenum type, const char* src) {
    GLuint s=glCreateShader(type);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);
    int success; glGetShaderiv(s,GL_COMPILE_STATUS,&success);
    if (!success){ char info[512]; glGetShaderInfoLog(s,512,nullptr,info); std::cerr<<"Shader error: "<<info<<"\n"; }
    return s;
}

GLuint createShaderProgram() {
    GLuint v=compileShader(GL_VERTEX_SHADER,vertexShaderSrc);
    GLuint f=compileShader(GL_FRAGMENT_SHADER,fragmentShaderSrc);
    GLuint prog=glCreateProgram();
    glAttachShader(prog,v); glAttachShader(prog,f);
    glLinkProgram(prog);
    int success; glGetProgramiv(prog,GL_LINK_STATUS,&success);
    if (!success){ char info[512]; glGetProgramInfoLog(prog,512,nullptr,info); std::cerr<<"Link error: "<<info<<"\n"; }
    glDeleteShader(v); glDeleteShader(f);
    return prog;
}

// Create cube mesh
Mesh createCubeMesh() {
    Mesh mesh;
    float vertices[] = {
        // positions          normals           texcoords
        -0.5f,-0.5f, 0.5f,   0,0,1,            0,0,
         0.5f,-0.5f, 0.5f,   0,0,1,            1,0,
         0.5f, 0.5f, 0.5f,   0,0,1,            1,1,
        -0.5f, 0.5f, 0.5f,   0,0,1,            0,1,
        -0.5f,-0.5f,-0.5f,   0,0,-1,           0,0,
         0.5f,-0.5f,-0.5f,   0,0,-1,           1,0,
         0.5f, 0.5f,-0.5f,   0,0,-1,           1,1,
        -0.5f, 0.5f,-0.5f,   0,0,-1,           0,1,
        -0.5f,-0.5f,-0.5f,  -1,0,0,            0,0,
        -0.5f, 0.5f,-0.5f,  -1,0,0,            1,0,
        -0.5f, 0.5f, 0.5f,  -1,0,0,            1,1,
        -0.5f,-0.5f, 0.5f,  -1,0,0,            0,1,
        0.5f,-0.5f,-0.5f,   1,0,0,            0,0,
        0.5f, 0.5f,-0.5f,   1,0,0,            1,0,
        0.5f, 0.5f, 0.5f,   1,0,0,            1,1,
        0.5f,-0.5f, 0.5f,   1,0,0,            0,1,
        -0.5f, 0.5f, 0.5f,   0,1,0,            0,0,
         0.5f, 0.5f, 0.5f,   0,1,0,            1,0,
         0.5f, 0.5f,-0.5f,   0,1,0,            1,1,
        -0.5f, 0.5f,-0.5f,   0,1,0,            0,1,
        -0.5f,-0.5f, 0.5f,   0,-1,0,           0,0,
         0.5f,-0.5f, 0.5f,   0,-1,0,           1,0,
         0.5f,-0.5f,-0.5f,   0,-1,0,           1,1,
        -0.5f,-0.5f,-0.5f,   0,-1,0,           0,1,
    };
    unsigned int indices[] = {
        0,1,2, 2,3,0,
        4,5,6, 6,7,4,
        8,9,10,10,11,8,
        12,13,14,14,15,12,
        16,17,18,18,19,16,
        20,21,22,22,23,20
    };

    GLuint VAO,VBO,EBO;
    glGenVertexArrays(1,&VAO);
    glGenBuffers(1,&VBO);
    glGenBuffers(1,&EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER,VBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    int stride=8*sizeof(float);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,stride,(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,stride,(void*)(6*sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    Mesh mesh;
    mesh.VAOs.push_back(VAO);
    mesh.VBOs.push_back(VBO);
    mesh.EBOs.push_back(EBO);
    mesh.indexCounts.push_back(sizeof(indices)/sizeof(indices[0]));
    return mesh;
}

// Load texture
GLuint LoadTexture(const char* filepath) {
    int width, height, nrChannels;
    unsigned char* data = stbi_load(filepath, &width, &height, &nrChannels, 0);
    if (!data) {
        std::cerr << "Failed to load texture: " << filepath << std::endl;
        return 0;
    }
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLenum format = GL_RGB;
    if (nrChannels == 1) format=GL_RED;
    else if (nrChannels == 3) format=GL_RGB;
    else if (nrChannels == 4) format=GL_RGBA;

    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);
    return texID;
}

// Setup scene with many objects
void setupScene(std::vector<SceneObject>& sceneObjects, Mesh& cubeMesh) {
    sceneObjects.clear();

    // Floor
    sceneObjects.push_back({cubeMesh, {0,-0.01f,0}, {0,0,0}, {50,0.02,50}, "Floor"});

    // Walls
    sceneObjects.push_back({cubeMesh, {0,2.5f,-25}, {0,0,0}, {50,5,0.2}, "Wall_Back"});
    sceneObjects.push_back({cubeMesh, {0,2.5f,25}, {0,0,0}, {50,5,0.2}, "Wall_Front"});
    sceneObjects.push_back({cubeMesh, {-25,2.5f,0}, {0,0,0}, {0.2,5,50}, "Wall_Left"});
    sceneObjects.push_back({cubeMesh, {25,2.5f,0}, {0,0,0}, {0.2,5,50}, "Wall_Right"});

    // Multiple Tables with chairs - spread across the room
    float startX = -18;
    float startZ = -12;
    int rows=4, cols=4;
    float spacingX=12, spacingZ=8;
    for(int i=0; i<rows; ++i){
        for(int j=0; j<cols; ++j){
            float x = startX + i*spacingX;
            float z = startZ + j*spacingZ;
            // Table
            sceneObjects.push_back({cubeMesh, {x,0.75f,z}, {0,0,0}, {3,0.75,3}, "Table"});
            // Chairs at four corners
            sceneObjects.push_back({cubeMesh, {x-1.5f,0.25f,z-1.5f}, {0,0,0}, {1,0.5,1}, "Chair"});
            sceneObjects.push_back({cubeMesh, {x+1.5f,0.25f,z-1.5f}, {0,0,0}, {1,0.5,1}, "Chair"});
            sceneObjects.push_back({cubeMesh, {x-1.5f,0.25f,z+1.5f}, {0,0,0}, {1,0.5,1}, "Chair"});
            sceneObjects.push_back({cubeMesh, {x+1.5f,0.25f,z+1.5f}, {0,0,0}, {1,0.5,1}, "Chair"});
        }
    }

    // Bar and counter
    sceneObjects.push_back({cubeMesh, {20,1.0f,-10}, {0,0,0}, {4,1,8}, "BarCounter"});
    // Stools for bar
    for (int i=0; i<6; ++i){
        float x = 18 + i*1.2f;
        sceneObjects.push_back({cubeMesh, {x,0.25f,-10}, {0,0,0}, {0.5,0.5,0.5}, "BarStool"});
    }

    // Food station / kitchen
    sceneObjects.push_back({cubeMesh, {-20,1.0f,15}, {0,0,0}, {8,2,4}, "Kitchen"});

    // Decorations
    sceneObjects.push_back({cubeMesh, {0,0.25f,0}, {0,0,0}, {0.5,0.5,0.5}, "Plant1"});
    sceneObjects.push_back({cubeMesh, {10,0.25f,-20}, {0,0,0}, {0.5,0.5,0.5}, "Plant2"});
    sceneObjects.push_back({cubeMesh, {-10,0.25f,10}, {0,0,0}, {0.5,0.5,0.5}, "Plant3"});
}
