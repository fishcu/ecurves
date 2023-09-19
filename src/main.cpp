// clang-format off
#include <glad/glad.h>
// clang-format on

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <numeric>
#include <vector>

// Shader source code
const char* vertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    
    void main() {
        gl_Position = vec4(aPos, 0.0, 1.0);
    }
)";

const char* fragmentShaderSource = R"(
    #version 330 core
    layout(origin_upper_left) in vec4 gl_FragCoord;
    out vec4 fragColor;
    
    uniform vec2 mousePos;
    uniform vec2 windowSize;

    uniform int nearestIndex;
    uniform int pointCount;
    uniform samplerBuffer pointsTexture; // TBO for point data

    void main() {
        fragColor = vec4(0.0);
        
        for (int i = 0; i < pointCount -1 ; ++i) {
            if (gl_FragCoord.x > texelFetch(pointsTexture, i).x && gl_FragCoord.x < texelFetch(pointsTexture, i + 1).x) {
                vec2 line = texelFetch(pointsTexture, i + 1).xy - texelFetch(pointsTexture, i).xy;
                vec2 toPoint = gl_FragCoord.xy - texelFetch(pointsTexture, i).xy;
                if (line.x * toPoint.y - toPoint.x * line.y > 0) {
                    fragColor = vec4(1.0);
                }
            }
        }
        for (int i = 0; i < pointCount; ++i) {
            vec2 point = texelFetch(pointsTexture, i).xy;
            float distance = length(gl_FragCoord.xy - point);
            if (distance <= (i == nearestIndex ? 8.0 : 5.0)) {
                fragColor = vec4(i == nearestIndex ?  vec3(1.0, 0.5, 0.5) : vec3(1.0, 0.0, 0.0), 1.0);
            }            
        }
    }
)";

// Function to find the index of the nearest point to a given position
int FindNearestPoint(const glm::vec2& position,
                     const std::vector<glm::vec2>& points,
                     float threshold = 50.0f) {
    float minDistance = threshold;
    int nearestIndex = -1;

    for (size_t i = 0; i < points.size(); ++i) {
        float distance = glm::distance(position, points[i]);
        if (distance < minDistance) {
            minDistance = distance;
            nearestIndex = static_cast<int>(i);
        }
    }

    return nearestIndex;
}

struct App {
    GLFWwindow* window;
    int width = 1600, height = 900;
    float dpi_scale = 2.0;

    // Used for drawing full-screen quad
    GLuint VBO, VAO;

    int init() {
        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW" << std::endl;
            return -1;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

        window = glfwCreateWindow(width, height, "ecurves", NULL, NULL);
        if (!window) {
            std::cerr << "Failed to create GLFW window" << std::endl;
            glfwTerminate();
            return -1;
        }

        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, &App::FramebufferSizeCallback);

        // Vsync
        glfwSwapInterval(1);

        glfwMakeContextCurrent(window);

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Failed to initialize GLAD" << std::endl;
            return -1;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 330");
        io.FontGlobalScale = dpi_scale;

        // Set up vertex data for two triangles to cover the viewport
        float vertices[] = {
            -1.0f, 1.0f,   //
            -1.0f, -1.0f,  //
            1.0f,  1.0f,   //
            1.0f,  -1.0f,
        };

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        // Bind the VAO first, then bind and set vertex buffer(s), and then
        // configure vertex attributes(s)
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
                     GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float),
                              (void*)0);
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        return 0;
    }

    void draw() {
        // Draw a full-screen quad
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Render ImGui
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    void cleanup() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glDeleteBuffers(1, &VBO);
        glDeleteVertexArrays(1, &VAO);
        glfwTerminate();
    }

    void framebufferSizeCallback(int new_width, int new_height) {
        glViewport(0, 0, new_width, new_height);

        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);

        dpi_scale =
            2.0f * static_cast<float>(fb_w) / static_cast<float>(new_width);

        ImGui::GetIO().FontGlobalScale = dpi_scale;

        width = new_width;
        height = new_height;
        printf("new window size: %d %d. New DPI: %f\n", width, height,
               dpi_scale);
    }
    static void FramebufferSizeCallback(GLFWwindow* window, int width,
                                        int height) {
        App* app = static_cast<App*>(glfwGetWindowUserPointer(window));
        app->framebufferSizeCallback(width, height);
    }
};

int main() {
    App app;
    if (app.init() != 0) {
        std::cerr << "Error initializing app!" << std::endl;
        return -1;
    }

    // Create and compile the vertex shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    // Check for vertex shader compilation errors
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        std::cerr << "Vertex shader compilation failed:\n"
                  << infoLog << std::endl;
    }

    // Create and compile the fragment shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    // Check for fragment shader compilation errors
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        std::cerr << "Fragment shader compilation failed:\n"
                  << infoLog << std::endl;
    }

    // Create and link the shader program
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // Check for shader program linking errors
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
        std::cerr << "Shader program linking failed:\n" << infoLog << std::endl;
    }

    // Delete the shader objects as they are linked into the program and no
    // longer needed
    glDeleteShader(fragmentShader);
    glDeleteShader(vertexShader);

    // Create a Texture Buffer Object (TBO) for points
    std::vector<glm::vec2> pointList;
    GLuint tbo;
    glGenBuffers(1, &tbo);
    glBindBuffer(GL_TEXTURE_BUFFER, tbo);
    glBufferData(GL_TEXTURE_BUFFER, pointList.size() * sizeof(glm::vec2),
                 pointList.data(), GL_DYNAMIC_DRAW);

    // Create a texture from the TBO
    GLuint pointsTexture;
    glGenTextures(1, &pointsTexture);
    glBindTexture(GL_TEXTURE_BUFFER, pointsTexture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, tbo);

    int nearestIndex = -1;
    int nearestIdxWhenClicked = -1;
    while (!glfwWindowShouldClose(app.window) &&
           !glfwGetKey(app.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("ecurves");
        // Initial window pos
        ImGui::SetWindowPos(ImVec2(100, 100), ImGuiCond_Once);

        static int isPlacingPoints = 1;
        ImGui::RadioButton("Place Points", &isPlacingPoints, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Move Points", &isPlacingPoints, 0);

        // Point annotations
        for (size_t i = 0; i < pointList.size(); ++i) {
            const glm::vec2& point = pointList[i];
            // Make the ImGui window transparent
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(
                ImGuiCol_WindowBg,
                ImVec4(0, 0, 0, 0));  // Transparent background
            char idxString[64];
            snprintf(idxString, sizeof(idxString), "%zu", i);
            // Calculate the size of the text
            ImVec2 textSize = ImGui::CalcTextSize(idxString);
            ImVec2 textPos(point.x - textSize.x * 0.5 * app.dpi_scale,
                           point.y - textSize.y * app.dpi_scale - 5.0f);
            ImGui::SetNextWindowPos(textPos);
            ImGui::Begin(
                idxString, nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoInputs |
                    ImGuiWindowFlags_NoSavedSettings);
            ImGui::Text("%zu", i);
            ImGui::End();
            ImGui::PopStyleVar(2);  // Pop the style changes
            ImGui::PopStyleColor();
        }

        ImGui::End();
        ImGui::Render();

        // Only do mouse events if Imgui doesn't capture them
        if (!ImGui::GetIO().WantCaptureMouse) {
            if (isPlacingPoints == 1) {
                if (ImGui::IsMouseClicked(0)) {
                    ImVec2 mousePos = ImGui::GetMousePos();
                    printf("adding point at %f %f\n", mousePos.x, mousePos.y);

                    pointList.push_back(glm::vec2(mousePos.x, mousePos.y));
                    std::stable_sort(
                        pointList.begin(), pointList.end(),
                        [](const glm::vec2& a, const glm::vec2& b) {
                            return a.x < b.x;
                        });

                    glBindBuffer(GL_TEXTURE_BUFFER, tbo);
                    glBufferData(GL_TEXTURE_BUFFER,
                                 pointList.size() * sizeof(glm::vec2),
                                 pointList.data(), GL_DYNAMIC_DRAW);
                }
            } else {
                ImVec2 mousePos = ImGui::GetMousePos();
                nearestIndex = FindNearestPoint(
                    glm::vec2(mousePos.x, mousePos.y), pointList);
                if (ImGui::IsMouseClicked(0)) {
                    nearestIdxWhenClicked = nearestIndex;
                }
                if (ImGui::IsMouseDragging(0, 0.0f) &&
                    nearestIdxWhenClicked != -1) {
                    std::vector<glm::vec2> pointListNew = pointList;
                    pointListNew[nearestIdxWhenClicked] =
                        glm::vec2(mousePos.x, mousePos.y);

                    const auto nearestPoint =
                        (nearestIndex != -1 ? pointListNew[nearestIndex]
                                            : glm::vec2{-1.0f, -1.0f});
                    const auto nearestPointWhenClicked =
                        pointListNew[nearestIdxWhenClicked];

                    std::stable_sort(
                        pointListNew.begin(), pointListNew.end(),
                        [](const glm::vec2& a, const glm::vec2& b) {
                            return a.x < b.x;
                        });

                    for (int i = 0; i < pointList.size(); ++i) {
                        if (nearestPoint == pointListNew[i]) {
                            nearestIndex = i;
                        }
                        if (nearestPointWhenClicked == pointListNew[i]) {
                            nearestIdxWhenClicked = i;
                        }
                        pointList[i] = pointListNew[i];
                    }

                    glBindBuffer(GL_TEXTURE_BUFFER, tbo);
                    glBufferData(GL_TEXTURE_BUFFER,
                                 pointList.size() * sizeof(glm::vec2),
                                 pointList.data(), GL_DYNAMIC_DRAW);
                } else {
                    nearestIdxWhenClicked = -1;
                }
            }
        }

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);

        // Set the mousePos uniform in the fragment shader
        ImVec2 mousePos = ImGui::GetMousePos();
        GLint mousePosLocation =
            glGetUniformLocation(shaderProgram, "mousePos");
        glUniform2f(mousePosLocation, static_cast<GLfloat>(mousePos.x),
                    static_cast<GLfloat>(mousePos.y));

        // Set the windowSize uniform in the fragment shader
        GLint windowSizeLocation =
            glGetUniformLocation(shaderProgram, "windowSize");
        glUniform2f(windowSizeLocation, static_cast<GLfloat>(app.width),
                    static_cast<GLfloat>(app.height));

        // Set the nearestIndex uniform in the fragment shader
        GLint nearestIndexLocation =
            glGetUniformLocation(shaderProgram, "nearestIndex");
        glUniform1i(nearestIndexLocation, static_cast<GLint>(nearestIndex));

        // Bind the TBO to the texture unit and set it as a uniform
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_BUFFER, pointsTexture);
        glUniform1i(glGetUniformLocation(shaderProgram, "pointsTexture"), 0);
        glUniform1i(glGetUniformLocation(shaderProgram, "pointCount"),
                    static_cast<GLint>(pointList.size()));

        app.draw();
    }

    glDeleteBuffers(1, &tbo);
    glDeleteTextures(1, &pointsTexture);
    glDeleteProgram(shaderProgram);
    app.cleanup();

    return 0;
}
