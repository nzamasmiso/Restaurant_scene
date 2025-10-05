#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Include GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// Shader sources
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
    FragPos = vec3(model * vec4(aPos,1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;
    gl_Position = projection * view * vec4(FragPos,1.0);
}
)";

const char* fragmentShaderSrc = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform vec3 lightPos;
uniform vec3 viewPos;

void main() {
    vec3 albedo = texture(albedoMap, TexCoord).rgb;
    vec3 normalMapSample = texture(normalMap, TexCoord).rgb * 2.0 - 1.0;
    vec3 N = normalize(Normal + normalMapSample);
    vec3 L = normalize(lightPos - FragPos);
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * albedo;
    vec3 ambient = 0.2 * albedo;
    vec3 V = normalize(viewPos - FragPos);
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), 32);
    vec3 specular = spec * vec3(1.0);
    vec3 color = ambient + diffuse + specular;
    FragColor = vec4(color, 1.0);
}
)";

// Globals
GLFWwindow* window;
unsigned int SCR_WIDTH=1280, SCR_HEIGHT=720;
float yaw=0.0f, pitch=20.0f, radius=80.0f;
bool showUI=true;
bool dayMode=true;

// Textures
std::unordered_map<std::string, GLuint> textures;

// Shader program
GLuint shaderProgram;

// Mesh struct
struct Mesh {
    GLuint VAO, VBO, EBO;
    unsigned int indexCount;
    GLuint albedoTex, normalTex;
};

// Scene object
struct SceneObject {
    Mesh mesh;
    glm::vec3 pos;
    glm::vec3 rot;
    glm::vec3 scale;
    std::string name;
};
std::vector<SceneObject> sceneObjects;

// Compile shader
GLuint compileShader(GLenum type, const char* src){
    GLuint s=glCreateShader(type);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);
    int success; glGetShaderiv(s,GL_COMPILE_STATUS,&success);
    if(!success){ char info[512]; glGetShaderInfoLog(s,512,nullptr,info); std::cerr<<"Shader compile error:"<<info<<"\n"; }
    return s;
}

GLuint createShaderProgram() {
    GLuint v=compileShader(GL_VERTEX_SHADER,vertexShaderSrc);
    GLuint f=compileShader(GL_FRAGMENT_SHADER,fragmentShaderSrc);
    GLuint prog=glCreateProgram();
    glAttachShader(prog,v); glAttachShader(prog,f);
    glLinkProgram(prog);
    glDeleteShader(v); glDeleteShader(f);
    return prog;
}

bool loadTexture(const std::string& path, GLuint& texID) {
    int w,h,c;
    unsigned char* data=stbi_load(path.c_str(),&w,&h,&c,0);
    if(!data){ std::cerr<<"Failed to load: "<<path<<"\n"; return false; }
    glGenTextures(1,&texID);
    glBindTexture(GL_TEXTURE_2D,texID);
    GLenum format=(c==4)?GL_RGBA:GL_RGB;
    glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,format,GL_UNSIGNED_BYTE,data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    stbi_image_free(data);
    return true;
}

// Create cube mesh
Mesh createCubeMesh(GLuint albedoTex, GLuint normalTex) {
    float vertices[] = {
        // position         normal          texcoords
        -0.5f,-0.5f, 0.5f,  0,0,1,         0,0,
         0.5f,-0.5f, 0.5f,  0,0,1,         1,0,
         0.5f, 0.5f, 0.5f,  0,0,1,         1,1,
        -0.5f, 0.5f, 0.5f,  0,0,1,         0,1,
        // back
        -0.5f,-0.5f,-0.5f,  0,0,-1,        0,0,
         0.5f,-0.5f,-0.5f,  0,0,-1,        1,0,
         0.5f, 0.5f,-0.5f,  0,0,-1,        1,1,
        -0.5f, 0.5f,-0.5f,  0,0,-1,        0,1,
        // left
        -0.5f,-0.5f,-0.5f, -1,0,0,         0,0,
        -0.5f, 0.5f,-0.5f, -1,0,0,         1,0,
        -0.5f, 0.5f, 0.5f, -1,0,0,         1,1,
        -0.5f,-0.5f, 0.5f, -1,0,0,         0,1,
        // right
         0.5f,-0.5f,-0.5f,  1,0,0,         0,0,
         0.5f, 0.5f,-0.5f,  1,0,0,         1,0,
         0.5f, 0.5f, 0.5f,  1,0,0,         1,1,
         0.5f,-0.5f, 0.5f,  1,0,0,         0,1,
        // top
        -0.5f, 0.5f, 0.5f,  0,1,0,         0,0,
         0.5f, 0.5f, 0.5f,  0,1,0,         1,0,
         0.5f, 0.5f,-0.5f,  0,1,0,         1,1,
        -0.5f, 0.5f,-0.5f,  0,1,0,         0,1,
        // bottom
        -0.5f,-0.5f, 0.5f,  0,-1,0,        0,0,
         0.5f,-0.5f, 0.5f,  0,-1,0,        1,0,
         0.5f,-0.5f,-0.5f,  0,-1,0,        1,1,
        -0.5f,-0.5f,-0.5f,  0,-1,0,        0,1,
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
    // position
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    // normal
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    // texcoords
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(6*sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    return {VAO, VBO, EBO, sizeof(indices)/sizeof(unsigned int), albedoTex, normalTex};
}

// Draw mesh with transform
void drawMesh(const Mesh& mesh, glm::mat4 model) {
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram,"model"),1,false,&model[0][0]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mesh.albedoTex);
    glUniform1i(glGetUniformLocation(shaderProgram,"albedoMap"),0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mesh.normalTex);
    glUniform1i(glGetUniformLocation(shaderProgram,"normalMap"),1);
    glBindVertexArray(mesh.VAO);
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
}

// Setup scene objects matching your sketch
void setupScene() {
    // Load textures
    loadTexture("textures/floor.jpg", textures["floor"]);
    loadTexture("textures/wall.jpg", textures["wall"]);
    loadTexture("textures/table.jpg", textures["table"]);
    loadTexture("textures/chair.jpg", textures["chair"]);
    loadTexture("textures/bar.jpg", textures["bar"]);
    loadTexture("textures/sofa.jpg", textures["sofa"]);
    loadTexture("textures/kitchen.jpg", textures["kitchen"]);
    loadTexture("textures/door.jpg", textures["door"]);
    loadTexture("textures/window.jpg", textures["window"]);
    loadTexture("textures/plant.jpg", textures["plant"]);

    // Create cube mesh
    Mesh cubeMesh = createCubeMesh(textures["wall"], textures["wall"]);

    // Walls perimeter (positions)
    sceneObjects.push_back({cubeMesh, {-50,1.5,0}, {0,0,0}, {0.2,3,100}, "Left Wall"});
    sceneObjects.push_back({cubeMesh, {50,1.5,0}, {0,0,0}, {0.2,3,100}, "Right Wall"});
    sceneObjects.push_back({cubeMesh, {0,1.5,-50}, {0,0,0}, {100,3,0.2}, "Front Wall"});
    sceneObjects.push_back({cubeMesh, {0,1.5,50}, {0,0,0}, {100,3,0.2}, "Back Wall"});

    // Internal partition
    sceneObjects.push_back({cubeMesh, {0,1.5,-20}, {0,0,0}, {40,3,0.2}, "Partition"});

    // Entrance Door
    sceneObjects.push_back({cubeMesh, {-25,1, -50}, {0,0,0}, {3,2,0.2}, "Main Door"});

    // Dining Tables
    sceneObjects.push_back({cubeMesh, {-15,0.75,-10}, {0,0,0}, {4,0.75,4}, "Table1"});
    sceneObjects.push_back({cubeMesh, {0,0.75,-10}, {0,0,0}, {4,0.75,4}, "Table2"});
    sceneObjects.push_back({cubeMesh, {15,0.75,-10}, {0,0,0}, {4,0.75,4}, "Table3"});

    // Chairs around tables
    for(float dx=-1.5; dx<=1.5; dx+=3)
        for(float dz=-1.5; dz<=1.5; dz+=3)
            sceneObjects.push_back({cubeMesh, {dx,0.25,dz-10}, {0,0,0}, {1,0.5,1}, "Chair"});

    // Bar area
    sceneObjects.push_back({cubeMesh, {-20,1.5,3}, {0,0,0}, {6,1.5,2}, "Bar"});

    // Kitchen
    sceneObjects.push_back({cubeMesh, {20,1,0}, {0,0,0}, {8,3,4}, "Kitchen Counter"});
    sceneObjects.push_back({cubeMesh, {20,1.5,2}, {0,0,0}, {2,1.5,2}, "Stove"});
    sceneObjects.push_back({cubeMesh, {18,1.5,8}, {0,0,0}, {2,1.5,2}, "Fridge"});

    // Lounge (sofas)
    sceneObjects.push_back({cubeMesh, {30,0.75,20}, {0,0,0}, {4,0.75,2}, "Sofa1"});
    sceneObjects.push_back({cubeMesh, {35,0.75,20}, {0,0,0}, {4,0.75,2}, "Sofa2"});
    sceneObjects.push_back({cubeMesh, {32.5,0.25,22}, {0,0,0}, {2,0.25,1}, "CoffeeTable"});

    // Restroom
    sceneObjects.push_back({cubeMesh, {-45,1,-45}, {0,0,0}, {3,3,0.2}, "Restroom Wall"});
    sceneObjects.push_back({cubeMesh, {-45,0.75,-45}, {0,0,0}, {1,0.75,1}, "Toilet"});
    sceneObjects.push_back({cubeMesh, {-45.5,0.75,-45}, {0,0,0}, {1,0.75,1}, "Sink"});

    // Decorations
    sceneObjects.push_back({cubeMesh, {10,0.5,10}, {0,0,0}, {0.2,0.2,0.2}, "Plant1"});
    sceneObjects.push_back({cubeMesh, {-10,0.5,-10}, {0,0,0}, {0.2,0.2,0.2}, "Plant2"});
}

// Draw scene objects
void drawScene(const glm::mat4& view, const glm::mat4& projection) {
    glUseProgram(shaderProgram);
    glUniform3f(glGetUniformLocation(shaderProgram,"lightPos"), -10,20,10);
    glm::vec3 camPos;
    camPos.x=cos(glm::radians(yaw))*radius;
    camPos.z=sin(glm::radians(yaw))*radius;
    camPos.y=20;
    glUniform3f(glGetUniformLocation(shaderProgram,"viewPos"), camPos.x, camPos.y, camPos.z);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram,"view"),1,false,&view[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram,"projection"),1,false,&projection[0][0]);

    for(auto& obj: sceneObjects) {
        glm::mat4 model=glm::translate(glm::mat4(1.0f), obj.pos);
        model=glm::rotate(model, glm::radians(obj.rot.x), glm::vec3(1,0,0));
        model=glm::rotate(model, glm::radians(obj.rot.y), glm::vec3(0,1,0));
        model=glm::rotate(model, glm::radians(obj.rot.z), glm::vec3(0,0,1));
        model=glm::scale(model, obj.scale);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram,"model"),1,false,&model[0][0]);
        glBindTexture(GL_TEXTURE_2D, textures["wall"]);
        glBindVertexArray(obj.mesh.VAO);
        glDrawElements(GL_TRIANGLES, obj.mesh.indexCount, GL_UNSIGNED_INT, 0);
    }
}

// Draw labels using ImGui
void drawLabels() {
    ImGui::Begin("Labels");
    ImGui::Text("Main Entrance");
    ImGui::Text("Lobby");
    ImGui::Text("Dining Area");
    ImGui::Text("Bar Area");
    ImGui::Text("Kitchen");
    ImGui::Text("Lounge");
    ImGui::Text("Restroom");
    ImGui::End();
}

// Initialize all
int main() {
    // GLFW init
    if (!glfwInit()) { std::cerr << "Failed to init GLFW"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    window=glfwCreateWindow(SCR_WIDTH,SCR_HEIGHT,"Restaurant Sketch Scene",nullptr,nullptr);
    if (!window){ std::cerr<<"Failed to create window"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glewExperimental=true; glewInit();

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io=ImGui::GetIO();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window,true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Compile shaders
    shaderProgram=createShaderProgram();

    // Setup scene objects
    setupScene();

    // Enable depth test
    glEnable(GL_DEPTH_TEST);

    // Main loop
    while(!glfwWindowShouldClose(window)){
        // Input
        if(glfwGetKey(window,GLFW_KEY_ESCAPE)==GLFW_PRESS)
            glfwSetWindowShouldClose(window,true);

        // Camera position
        float camX=cos(glm::radians(yaw))*radius;
        float camZ=sin(glm::radians(yaw))*radius;
        glm::vec3 camPos(camX,20,camZ);
        glm::mat4 view=glm::lookAt(camPos, glm::vec3(0,0,0), glm::vec3(0,1,0));
        glm::mat4 projection=glm::perspective(glm::radians(45.0f),(float)SCR_WIDTH/SCR_HEIGHT,0.1f,200.0f);

        // Clear
        if(dayMode) glClearColor(0.5f,0.8f,1.0f,1);
        else glClearColor(0.1f,0.1f,0.2f,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        // Draw scene
        drawScene(view, projection);

        // UI
        if(showUI){
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // Controls
            ImGui::Begin("Controls");
            ImGui::Checkbox("Day Mode", &dayMode);
            ImGui::Checkbox("Show UI", &showUI);
            ImGui::SliderFloat("Camera Yaw", &yaw, 0, 360);
            ImGui::SliderFloat("Camera Distance", &radius, 20, 150);
            ImGui::End();

            // Labels
            drawLabels();

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        // Swap buffers
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    for(auto& obj: sceneObjects){
        glDeleteVertexArrays(1,&obj.mesh.VAO);
        glDeleteBuffers(1,&obj.mesh.VBO);
        glDeleteBuffers(1,&obj.mesh.EBO);
    }
    glDeleteProgram(shaderProgram);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
