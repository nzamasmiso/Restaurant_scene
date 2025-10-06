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

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

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

// Lighting
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

    // Point lights
    for (int i=0; i<4; ++i) {
        vec3 toLight = normalize(pointLights[i].position - FragPos);
        float diffPL = max(dot(norm, toLight), 0.0);
        vec3 diffusePL = diffPL * pointLights[i].color * pointLights[i].intensity;
        vec3 halfwayDir = normalize(toLight + viewDir);
        float spec = pow(max(dot(norm, halfwayDir), 0.0), shininess);
        vec3 specular = spec * pointLights[i].color * pointLights[i].intensity;
        result += diffusePL + specular;
    }

    // Spotlights
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

// Data structures
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
    GLuint textureID; // Texture for this object
};

// Globals
GLFWwindow* window;
unsigned int SCR_WIDTH=1280, SCR_HEIGHT=720;
float yaw=45.0f, radius=80.0f;
bool showUI=true;
bool dayMode=true;
GLuint shaderProgram;

// Function prototypes
GLuint compileShader(GLenum type, const char* src);
GLuint createShaderProgram();
Mesh createCubeMesh();
GLuint LoadTexture(const char* filepath);
void setupScene(std::vector<SceneObject>& sceneObjects, Mesh& cubeMesh);
void drawScene(const std::vector<SceneObject>& sceneObjects, const glm::mat4& view, const glm::mat4& projection);

int main() {
    // GLFW init
    if (!glfwInit()) { std::cerr<<"Failed to init GLFW"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window=glfwCreateWindow(SCR_WIDTH,SCR_HEIGHT,"Scene with Textured Cubes",nullptr,nullptr);
    if (!window){ std::cerr<<"Failed to create GLFW"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glewExperimental=true; glewInit();

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window,true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Compile shader
    GLuint shader=createShaderProgram();
    glUseProgram(shader);

    // Create cube mesh
    Mesh cubeMesh = createCubeMesh();

    // Load textures
    GLuint wallTexture = LoadTexture("textures/wall.jpg");
    GLuint floorTexture = LoadTexture("textures/floor.jpg");
    GLuint tableTexture = LoadTexture("textures/table.jpg");
    GLuint chairTexture = LoadTexture("textures/chair.jpg");
    GLuint barTexture = LoadTexture("textures/bar.jpg");
    GLuint kitchenTexture = LoadTexture("textures/kitchen.jpg");
    GLuint sofaTexture = LoadTexture("textures/sofa.jpg");
    GLuint plantTexture = LoadTexture("textures/plant.jpg");

    // Build scene with textured cubes
    std::vector<SceneObject> sceneObjects;
    setupScene(sceneObjects, cubeMesh);

    // Assign textures to objects based on their name
    for (auto& obj : sceneObjects) {
        if (obj.name.find("Wall") != std::string::npos) obj.textureID = wallTexture;
        else if (obj.name.find("Floor") != std::string::npos) obj.textureID = floorTexture;
        else if (obj.name.find("Table") != std::string::npos) obj.textureID = tableTexture;
        else if (obj.name.find("Chair") != std::string::npos) obj.textureID = chairTexture;
        else if (obj.name.find("Bar") != std::string::npos) obj.textureID = barTexture;
        else if (obj.name.find("Kitchen") != std::string::npos) obj.textureID = kitchenTexture;
        else if (obj.name.find("Sofa") != std::string::npos) obj.textureID = sofaTexture;
        else if (obj.name.find("Plant") != std::string::npos) obj.textureID = plantTexture;
        else obj.textureID = wallTexture; // default fallback
    }

    glEnable(GL_DEPTH_TEST);

    // Main loop
    while (!glfwWindowShouldClose(window)){
        // Input
        if(glfwGetKey(window,GLFW_KEY_ESCAPE)==GLFW_PRESS)
            glfwSetWindowShouldClose(window,true);

        // Camera position
        float camX=cos(glm::radians(yaw))*radius;
        float camZ=sin(glm::radians(yaw))*radius;
        glm::vec3 camPos(camX,20,camZ);
        glm::mat4 view=glm::lookAt(camPos, glm::vec3(0), glm::vec3(0,1,0));
        glm::mat4 projection=glm::perspective(glm::radians(45.0f),(float)SCR_WIDTH/SCR_HEIGHT,0.1f,200.0f);

        // Clear
        if(dayMode) glClearColor(0.5f,0.8f,1.0f,1);
        else glClearColor(0.1f,0.1f,0.2f,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        // Draw scene
        glUseProgram(shader);
        // Set lights
        glUniform3f(glGetUniformLocation(shader,"dirLight.position"), 0.0f, 10.0f, 0.0f);
        glUniform3f(glGetUniformLocation(shader,"dirLight.color"), 1.0f,1.0f,1.0f);
        glUniform1f(glGetUniformLocation(shader,"dirLight.intensity"), 0.5f);
        glUniform3f(glGetUniformLocation(shader,"pointLights[0].position"), 5.0f,4.0f,5.0f);
        glUniform3f(glGetUniformLocation(shader,"pointLights[0].color"), 1.0f,0.8f,0.6f);
        glUniform1f(glGetUniformLocation(shader,"pointLights[0].intensity"), 0.8f);
        glUniform3f(glGetUniformLocation(shader,"pointLights[1].position"), -5.0f,4.0f,-5.0f);
        glUniform3f(glGetUniformLocation(shader,"pointLights[1].color"), 1.0f,0.8f,0.6f);
        glUniform1f(glGetUniformLocation(shader,"pointLights[1].intensity"), 0.8f);
        // Spotlights
        glUniform3f(glGetUniformLocation(shader,"spotLights[0].position"), 0.0f, 8.0f, 0.0f);
        glUniform3f(glGetUniformLocation(shader,"spotLights[0].color"), 1.0f,1.0f,0.8f);
        glUniform1f(glGetUniformLocation(shader,"spotLights[0].intensity"), 1.0f);
        glUniform3f(glGetUniformLocation(shader,"spotLights[1].position"), 10.0f,8.0f,-10.0f);
        glUniform3f(glGetUniformLocation(shader,"spotLights[1].color"), 1.0f,1.0f,0.8f);
        glUniform1f(glGetUniformLocation(shader,"spotLights[1].intensity"), 1.0f);

        glUniform3f(glGetUniformLocation(shader,"viewPos"), camX,20,camZ);
        glUniform1f(glGetUniformLocation(shader,"shininess"), 64.0f);

        // Draw objects
        for (const auto& obj : sceneObjects) {
            glm::mat4 model=glm::translate(glm::mat4(1.0f), obj.pos);
            model=glm::rotate(model, glm::radians(obj.rot.x), glm::vec3(1,0,0));
            model=glm::rotate(model, glm::radians(obj.rot.y), glm::vec3(0,1,0));
            model=glm::rotate(model, glm::radians(obj.rot.z), glm::vec3(0,0,1));
            model=glm::scale(model, obj.scale);
            glUniformMatrix4fv(glGetUniformLocation(shader,"model"),1,GL_FALSE,&model[0][0]);

            // Bind texture
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, obj.textureID);
            glUniform1i(glGetUniformLocation(shader, "albedoMap"), 0);

            glBindVertexArray(obj.mesh.VAOs[0]);
            glDrawElements(GL_TRIANGLES, obj.mesh.indexCounts[0], GL_UNSIGNED_INT, 0);
        }

        // UI
        if (showUI) {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGui::Begin("Controls");
            ImGui::Checkbox("Day Mode", &dayMode);
            ImGui::Checkbox("Show UI", &showUI);
            ImGui::SliderFloat("Yaw", &yaw, 0, 360);
            ImGui::SliderFloat("Cam Distance", &radius, 20, 150);
            ImGui::End();

            ImGui::Begin("Lighting");
            ImGui::Text("Ambient, Directional, Point, Spot Lights");
            ImGui::End();

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

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
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

// Shader compilation
GLuint compileShader(GLenum type, const char* src) {
    GLuint s=glCreateShader(type);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);
    int success; glGetShaderiv(s,GL_COMPILE_STATUS,&success);
    if(!success){ char info[512]; glGetShaderInfoLog(s,512,nullptr,info); std::cerr<<"Shader error: "<<info<<"\n"; }
    return s;
}

GLuint createShaderProgram() {
    GLuint v=compileShader(GL_VERTEX_SHADER,vertexShaderSrc);
    GLuint f=compileShader(GL_FRAGMENT_SHADER,fragmentShaderSrc);
    GLuint prog=glCreateProgram();
    glAttachShader(prog,v); glAttachShader(prog,f);
    glLinkProgram(prog);
    int success; glGetProgramiv(prog,GL_LINK_STATUS,&success);
    if(!success){ char info[512]; glGetProgramInfoLog(prog,512,nullptr,info); std::cerr<<"Program link error: "<<info<<"\n"; }
    glDeleteShader(v); glDeleteShader(f);
    return prog;
}

// Create cube geometry
Mesh createCubeMesh() {
    Mesh mesh;
    float vertices[] = {
        // positions             normals           texcoords
        // Front face
        -0.5f,-0.5f, 0.5f,      0,0,1,             0,0,
         0.5f,-0.5f, 0.5f,      0,0,1,             1,0,
         0.5f, 0.5f, 0.5f,      0,0,1,             1,1,
        -0.5f, 0.5f, 0.5f,      0,0,1,             0,1,
        // Back face
        -0.5f,-0.5f,-0.5f,      0,0,-1,            0,0,
         0.5f,-0.5f,-0.5f,      0,0,-1,            1,0,
         0.5f, 0.5f,-0.5f,      0,0,-1,            1,1,
        -0.5f, 0.5f,-0.5f,      0,0,-1,            0,1,
        // Left face
        -0.5f,-0.5f,-0.5f,     -1,0,0,             0,0,
        -0.5f, 0.5f,-0.5f,     -1,0,0,             1,0,
        -0.5f, 0.5f, 0.5f,     -1,0,0,             1,1,
        -0.5f,-0.5f, 0.5f,     -1,0,0,             0,1,
        // Right face
         0.5f,-0.5f,-0.5f,      1,0,0,             0,0,
         0.5f, 0.5f,-0.5f,      1,0,0,             1,0,
         0.5f, 0.5f, 0.5f,      1,0,0,             1,1,
         0.5f,-0.5f, 0.5f,      1,0,0,             0,1,
        // Top face
        -0.5f, 0.5f, 0.5f,      0,1,0,             0,0,
         0.5f, 0.5f, 0.5f,      0,1,0,             1,0,
         0.5f, 0.5f,-0.5f,      0,1,0,             1,1,
        -0.5f, 0.5f,-0.5f,      0,1,0,             0,1,
        // Bottom face
        -0.5f,-0.5f, 0.5f,      0,-1,0,            0,0,
         0.5f,-0.5f, 0.5f,      0,-1,0,            1,0,
         0.5f,-0.5f,-0.5f,      0,-1,0,            1,1,
        -0.5f,-0.5f,-0.5f,      0,-1,0,            0,1,
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

// Load texture from file
GLuint LoadTexture(const char* filepath) {
    int width, height, nrChannels;
    unsigned char* data = stbi_load(filepath, &width, &height, &nrChannels, 0);
    if (!data) {
        std::cerr << "Failed to load texture: " << filepath << std::endl;
        return 0;
    }
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Set wrapping/filtering options
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);	
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLenum format = GL_RGB;
    if (nrChannels == 1)
        format = GL_RED;
    else if (nrChannels == 3)
        format = GL_RGB;
    else if (nrChannels == 4)
        format = GL_RGBA;

    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);

    return textureID;
}

// Set up scene objects with positions, rotations, scales
void setupScene(std::vector<SceneObject>& sceneObjects, Mesh& cubeMesh) {
    // Walls
    sceneObjects.push_back({cubeMesh, {-50,1.5f,0}, {0,0,0}, {0.2f,3,100}, "Wall_Left"});
    sceneObjects.push_back({cubeMesh, {50,1.5f,0}, {0,0,0}, {0.2f,3,100}, "Wall_Right"});
    sceneObjects.push_back({cubeMesh, {0,1.5f,-50}, {0,0,0}, {100,3,0.2f}, "Wall_Front"});
    sceneObjects.push_back({cubeMesh, {0,1.5f,50}, {0,0,0}, {100,3,0.2f}, "Wall_Back"});

    // Partition
    sceneObjects.push_back({cubeMesh, {0,1.5f,-20}, {0,0,0}, {40,3,0.2f}, "Partition"});

    // Main door
    sceneObjects.push_back({cubeMesh, {-25,1, -50}, {0,0,0}, {3,2,0.2f}, "Main Door"});

    // Tables
    sceneObjects.push_back({cubeMesh, {-15,0.75f,-10}, {0,0,0}, {4,0.75,4}, "Table1"});
    sceneObjects.push_back({cubeMesh, {0,0.75f,-10}, {0,0,0}, {4,0.75,4}, "Table2"});
    sceneObjects.push_back({cubeMesh, {15,0.75f,-10}, {0,0,0}, {4,0.75,4}, "Table3"});

    // Chairs
    sceneObjects.push_back({cubeMesh, {-15,0.25f,-10}, {0,0,0}, {1,0.5,1}, "Chair1"});
    sceneObjects.push_back({cubeMesh, {0,0.25f,-10}, {0,0,0}, {1,0.5,1}, "Chair2"});
    sceneObjects.push_back({cubeMesh, {15,0.25f,-10}, {0,0,0}, {1,0.5,1}, "Chair3"});

    // Bar
    sceneObjects.push_back({cubeMesh, {-20,1.5f,3}, {0,0,0}, {6,1.5,2}, "Bar"});

    // Drop-down counter
    sceneObjects.push_back({cubeMesh, {-20,1.5f,1}, {0,0,0}, {4,1.5,0.5}, "DropDown"});

    // Kitchen
    sceneObjects.push_back({cubeMesh, {20,1.5f,-10}, {0,0,0}, {8,3,4}, "Kitchen"});

    // Sofas
    sceneObjects.push_back({cubeMesh, {30,0.75f,20}, {0,0,0}, {4,0.75,2}, "Sofa1"});
    sceneObjects.push_back({cubeMesh, {35,0.75f,20}, {0,0,0}, {4,0.75,2}, "Sofa2"});

    // Coffee table
    sceneObjects.push_back({cubeMesh, {32.5,0.25f,22}, {0,0,0}, {2,0.25,1}, "CoffeeTable"});

    // Plants
    sceneObjects.push_back({cubeMesh, {10,0.5f,10}, {0,0,0}, {0.2f,0.2f,0.2f}, "Plant1"});
    sceneObjects.push_back({cubeMesh, {-10,0.5f,-10}, {0,0,0}, {0.2f,0.2f,0.2f}, "Plant2"});
}

// Draw scene with textures and lighting
void drawScene(const std::vector<SceneObject>& sceneObjects, const glm::mat4& view, const glm::mat4& projection) {
    GLuint shader = glGetIntegerv(GL_CURRENT_PROGRAM);
    // Set lights
    glUniform3f(glGetUniformLocation(shader,"dirLight.position"), 0.0f, 10.0f, 0.0f);
    glUniform3f(glGetUniformLocation(shader,"dirLight.color"), 1.0f,1.0f,1.0f);
    glUniform1f(glGetUniformLocation(shader,"dirLight.intensity"), 0.5f);
    glUniform3f(glGetUniformLocation(shader,"pointLights[0].position"), 5.0f,4.0f,5.0f);
    glUniform3f(glGetUniformLocation(shader,"pointLights[0].color"), 1.0f,0.8f,0.6f);
    glUniform1f(glGetUniformLocation(shader,"pointLights[0].intensity"), 0.8f);
    glUniform3f(glGetUniformLocation(shader,"pointLights[1].position"), -5.0f,4.0f,-5.0f);
    glUniform3f(glGetUniformLocation(shader,"pointLights[1].color"), 1.0f,0.8f,0.6f);
    glUniform1f(glGetUniformLocation(shader,"pointLights[1].intensity"), 0.8f);
    // Spotlights
    glUniform3f(glGetUniformLocation(shader,"spotLights[0].position"), 0.0f, 8.0f, 0.0f);
    glUniform3f(glGetUniformLocation(shader,"spotLights[0].color"), 1.0f,1.0f,0.8f);
    glUniform1f(glGetUniformLocation(shader,"spotLights[0].intensity"), 1.0f);
    glUniform3f(glGetUniformLocation(shader,"spotLights[1].position"), 10.0f,8.0f,-10.0f);
    glUniform3f(glGetUniformLocation(shader,"spotLights[1].color"), 1.0f,1.0f,0.8f);
    glUniform1f(glGetUniformLocation(shader,"spotLights[1].intensity"), 1.0f);

    // Camera
    float camX=cos(glm::radians(yaw))*radius;
    float camZ=sin(glm::radians(yaw))*radius;
    glm::vec3 viewPos(camX,20,camZ);
    glUniform3f(glGetUniformLocation(shader,"viewPos"), viewPos.x, viewPos.y, viewPos.z);
    glUniform1f(glGetUniformLocation(shader,"shininess"), 64.0f);

    // Draw objects
    for (const auto& obj : sceneObjects) {
        glm::mat4 model=glm::translate(glm::mat4(1.0f), obj.pos);
        model=glm::rotate(model, glm::radians(obj.rot.x), glm::vec3(1,0,0));
        model=glm::rotate(model, glm::radians(obj.rot.y), glm::vec3(0,1,0));
        model=glm::rotate(model, glm::radians(obj.rot.z), glm::vec3(0,0,1));
        model=glm::scale(model, obj.scale);
        glUniformMatrix4fv(glGetUniformLocation(shader,"model"),1,GL_FALSE,&model[0][0]);
        // Bind texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, obj.textureID);
        glUniform1i(glGetUniformLocation(shader, "albedoMap"), 0);
        glBindVertexArray(obj.mesh.VAOs[0]);
        glDrawElements(GL_TRIANGLES, obj.mesh.indexCounts[0], GL_UNSIGNED_INT, 0);
    }
}

int main() {
    // GLFW init
    if (!glfwInit()) { std::cerr<<"Failed to init GLFW"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window=glfwCreateWindow(SCR_WIDTH,SCR_HEIGHT,"Textured Cubes Scene",nullptr,nullptr);
    if (!window){ std::cerr<<"Failed to create GLFW"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glewExperimental=true; glewInit();

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window,true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Compile shader
    GLuint shader=createShaderProgram();
    glUseProgram(shader);

    // Create cube mesh
    Mesh cubeMesh = createCubeMesh();

    // Load textures
    GLuint wallTexture = LoadTexture("textures/wall.jpg");
    GLuint floorTexture = LoadTexture("textures/floor.jpg");
    GLuint tableTexture = LoadTexture("textures/table.jpg");
    GLuint chairTexture = LoadTexture("textures/chair.jpg");
    GLuint barTexture = LoadTexture("textures/bar.jpg");
    GLuint kitchenTexture = LoadTexture("textures/kitchen.jpg");
    GLuint sofaTexture = LoadTexture("textures/sofa.jpg");
    GLuint plantTexture = LoadTexture("textures/plant.jpg");

    // Build scene with textured cubes
    std::vector<SceneObject> sceneObjects;
    setupScene(sceneObjects, cubeMesh);

    // Assign textures based on name
    for (auto& obj : sceneObjects) {
        if (obj.name.find("Wall") != std::string::npos) obj.textureID = wallTexture;
        else if (obj.name.find("Floor") != std::string::npos) obj.textureID = floorTexture;
        else if (obj.name.find("Table") != std::string::npos) obj.textureID = tableTexture;
        else if (obj.name.find("Chair") != std::string::npos) obj.textureID = chairTexture;
        else if (obj.name.find("Bar") != std::string::npos) obj.textureID = barTexture;
        else if (obj.name.find("Kitchen") != std::string::npos) obj.textureID = kitchenTexture;
        else if (obj.name.find("Sofa") != std::string::npos) obj.textureID = sofaTexture;
        else if (obj.name.find("Plant") != std::string::npos) obj.textureID = plantTexture;
        else obj.textureID = wallTexture; // fallback
    }

    glEnable(GL_DEPTH_TEST);

    // Main loop
    while (!glfwWindowShouldClose(window)){
        // Input
        if(glfwGetKey(window,GLFW_KEY_ESCAPE)==GLFW_PRESS)
            glfwSetWindowShouldClose(window,true);

        // Camera
        float camX=cos(glm::radians(yaw))*radius;
        float camZ=sin(glm::radians(yaw))*radius;
        glm::vec3 camPos(camX,20,camZ);
        glm::mat4 view=glm::lookAt(camPos, glm::vec3(0), glm::vec3(0,1,0));
        glm::mat4 projection=glm::perspective(glm::radians(45.0f),(float)SCR_WIDTH/SCR_HEIGHT,0.1f,200.0f);

        // Clear
        if(dayMode) glClearColor(0.5f,0.8f,1.0f,1);
        else glClearColor(0.1f,0.1f,0.2f,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        // Draw scene
        drawScene(sceneObjects, view, projection);

        // UI
        if (showUI) {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGui::Begin("Controls");
            ImGui::Checkbox("Day Mode", &dayMode);
            ImGui::Checkbox("Show UI", &showUI);
            ImGui::SliderFloat("Yaw", &yaw, 0, 360);
            ImGui::SliderFloat("Cam Distance", &radius, 20, 150);
            ImGui::End();

            ImGui::Begin("Lighting");
            ImGui::Text("Ambient, Directional, Point, Spot Lights");
            ImGui::End();

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

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
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    return 0;
}
