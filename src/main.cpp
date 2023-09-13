// clang-format off
#include <glad/glad.h>
// clang-format on

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
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
    out vec4 fragColor;
    
    uniform vec2 windowSize;
    uniform int pointCount;
    uniform samplerBuffer pointsTexture; // TBO for point data

    void main() {
        fragColor = vec4(0.0); // Initialize with no color

        vec2 coord = gl_FragCoord.xy / windowSize;

        fragColor.xy = coord;

        // Iterate through the points
        for (int i = 0; i < pointCount; ++i) {
            vec2 point = texelFetch(pointsTexture, i).xy;
            float distance = length(coord - point);
            if (distance <= 0.1) { // Adjust this threshold as needed
                fragColor = vec4(1.0, 0.0, 0.0, 1.0);
                break;
            }
        }
    }
)";

// Function to find the index of the nearest point to a given position
int FindNearestPoint(const glm::vec2& position,
                     const std::vector<glm::vec2>& points) {
    float minDistance = FLT_MAX;
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

void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    // Update the viewport to match the new window size
    glViewport(0, 0, width, height);
    printf("window size changed to %d %d\n", width, height);
    (void)window;
}

int main() {
    // Initialize GLFW and create a window
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // Configure GLFW
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    // Create a windowed mode window and its OpenGL context
    GLFWwindow* window = glfwCreateWindow(800, 600, "ecurves", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    // vsync
    glfwSwapInterval(1);
    // Set up the window resize callback
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    // Make the window's context current
    glfwMakeContextCurrent(window);

    // Initialize GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
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

    // Set up vertex data for two triangles to cover the viewport
    float vertices[] = {
        -1.0f, 1.0f,   //
        -1.0f, -1.0f,  //
        1.0f,  1.0f,   //
        1.0f,  -1.0f,
    };

    // Generate a vertex buffer object (VBO) and vertex array object (VAO)
    GLuint VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    // Bind the VAO first, then bind and set vertex buffer(s), and then
    // configure vertex attributes(s)
    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float),
                          (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Create a Texture Buffer Object (TBO) for points
    std::vector<glm::vec2> pointList;  // List to store points
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

    // Setup ImGui context and binding to GLFW and OpenGL3
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    // Set ImGui's DPI scaling callback to glfwGetWindowContentScale
    glfwSetWindowContentScaleCallback(
        window, [](GLFWwindow*, float xscale, float yscale) {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplayFramebufferScale = ImVec2(xscale, yscale);
        });

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Main loop
    int nearestIndex = -1;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start the ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Create a Dear ImGui window for the 2D viewport
        ImGui::Begin("ecurves");

        // Radio buttons to switch between placing and moving points
        static int isPlacingPoints = 1;
        ImGui::RadioButton("Place Points", &isPlacingPoints, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Move Points", &isPlacingPoints, 0);

        // Check for mouse clicks and add/move points based on the mode
        if (isPlacingPoints == 1) {
            // Placing points mode (add points)
            if (ImGui::IsMouseClicked(0)) {
                ImVec2 mousePos = ImGui::GetMousePos();
                glm::vec2 normalizedMousePos = glm::vec2(
                    mousePos.x / 800.0f, 1.0f - (mousePos.y / 600.0f));

                printf("adding point at %f %f\n", normalizedMousePos.x,
                       normalizedMousePos.y);

                // Add the point to the list
                pointList.push_back(normalizedMousePos);

                // Update the TBO with new data
                glBindBuffer(GL_TEXTURE_BUFFER, tbo);
                glBufferData(GL_TEXTURE_BUFFER,
                             pointList.size() * sizeof(glm::vec2),
                             pointList.data(), GL_DYNAMIC_DRAW);
            }
        } else {
            // Moving points mode
            if (ImGui::IsMouseClicked(0)) {
                ImVec2 mousePos = ImGui::GetMousePos();
                glm::vec2 normalizedMousePos = glm::vec2(
                    mousePos.x / 800.0f, 1.0f - (mousePos.y / 600.0f));

                // Find the nearest point and set it as the moving point
                nearestIndex = FindNearestPoint(normalizedMousePos, pointList);
            } else if (ImGui::IsMouseDragging(0) && nearestIndex != -1) {
                // Move the selected point
                ImVec2 mousePos = ImGui::GetMousePos();
                glm::vec2 normalizedMousePos = glm::vec2(
                    mousePos.x / 800.0f, 1.0f - (mousePos.y / 600.0f));
                pointList[nearestIndex] = normalizedMousePos;

                // Update the TBO with new data
                glBindBuffer(GL_TEXTURE_BUFFER, tbo);
                glBufferData(GL_TEXTURE_BUFFER,
                             pointList.size() * sizeof(glm::vec2),
                             pointList.data(), GL_DYNAMIC_DRAW);
            } else {
                // Reset the moving point
                nearestIndex = -1;
            }
        }

        // End the ImGui window
        ImGui::End();

        // Clear the screen
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Use the shader program
        glUseProgram(shaderProgram);

        // Set the windowSize uniform in the fragment shader
        GLint windowSizeLocation =
            glGetUniformLocation(shaderProgram, "windowSize");
        glUniform2f(windowSizeLocation, 800.0f, 600.0f);  // Window size

        // Bind the TBO to the texture unit and set it as a uniform
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_BUFFER, pointsTexture);
        glUniform1i(glGetUniformLocation(shaderProgram, "pointsTexture"), 0);
        glUniform1i(glGetUniformLocation(shaderProgram, "pointCount"),
                    static_cast<GLint>(pointList.size()));

        // Draw a full-screen quad
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Render ImGui
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Swap front and back buffers
        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glDeleteBuffers(1, &tbo);
    glDeleteTextures(1, &pointsTexture);
    glDeleteBuffers(1, &VBO);
    glDeleteVertexArrays(1, &VAO);
    glDeleteProgram(shaderProgram);
    glfwTerminate();

    return 0;
}
