
// restaurant_floorplan_modern.cpp
// Modern OpenGL 3.3 core + ImGui single-file 2D restaurant floor plan (responsive, shader-based).

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <cmath>
#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------- Shader sources ----------------------
const char* VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;

out vec4 vColor;
out vec2 vUV;

uniform mat4 uMVP;

void main() {
    vColor = aColor;
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)";

const char* FRAG_SRC = R"(
#version 330 core
in vec4 vColor;
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uTexture;
uniform bool useTexture;

void main() {
    if (useTexture)
        FragColor = texture(uTexture, vUV) * vColor;
    else
        FragColor = vColor;
}
)";

// ---------------------- Shader utilities ----------------------
static GLuint compileShader(GLenum t, const char* src){
    GLuint s = glCreateShader(t);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);
    GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){ char log[1024]; glGetShaderInfoLog(s,1024,nullptr,log); std::cerr<<"Shader compile error: "<<log<<"\n"; }
    return s;
}
static GLuint createProgram(const char* vs,const char* fs){
    GLuint vsId = compileShader(GL_VERTEX_SHADER, vs);
    GLuint fsId = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, vsId); glAttachShader(p, fsId);
    glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok){ char log[1024]; glGetProgramInfoLog(p,1024,nullptr,log); std::cerr<<"Link error: "<<log<<"\n"; }
    glDeleteShader(vsId); glDeleteShader(fsId);
    return p;
}

// ---------------------- Draw buffer ----------------------
struct DrawBuffer {
    std::vector<float> data; // x,y,r,g,b,a,u,v
    GLuint vao=0, vbo=0;
    size_t vertexCount=0;
    GLuint texture=0; // store currently bound texture
    void init(){
        glGenVertexArrays(1,&vao);
        glGenBuffers(1,&vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, 1<<20, nullptr, GL_DYNAMIC_DRAW);

        // position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)0);

        // color
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(2*sizeof(float)));

        // texcoords
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(6*sizeof(float)));

        glBindVertexArray(0);
    }
    void begin(){ data.clear(); vertexCount=0; }
    void pushVertex(float x,float y, float r,float g,float b,float a, float u=0.0f, float v=0.0f){
        data.push_back(x); data.push_back(y);
        data.push_back(r); data.push_back(g); data.push_back(b); data.push_back(a);
        data.push_back(u); data.push_back(v);
        vertexCount++;
    }

    void uploadAndDrawTriangles(GLenum mode){
        if(data.empty()) return;
        glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferSubData(GL_ARRAY_BUFFER,0,data.size()*sizeof(float),data.data());
        glBindVertexArray(vao);
        if(texture) glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(mode,0,(GLsizei)vertexCount);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D,0);
    }
    void destroy(){ if(vbo) glDeleteBuffers(1,&vbo); if(vao) glDeleteVertexArrays(1,&vao); }
};

////
GLuint loadTexture(const char* path){
    int w,h,n;
    unsigned char* data = stbi_load(path, &w, &h, &n, 4);
    if(!data){ std::cerr<<"Failed to load texture "<<path<<"\n"; return 0; }
    GLuint tex;
    glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);
    return tex;
}

// ---------------------- Geometry helpers ----------------------
static void addRectTriangles(DrawBuffer &buf, float x, float y, float w, float h, glm::vec4 color){
    buf.pushVertex(x, y, color.r, color.g, color.b, color.a);
    buf.pushVertex(x+w, y, color.r, color.g, color.b, color.a);
    buf.pushVertex(x+w, y+h, color.r, color.g, color.b, color.a);
    buf.pushVertex(x, y, color.r, color.g, color.b, color.a);
    buf.pushVertex(x+w, y+h, color.r, color.g, color.b, color.a);
    buf.pushVertex(x, y+h, color.r, color.g, color.b, color.a);
}
static void addRectLines(DrawBuffer &buf, float x, float y, float w, float h, glm::vec4 color){
    buf.pushVertex(x, y, color.r,color.g,color.b,color.a);
    buf.pushVertex(x+w, y, color.r,color.g,color.b,color.a);
    buf.pushVertex(x+w, y, color.r,color.g,color.b,color.a);
    buf.pushVertex(x+w, y+h, color.r,color.g,color.b,color.a);
    buf.pushVertex(x+w, y+h, color.r,color.g,color.b,color.a);
    buf.pushVertex(x, y+h, color.r,color.g,color.b,color.a);
    buf.pushVertex(x, y+h, color.r,color.g,color.b,color.a);
    buf.pushVertex(x, y, color.r,color.g,color.b,color.a);
}
static void addCircleTriangles(DrawBuffer &buf, float cx, float cy, float r, int segments, glm::vec4 color){
    for(int i=0;i<segments;i++){
        float a1 = (float)i / segments * 2.0f * M_PI;
        float a2 = (float)(i+1) / segments * 2.0f * M_PI;
        buf.pushVertex(cx, cy, color.r,color.g,color.b,color.a);
        buf.pushVertex(cx + cosf(a1)*r, cy + sinf(a1)*r, color.r,color.g,color.b,color.a);
        buf.pushVertex(cx + cosf(a2)*r, cy + sinf(a2)*r, color.r,color.g,color.b,color.a);
    }
}
 void addRectTextured(DrawBuffer &buf, float x, float y, float w, float h, glm::vec4 color){
    buf.pushVertex(x, y, color.r,color.g,color.b,color.a, 0.0f, 0.0f);
    buf.pushVertex(x+w, y, color.r,color.g,color.b,color.a, 1.0f, 0.0f);
    buf.pushVertex(x+w, y+h, color.r,color.g,color.b,color.a, 1.0f, 1.0f);

    buf.pushVertex(x, y, color.r,color.g,color.b,color.a, 0.0f, 0.0f);
    buf.pushVertex(x+w, y+h, color.r,color.g,color.b,color.a, 1.0f, 1.0f);
    buf.pushVertex(x, y+h, color.r,color.g,color.b,color.a, 0.0f, 1.0f);
}

// ---------------------- Floor plan structures ----------------------
struct RectItem { 
    float x,y,w,h; 
    glm::vec4 color; 
    std::string label;
    std::string type = "A"; // "A" or "B" window type
    GLuint texture = 0;
};

struct CircleItem { float x,y,r; glm::vec4 color; std::string label;GLuint texture = 0; };
struct DoorItem { float x,y,w,h; std::string hinge; std::string label;GLuint texture = 0;};
struct Vertex { glm::vec2 pos; glm::vec4 color; glm::vec2 uv; };
// ---------------------- Elevation Parameters ----------------------
static const float wallHeight   = 300.0f;  // cm or arbitrary units
static const float doorHeight   = 220.0f;
static const float windowHeight = 120.0f;
static const float windowSill   = 90.0f;

    GLuint floorTexture = 0;
    GLuint wallTexture = 0;
    GLuint kitchenTexture = 0;
    GLuint barTexture = 0;
    GLuint tableTexture = 0;
    GLuint doorTexture = 0; 

struct FloorPlan {
    bool showGrid = true;
    bool showLabels = true;
    bool showDoorSwings = true;
    bool showDimensions = true;
    bool showDrains=true;
    bool showScaleBar=true;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    bool showWindows = true;
    bool showDoors = true;
    bool showFrontElevation=false;
    bool showSideElevation=true;
    std::vector<RectItem> walls;
    std::vector<RectItem> kitchen;
    std::vector<RectItem> bar;
    std::vector<RectItem> windows;
    std::vector<RectItem> restrooms;
    std::vector<RectItem> fire;
    std::vector<RectItem> tablesRect;
    std::vector<CircleItem> tablesCircle;
    std::vector<DoorItem> doors;
    std::vector<RectItem> floor;
    std::vector<CircleItem>drains;
    DrawBuffer triBuf;
    DrawBuffer lineBuf;
    glm::mat4 proj;
    int canvasW=1200, canvasH=800;
   
    bool frontView = false;
    float doorHeight = 210.0f;
    float windowHeight = 120.0f;

 
   void init(int w,int h){
        canvasW=w; canvasH=h;
        triBuf.init(); lineBuf.init();
        setupDefaultLayout();
        updateProjection(w,h);
    }
    void setupDefaultLayout(){
        floor.clear(); walls.clear(); kitchen.clear(); bar.clear(); windows.clear(); restrooms.clear();
        fire.clear(); tablesRect.clear(); tablesCircle.clear(); doors.clear();drains.clear();

        floor.push_back({50, 50, 1100, 700, glm::vec4(0.172f, 0.243f, 0.314f, 1.0f), "Floor"});
        // --- Exterior wall ---
        walls.push_back({50,50,15,700, glm::vec4(0.172f,0.243f,0.314f,1.0f), "Exterior Wall"});
        walls.push_back({50,50,1100,15, glm::vec4(0.172f,0.243f,0.314f,1.0f), "Exterior Wall"});
        walls.push_back({1135,50,15,700, glm::vec4(0.172f,0.243f,0.314f,1.0f), "Exterior Wall"});
        walls.push_back({50,750,1100,15, glm::vec4(0.172f,0.243f,0.314f,1.0f), "Exterior Wall"});

     
   // --- Interior partition walls ---
        walls.push_back({400, 50, 5, 350, glm::vec4(0.365f,0.427f,0.494f,1.0f), "Interior  Wall"});
	walls.push_back({400, 470, 5, 295, glm::vec4(0.365f,0.427f,0.494f,1.0f), "Interior  Wall"});
        walls.push_back({850, 450, 3, 300, glm::vec4(0.365f,0.427f,0.494f,1.0f), "Interior Walll"});


        // --- Kitchen ---
        kitchen.push_back({100,100,200,80, glm::vec4(0.5f,0.55f,0.55f,1.0f), "Preperation Area"});
        kitchen.push_back({100,200,150,60, glm::vec4(0.906f,0.298f,0.196f,1.0f), "Cooking"});
        kitchen.push_back({100,280,120,50, glm::vec4(0.204f,0.596f,0.859f,1.0f), "Sink"});
        kitchen.push_back({280,100,80,40, glm::vec4(0.608f,0.353f,0.714f,1.0f), "Hand Wash"});
        kitchen.push_back({100,350,250,120, glm::vec4(0.204f,0.255f,0.369f,1.0f), "Storeroom"});

	drains.push_back({150, 150, 6.0f, glm::vec4(0.0f,0.5f,1.0f,1.0f), "Drain 1"});
	drains.push_back({200, 300, 6.0f, glm::vec4(0.0f,0.5f,1.0f,1.0f), "Drain 2"});
	drains.push_back({300, 350, 6.0f, glm::vec4(0.0f,0.5f,1.0f,1.0f), "Drain 3"});

        // --- Bar counter ---
        bar.push_back({935,100,25,250, glm::vec4(0.827f,0.329f,0.0f,1.0f), "Bar Counter"});
	for(int i=0;i<5;i++)
           tablesCircle.push_back({920.0f, 120.0f + i*50.0f, 6.0f, glm::vec4(0.902f,0.494f,0.133f,1.0f), ""});
        // --- Windows ---
        windows.push_back({200,50,150,10, glm::vec4(0.204f,0.596f,0.859f,1.0f), "Window","A"});
        windows.push_back({700,50,200,10, glm::vec4(0.204f,0.596f,0.859f,1.0f), "Window","A"});
        windows.push_back({50,200,10,100, glm::vec4(0.204f,0.596f,0.859f,1.0f), "Window",""});
        windows.push_back({500,755,200,10, glm::vec4(0.204f,0.596f,0.859f,1.0f), "Window","C"});
        windows.push_back({1110,70,40,60, glm::vec4(0.529f,0.808f,0.922f,1.0f), "Men's Toilet Window"});
        windows.push_back({1110,170,40,60, glm::vec4(0.529f,0.808f,0.922f,1.0f), "Women's Toilet Window"});

        // --- Restrooms ---
        restrooms.push_back({1030,50,120,100, glm::vec4(0.608f,0.357f,0.714f,1.0f), "Men's"});
        restrooms.push_back({1030,150,120,100, glm::vec4(0.608f,0.357f,0.714f,1.0f), "Women's"});

        // --- Doors ---
        doors.push_back({500,50,80,10, "Main Entrance"});
        doors.push_back({50,600,10,50, "Back Door"});
        doors.push_back({1135,350,10,80, "Emergency Exit"});
        doors.push_back({400,400,10,70, "Side Door"});
        // --- Fire Equipment ---
        fire.push_back({390,230,12,20, glm::vec4(0.906f,0.298f,0.196f,1.0f), "Fire Extinguisher"});
        fire.push_back({400,490,12,20, glm::vec4(0.906f,0.298f,0.196f,1.0f), "Fire Extinguisher"});
        fire.push_back({920,50,12,20, glm::vec4(0.906f,0.298f,0.196f,1.0f), ""});
        fire.push_back({1138,450,12,20, glm::vec4(0.906f,0.298f,0.196f,1.0f), "Fire Extinguisher"});

        // --- Rectangular Tables with Chairs ---
        const float tableSize = 50.0f;
        const float chairW = 12.0f, chairH = 20.0f;
        const float leftX = 520.0f;
        std::vector<float> leftY = {180.0f, 260.0f, 340.0f};
        const float rightX = 680.0f;
        std::vector<float> rightY = {180.0f, 260.0f, 340.0f};

        for(auto &y: leftY){
            tablesRect.push_back({leftX, y, tableSize, tableSize, glm::vec4(0.902f,0.494f,0.133f,1.0f), "Table"});
            tablesRect.push_back({leftX - chairW, y + (tableSize-chairH)/2, chairW, chairH, glm::vec4(0.10f,0.54f,0.22f,1.0f), ""});
            tablesRect.push_back({leftX + tableSize, y + (tableSize-chairH)/2, chairW, chairH, glm::vec4(0.10f,0.54f,0.22f,1.0f), ""});
        }

        for(auto &y: rightY){
            tablesRect.push_back({rightX, y, tableSize, tableSize, glm::vec4(0.902f,0.494f,0.133f,1.0f), "Table"});
            tablesRect.push_back({rightX - chairW, y + (tableSize-chairH)/2, chairW, chairH, glm::vec4(0.10f,0.54f,0.22f,1.0f), ""});
            tablesRect.push_back({rightX + tableSize, y + (tableSize-chairH)/2, chairW, chairH, glm::vec4(0.10f,0.54f,0.22f,1.0f), ""});
        }

        // --- Round Tables ---
        int numRound = 2;
        float baseX = 550.0f;
        float baseY = 600.0f;
        float tableSpacing = 170.0f;
        float tableRadius = 60.0f;
        float chairDist = tableRadius + 15.0f;
        for(int i=0;i<numRound;i++){
            float tx = baseX + i*tableSpacing;
            float ty = baseY;
            tablesCircle.push_back({tx, ty, tableRadius, glm::vec4(0.55f,0.35f,0.2f,1.0f), "Table"});

            for(int c=0;c<4;c++){
                float angle = (float)c/4.0f * 2.0f * M_PI;
                float cx = tx + cosf(angle)*chairDist;
                float cy = ty + sinf(angle)*chairDist;
                tablesRect.push_back({cx-chairW/2, cy-chairH/2, chairW, chairH, glm::vec4(0.10f,0.54f,0.22f,1.0f), "Chair"});
            }
        }

        // --- Sofa Area ---
        tablesRect.push_back({965, 710, 140, 40, glm::vec4(0.32f,0.20f,0.10f,1.0f), "Sofa"});
        tablesRect.push_back({970, 715, 60, 30, glm::vec4(0.45f,0.30f,0.18f,1.0f), ""});
        tablesRect.push_back({1040, 715, 60, 30, glm::vec4(0.45f,0.30f,0.18f,1.0f), ""});
        tablesRect.push_back({1100, 550, 40, 150, glm::vec4(0.32f,0.20f,0.10f,1.0f), "Sofa"});
        tablesRect.push_back({1105, 555, 30, 60, glm::vec4(0.45f,0.30f,0.18f,1.0f), ""});
        tablesRect.push_back({1105, 635, 30, 60, glm::vec4(0.45f,0.30f,0.18f,1.0f), ""});
        tablesRect.push_back({1000, 580, 80, 100, glm::vec4(0.6f,0.4f,0.2f,1.0f), "Coffee Table"});

        // TV
        walls.push_back({853, 550, 10, 100, glm::vec4(0.05f,0.05f,0.05f,1.0f), "TV"});
    }
    // ---------------------- Additional compliance features ----------------------
    void loadTextures() {
    floorTexture = loadTexture("floor.jpg");
    wallTexture  = loadTexture("wall.jpg");
    
    kitchenTexture = loadTexture("/home/floor.jpg");
    barTexture     = loadTexture("/home/textures/bar.jpg");
    tableTexture   = loadTexture("textures/table.jpg");
    doorTexture    = loadTexture("textures/door.jpg");
    }

    void drawDoorSwings() {
    if (!showDoorSwings) return;

    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    float sx = scaleX;
    float sy = scaleY;

    auto worldToScreen = [&](float wx, float wy, ImVec2 &out) -> bool {
        glm::vec4 clip = proj * glm::vec4(wx * sx, wy * sy, 0.0f, 1.0f);
        if (clip.w == 0.0f) return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f) return false;
        float sx_pix = (ndc.x * 0.5f + 0.5f) * (float)canvasW;
        float sy_pix = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)canvasH;
        out = ImVec2(sx_pix, sy_pix);
        return true;
    };

    for (const auto &d : doors) {
        ImVec2 center;
        if (!worldToScreen(d.x, d.y, center)) continue;

        float radius = (d.w + d.h) * 0.5f * sx; // scale radius relative to world units
        int segments = 20;
        float angleStart = 0.0f;
        float angleEnd = M_PI_2; // 90-degree swing

        // Draw the swing arc
        for (int i = 0; i < segments; i++) {
            float a1 = angleStart + (angleEnd - angleStart) * i / segments;
            float a2 = angleStart + (angleEnd - angleStart) * (i + 1) / segments;

            // Convert arc points to screen space relative to center
            ImVec2 p1(center.x + cosf(a1) * radius, center.y + sinf(a1) * radius);
            ImVec2 p2(center.x + cosf(a2) * radius, center.y + sinf(a2) * radius);

            draw_list->AddLine(center, p1, IM_COL32(255,128,0,200), 2.0f * sx);
            draw_list->AddLine(p1, p2, IM_COL32(255,128,0,200), 2.0f * sx);
        }
    }
}
void drawDimensions() {
    if(!showDimensions) return;

    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    float sx = scaleX, sy = scaleY;
    ImFont* font = ImGui::GetFont();
    float fontSize = 12.0f * sx; // scale font size with window

    auto worldToScreen = [&](float wx, float wy, ImVec2 &out) -> bool {
        glm::vec4 clip = proj * glm::vec4(wx * sx, wy * sy, 0.0f, 1.0f);
        if(clip.w == 0.0f) return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if(ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f) return false;
        out = ImVec2((ndc.x*0.5f + 0.5f) * canvasW, (1.0f - (ndc.y*0.5f + 0.5f)) * canvasH);
        return true;
    };

    auto drawLineWithArrow = [&](ImVec2 p0, ImVec2 p1){
        draw_list->AddLine(p0, p1, IM_COL32(0,0,0,255), 1.5f);
        float arrowSize = 5.0f * sx;
        draw_list->AddLine(p0, ImVec2(p0.x+arrowSize,p0.y+arrowSize), IM_COL32(0,0,0,255), 1.5f);
        draw_list->AddLine(p1, ImVec2(p1.x-arrowSize,p1.y-arrowSize), IM_COL32(0,0,0,255), 1.5f);
    };

    auto drawRectDims = [&](const std::vector<RectItem> &items){
        for(const auto &r: items){
            // Horizontal
            ImVec2 p0, p1;
            if(worldToScreen(r.x, r.y+r.h+5, p0) && worldToScreen(r.x+r.w, r.y+r.h+5, p1)){
                drawLineWithArrow(p0,p1);
                char buf[32]; snprintf(buf,32,"%.0f", r.w);
                ImVec2 textSize = ImGui::CalcTextSize(buf);
                ImVec2 textPos((p0.x+p1.x-textSize.x)*0.5f, p0.y - textSize.y*0.5f);
                draw_list->AddRectFilled(ImVec2(textPos.x-2,textPos.y-1), ImVec2(textPos.x+textSize.x+2,textPos.y+textSize.y+1), IM_COL32(255,255,255,200));
                draw_list->AddText(font,fontSize,textPos,IM_COL32(0,0,0,255),buf);
            }
            // Vertical
            if(worldToScreen(r.x+r.w+5, r.y, p0) && worldToScreen(r.x+r.w+5, r.y+r.h, p1)){
                drawLineWithArrow(p0,p1);
                char buf[32]; snprintf(buf,32,"%.0f", r.h);
                ImVec2 textSize = ImGui::CalcTextSize(buf);
                ImVec2 textPos(p0.x - textSize.x*0.5f, (p0.y+p1.y-textSize.y)*0.5f);
                draw_list->AddRectFilled(ImVec2(textPos.x-2,textPos.y-1), ImVec2(textPos.x+textSize.x+2,textPos.y+textSize.y+1), IM_COL32(255,255,255,200));
                draw_list->AddText(font,fontSize,textPos,IM_COL32(0,0,0,255),buf);
            }
        }
    };

    // Draw dimensions for all relevant rects
    drawRectDims(walls);
    //drawRectDims(kitchen);
    drawRectDims(bar);
    drawRectDims(windows);
    //drawRectDims(restrooms);
    //drawRectDims(fire);
    //drawRectDims(tablesRect);
}
void drawFloorDrains() {
    if (!showDrains) return;

    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    float sx = scaleX;

    auto worldToScreen = [&](float wx, float wy, ImVec2 &out) -> bool {
        glm::vec4 clip = proj * glm::vec4(wx * sx, wy * sx, 0.0f, 1.0f);
        if (clip.w == 0.0f) return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f) return false;
        out = ImVec2((ndc.x*0.5f+0.5f)*canvasW, (1.0f-(ndc.y*0.5f+0.5f))*canvasH);
        return true;
    };

    ImFont* font = ImGui::GetFont();
    float fontSize = 12.0f * sx;

    for (const auto &d : drains) {
        ImVec2 screenPos;
        if (!worldToScreen(d.x, d.y, screenPos)) continue;
        draw_list->AddCircleFilled(screenPos, d.r*sx, IM_COL32((int)(d.color.r*255),(int)(d.color.g*255),(int)(d.color.b*255),255), 12);

        // Draw label
        if (!d.label.empty()) {
            ImVec2 textSize = ImGui::CalcTextSize(d.label.c_str());
            ImVec2 textPos(screenPos.x - textSize.x*0.5f, screenPos.y - d.r*sx - textSize.y - 2);
            draw_list->AddRectFilled(ImVec2(textPos.x-2, textPos.y-1), ImVec2(textPos.x+textSize.x+2, textPos.y+textSize.y+1), IM_COL32(255,255,255,200));
            draw_list->AddText(font, fontSize, textPos, IM_COL32(0,0,0,255), d.label.c_str());
        }
    }
}
    void drawScaleBar() {
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    float sx = scaleX;
    float sy = scaleY;

    // Example: scale bar starts at world coordinates (60, 40)
    float wx0 = 60.0f;
    float wy0 = 40.0f;
    float length_m = 100.0f; // 1 meter in world units

    auto worldToScreen = [&](float wx, float wy, ImVec2 &out) -> bool {
        glm::vec4 clip = proj * glm::vec4(wx * sx, wy * sy, 0.0f, 1.0f);
        if (clip.w == 0.0f) return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f) return false;
        float sx_pix = (ndc.x * 0.5f + 0.5f) * (float)canvasW;
        float sy_pix = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)canvasH;
        out = ImVec2(sx_pix, sy_pix);
        return true;
    };

    ImVec2 start, end;
    if (!worldToScreen(wx0, wy0, start)) return;
    if (!worldToScreen(wx0 + length_m, wy0, end)) return;

    draw_list->AddLine(start, end, IM_COL32(0,0,0,255), 2.0f * sx);
    draw_list->AddText(ImVec2(start.x, start.y - 20.0f*sx), IM_COL32(0,0,0,255), "1 m");
    }

    void drawLabels() {
    if(!showLabels) return;

    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    float sx = scaleX;
    float sy = scaleY;
    ImFont* font = ImGui::GetFont();
    float fontSize = 14.0f * sx;

    auto worldToScreen = [&](float wx, float wy, ImVec2 &out) -> bool {
        glm::vec4 clip = proj * glm::vec4(wx * sx, wy * sy, 0.0f, 1.0f);
        if (clip.w == 0.0f) return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f) return false;
        float sx_pix = (ndc.x * 0.5f + 0.5f) * (float)canvasW;
        float sy_pix = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)canvasH;
        out = ImVec2(sx_pix, sy_pix);
        return true;
    };
    auto drawRectLabels = [&](auto const &items){
    for (const auto &r : items) {
        if (r.label.empty()) continue;
        float cx = r.x + r.w*0.5f;
        float cy = r.y + r.h*0.5f;
        ImVec2 screenPos;
        if(!worldToScreen(cx, cy, screenPos)) continue;
        ImVec2 textSize = ImGui::CalcTextSize(r.label.c_str());
        ImVec2 textPos(screenPos.x - textSize.x*0.5f, screenPos.y - textSize.y*0.5f);
        ImU32 bg = IM_COL32(255,255,255,200);
        draw_list->AddRectFilled(ImVec2(textPos.x-4,textPos.y-2),
                                 ImVec2(textPos.x+textSize.x+4,textPos.y+textSize.y+2),
                                 bg);
        draw_list->AddText(font, fontSize, textPos, IM_COL32(0,0,0,255), r.label.c_str());
    }
   };
    auto drawCircleLabels = [&](const std::vector<CircleItem> &items){
        for(const auto &c: items){
            if(c.label.empty()) continue;
            ImVec2 screenPos;
            if(!worldToScreen(c.x, c.y, screenPos)) continue;
            ImVec2 textSize = ImGui::CalcTextSize(c.label.c_str());
            ImVec2 textPos(screenPos.x - textSize.x * 0.5f, screenPos.y - textSize.y * 0.5f);
            ImU32 bg = IM_COL32(255,255,255,200);
            draw_list->AddRectFilled(ImVec2(textPos.x - 4, textPos.y - 2),
                                     ImVec2(textPos.x + textSize.x + 4, textPos.y + textSize.y + 2),
                                     bg);
            draw_list->AddText(font, fontSize, textPos, IM_COL32(0,0,0,255), c.label.c_str());
        }
    };

    drawRectLabels(walls);
    drawRectLabels(kitchen);
    drawRectLabels(bar);
    drawRectLabels(windows);
    drawRectLabels(restrooms);
    drawRectLabels(fire);
    drawRectLabels(tablesRect);
    drawCircleLabels(tablesCircle);
    drawRectLabels(doors);
    drawCircleLabels(drains);
   }

void drawDoorSwingElevation(float x, float y, float w, float h, bool isFront=true) {
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    float sx = scaleX, sy = scaleY;
    float groundY = 600.0f;

    ImVec2 center(x*sx, groundY - h*sy);
    float radius = w * 0.5f * sx;
    int segments = 20;

    for(int i=0; i<=segments; i++) {
        float a0 = (M_PI/2.0f) * i/segments;
        float a1 = (M_PI/2.0f) * (i+1)/segments;
        ImVec2 p0 = ImVec2(center.x + radius*cos(a0), center.y + radius*sin(a0));
        ImVec2 p1 = ImVec2(center.x + radius*cos(a1), center.y + radius*sin(a1));
        draw_list->AddLine(p0,p1, IM_COL32(0,0,0,255), 1.0f);
    }
}

void drawFrontElevationView() {
    if (!showFrontElevation) return;

    ImGui::Begin("Front Elevation");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float scale = 0.5f;  // elevation scale
    float groundY = origin.y + 300.0f;  // baseline for walls

    // Draw baseline
    dl->AddLine(ImVec2(origin.x, groundY), ImVec2(origin.x+1200*scale, groundY),
                IM_COL32(0,0,0,255), 2.0f);

    // Draw walls as vertical extrusions
    for (auto &w : walls) {
        float x0 = origin.x + w.x * scale;
        float x1 = origin.x + (w.x + w.w) * scale;
        float y0 = groundY;
        float y1 = groundY - wallHeight * scale;
        dl->AddRectFilled(ImVec2(x0, y1), ImVec2(x1, y0),
            IM_COL32((int)(w.color.r*255),(int)(w.color.g*255),(int)(w.color.b*255),255));
        dl->AddRect(ImVec2(x0, y1), ImVec2(x1, y0), IM_COL32(0,0,0,255));
    }

    // Draw windows
    for (auto &win : windows) {
        float x0 = origin.x + win.x * scale;
        float x1 = origin.x + (win.x + win.w) * scale;
        float y0 = groundY - windowSill * scale;
        float y1 = y0 - windowHeight * scale;
        dl->AddRectFilled(ImVec2(x0, y1), ImVec2(x1, y0), IM_COL32(120,180,255,255));
        dl->AddRect(ImVec2(x0, y1), ImVec2(x1, y0), IM_COL32(0,0,0,255));
    }

    // Draw doors
    for (auto &d : doors) {
        float x0 = origin.x + d.x * scale;
        float x1 = origin.x + (d.x + d.w) * scale;
        float y0 = groundY;
        float y1 = y0 - doorHeight * scale;
        dl->AddRectFilled(ImVec2(x0, y1), ImVec2(x1, y0), IM_COL32(180,100,50,255));
        dl->AddRect(ImVec2(x0, y1), ImVec2(x1, y0), IM_COL32(0,0,0,255));
    }

    ImGui::End();
}

void drawSideElevationView() {
    if (!showSideElevation) return;

    ImGui::Begin("Side Elevation");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float scale = 0.5f;
    float groundY = origin.y + 300.0f;

    dl->AddLine(ImVec2(origin.x, groundY), ImVec2(origin.x+700*scale, groundY),
                IM_COL32(0,0,0,255), 2.0f);

    for (auto &w : walls) {
        float depth = w.h; // projecting width as depth
        float x0 = origin.x;
        float x1 = origin.x + depth * scale;
        float y0 = groundY;
        float y1 = groundY - wallHeight * scale;
        dl->AddRectFilled(ImVec2(x0, y1), ImVec2(x1, y0),
            IM_COL32((int)(w.color.r*255),(int)(w.color.g*255),(int)(w.color.b*255),255));
        dl->AddRect(ImVec2(x0, y1), ImVec2(x1, y0), IM_COL32(0,0,0,255));
    }

    ImGui::End();
}
void updateProjection(int w,int h){
        canvasW = w;
        canvasH = h;
        glViewport(0, 0, w, h);

        scaleX = (float)w / 1200.0f;
        scaleY = (float)h / 800.0f;
        float s = (scaleX < scaleY) ? scaleX : scaleY;
        scaleX = scaleY = s;

        float viewW = 1200.0f * scaleX;
        float viewH = 800.0f * scaleY;
        proj = glm::ortho(0.0f, viewW, viewH, 0.0f, -1.0f, 1.0f);
    }

void render(GLuint shader) {
    float sx = scaleX, sy = scaleY;

    glUseProgram(shader);
    glUniformMatrix4fv(glGetUniformLocation(shader,"uMVP"),1,GL_FALSE, glm::value_ptr(proj));

    // ------------------ TRIANGLES ------------------
    triBuf.begin();

    // --- Floor with texture ---
    glUniform1i(glGetUniformLocation(shader,"useTexture"), true);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, floorTexture);
    glUniform1i(glGetUniformLocation(shader,"uTexture"), 0);
    addRectTextured(triBuf, floor[0].x*sx, floor[0].y*sy, floor[0].w*sx, floor[0].h*sy, glm::vec4(1.0f));

    // --- Walls with texture ---
    glBindTexture(GL_TEXTURE_2D, wallTexture);
    for(auto &w: walls)
        addRectTextured(triBuf, w.x*sx, w.y*sy, w.w*sx, w.h*sy, glm::vec4(1.0f));

    // --- Colored objects ---
    glUniform1i(glGetUniformLocation(shader,"useTexture"), false);

    for(auto &k: kitchen) addRectTriangles(triBuf, k.x*sx, k.y*sy, k.w*sx, k.h*sy, k.color);
    for(auto &b: bar) addRectTriangles(triBuf, b.x*sx, b.y*sy, b.w*sx, b.h*sy, b.color);
    for(auto &win: windows) addRectTriangles(triBuf, win.x*sx, win.y*sy, win.w*sx, win.h*sy, win.color);
    for(auto &r: restrooms) addRectTriangles(triBuf, r.x*sx, r.y*sy, r.w*sx, r.h*sy, r.color);
    for(auto &f: fire) addRectTriangles(triBuf, f.x*sx, f.y*sy, f.w*sx, f.h*sy, f.color);

    glm::vec4 doorColor(0.545f,0.271f,0.075f,1.0f);
    for(auto &d: doors) addRectTriangles(triBuf, d.x*sx, d.y*sy, d.w*sx, d.h*sy, doorColor);

    for(auto &t: tablesRect) addRectTriangles(triBuf, t.x*sx, t.y*sy, t.w*sx, t.h*sy, t.color);
    for(auto &c: tablesCircle) addCircleTriangles(triBuf, c.x*sx, c.y*sy, c.r*sx, 20, c.color);
   
    triBuf.uploadAndDrawTriangles(GL_TRIANGLES);

    // ------------------ LINES ------------------
    lineBuf.begin();
   
    // Grid
    if(showGrid){
        glm::vec4 gcol(0.0f,0.0f,0.0f,0.06f);
        int step = 50;
        for(int x=50;x<=1150;x+=step) lineBuf.pushVertex(x*sx,50*sy,gcol.r,gcol.g,gcol.b,gcol.a), lineBuf.pushVertex(x*sx,750*sy,gcol.r,gcol.g,gcol.b,gcol.a);
        for(int y=50;y<=750;y+=step) lineBuf.pushVertex(50*sx,y*sy,gcol.r,gcol.g,gcol.b,gcol.a), lineBuf.pushVertex(1150*sx,y*sy,gcol.r,gcol.g,gcol.b,gcol.a);
    }

    glm::vec4 outlineColor(0.2f,0.24f,0.28f,1.0f);
    for(auto &w: walls) addRectLines(lineBuf, w.x*sx, w.y*sy, w.w*sx, w.h*sy, outlineColor);
    for(auto &k: kitchen) addRectLines(lineBuf, k.x*sx, k.y*sy, k.w*sx, k.h*sy, glm::vec4(0.12f,0.12f,0.12f,1.0f));
    for(auto &t: tablesRect) addRectLines(lineBuf, t.x*sx, t.y*sy, t.w*sx, t.h*sy, glm::vec4(0.62f,0.36f,0.12f,1.0f));

    lineBuf.uploadAndDrawTriangles(GL_LINES);
}


    void destroy(){ triBuf.destroy(); lineBuf.destroy(); }
};


// ---------------------- GLFW + Main ----------------------
static FloorPlan *gPlan = nullptr;
static GLuint gProgram = 0;
static int gWinW=1280,gWinH=800;
static void framebuffer_size_cb(GLFWwindow*, int w,int h){
    if(w>0 && h>0){ gWinW=w; gWinH=h; if(gPlan) gPlan->updateProjection(w,h); }
}
int main() {
    if(!glfwInit()){ fprintf(stderr,"glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(gWinW,gWinH,"Restaurant Floor Plan (2D Modern OpenGL)",nullptr,nullptr);
    if(!window){ fprintf(stderr,"Window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_cb);

    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)){ fprintf(stderr,"gladLoadGLLoader failed\n"); return 1; }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    gProgram = createProgram(VERT_SRC, FRAG_SRC);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io=ImGui::GetIO(); (void)io;
    ImGui::StyleColorsLight();
    ImGui_ImplGlfw_InitForOpenGL(window,true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    FloorPlan plan;
    plan.init(1200,800);
    gPlan = &plan;

    // --- Main loop ---
    while(!glfwWindowShouldClose(window)){
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Controls");
        // Inside your main loop, after "Controls" window:
	
    	if (plan.showFrontElevation) {
    	plan.drawFrontElevationView();
	}
	if (plan.showSideElevation) {
	    plan.drawSideElevationView();
	}
	if(ImGui::Button("Reset Layout")) plan.setupDefaultLayout();
        ImGui::Checkbox("Show Grid",&plan.showGrid);
        ImGui::Checkbox("Show Labels",&plan.showLabels);
        ImGui::Checkbox("Show Door Swings",&plan.showDoorSwings);
        ImGui::Checkbox("Show Dimensions",&plan.showDimensions);
       	ImGui::Checkbox("Show Front Elevation", &plan.showFrontElevation);
		 //ImGui::Begin("Front Elevation Controls");
	//ImGui::Checkbox("Show Windows", &plan.showWindows);
	
	ImGui::End();
	glClearColor(0.925f,0.941f,0.945f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        plan.render(gProgram);
        //plan.drawLabels();  // <-- fixed capitalization and added semicolon
        plan.drawDoorSwings();
        plan.drawFloorDrains();
        plan.drawDimensions();  // dynamic & scaled
        plan.drawScaleBar();
        plan.drawLabels();
//	plan.drawFrontElevation();
//	plan.drawFrontElevationWindow(); 
        ImGui::Render();
//        drawFrontElevation(plan);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    plan.destroy();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glDeleteProgram(gProgram);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
