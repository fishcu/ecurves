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
    uniform samplerBuffer pointsTexture;  // TBO for point data

    float DigitBin(const in int x) {
        return x == 0   ? 480599.0
            : x == 1 ? 139810.0
            : x == 2 ? 476951.0
            : x == 3 ? 476999.0
            : x == 4 ? 350020.0
            : x == 5 ? 464711.0
            : x == 6 ? 464727.0
            : x == 7 ? 476228.0
            : x == 8 ? 481111.0
            : x == 9 ? 481095.0
                        : 0.0;
    }

    float PrintValue(vec2 fragCoord, vec2 pixelCoord, vec2 fontSize, float value,
                    float digits, float decimals) {
        vec2 charCoord = (fragCoord - pixelCoord) / fontSize;
        if (charCoord.y < 0.0 || charCoord.y >= 1.0) return 0.0;
        float bits = 0.0;
        float digitIndex1 = digits - floor(charCoord.x) + 1.0;
        if (-digitIndex1 <= decimals) {
            float pow1 = pow(10.0, digitIndex1);
            float absValue = abs(value);
            float pivot = max(absValue, 1.5) * 10.0;
            if (pivot < pow1) {
                if (value < 0.0 && pivot >= pow1 * 0.1) bits = 1792.0;
            } else if (digitIndex1 == 0.0) {
                if (decimals > 0.0) bits = 2.0;
            } else {
                value = digitIndex1 < 0.0 ? fract(absValue) : absValue * 10.0;
                bits = DigitBin(int(mod(value / pow1, 10.0)));
            }
        }
        return floor(mod(bits / pow(2.0, floor(fract(charCoord.x) * 4.0) +
                                            floor(charCoord.y * 5.0) * 4.0),
                        2.0));
    }

    float y_eval(vec2 p0, vec2 delta, float x_t) {
        return delta.y * (x_t - p0.x) / delta.x + p0.y;
    }

    float x_eval(vec2 p0, vec2 delta, float y_t) {
        return delta.x * (y_t - p0.y) / delta.y + p0.x;
    }

    float line_square_overlap(vec2 p0, vec2 p1, vec4 sq) {
        vec2 delta = p1 - p0;

        if (delta.x < 1.0e-8) {
            return 0.0;
        }

        float x_start = clamp(p0.x, sq.x, sq.z);
        float x_end = clamp(p1.x, sq.x, sq.z);
        if (abs(delta.y) < 1.0e-8) {
            float y = clamp(p0.y, sq.y, sq.w);
            return (x_end - x_start) * (sq.w - y);
        } else if (delta.y > 0.0) {
            // where line hits upper border of square
            float x_intersect_start =
                clamp(x_eval(p0, delta, sq.y), x_start, x_end);
            float y_at_x_intersect_start =
                clamp(y_eval(p0, delta, x_intersect_start), sq.y, sq.w);
            // where line hits lower border of square
            float x_intersect_end = clamp(x_eval(p0, delta, sq.w), x_start, x_end);
            float y_at_x_intersect_end = clamp(y_eval(p0, delta, x_intersect_end), sq.y, sq.w);
            // overlap is:
            return (x_intersect_start - x_start) * (sq.w - y_at_x_intersect_start) +
                (x_intersect_end - x_intersect_start) *
                    (sq.w -
                        0.5 * (y_at_x_intersect_start + y_at_x_intersect_end));
        } else {
            // where line hits upper border of square
            float x_intersect_start =
                clamp(x_eval(p0, delta, sq.w), x_start, x_end);
            float y_at_x_intersect_start = clamp(y_eval(p0, delta, x_intersect_start), sq.y, sq.w);
            // where line hits lower border of square
            float x_intersect_end = clamp(x_eval(p0, delta, sq.y), x_start, x_end);
            float y_at_x_intersect_end = clamp(y_eval(p0, delta, x_intersect_end), sq.y, sq.w);
            // overlap is:
            return (x_intersect_end - x_intersect_start) *
                    (sq.w -
                        0.5 * (y_at_x_intersect_start + y_at_x_intersect_end)) +
                (x_end - x_intersect_end) * (sq.w - y_at_x_intersect_end);
        }
    }

    float line_segment_sdf(vec2 p0, vec2 p1, vec2 x) {
        vec2 x_p0 = x - p0;
        vec2 line = p1 - p0;
        float h = clamp(dot(x_p0, line) / dot(line, line), 0.0, 1.0);
        return length(x_p0 - line * h); // * sign(x_p0.x * line.y - x_p0.y * line.x);
    }

    float line_polygon_sdf(in vec2 p0, in vec2 p1, in vec2 x) {
        vec2 p = x - p0;
        vec2 e = p1 - p0;
        float h = clamp(dot(p, e) / dot(e, e), 0.0, 1.0);
        float d = length(p - e * h);
        float s = 1.0;
        // even-odd rule
        if ((p.x > 0.0) != (p.x > e.x)) {
            if ((e.x * p.y < e.y * p.x) != (e.x < 0.0)) {
                s = -s;
            } 
        }
        return d * s;

        // if (abs(p1.x - p0.x) < 1.0e-8) {
        //     // return inf
        //     return 1.0e20;
        // }

        // vec2 line = p1 - p0;

        // vec2 v0 = x - p0;

        // vec2 pq0 = v0 - line * clamp(dot(v0, line) / dot(line, line), 0.0, 1.0);

        // if (x.x >= p0.x && x.x < p1.x && pq0.y > 0.0) {
        //     return -length(pq0);
        // }

        // vec2 pq1 = x - vec2(p0.x, max(x.y, p0.y));
        // vec2 pq2 = x - vec2(p1.x, max(x.y, p1.y));

        // float s = p0.x - p1.x;
        // vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * line.y - v0.y * line.x)),
        //                 vec2(dot(pq1, pq1), s * (p0.x - x.x))),
        //             vec2(dot(pq2, pq2), s * (x.x - p1.x)));

        // return -sqrt(d.x) * (float(d.y > 0.0) * 2.0 - 1.0);
    }

    float cro(in vec2 a, in vec2 b) { return a.x * b.y - a.y * b.x; }

    bool is_clockwise(vec2 a, vec2 b) { return cro(a, b) < 0.0; }

    vec2 perp(vec2 x) {
        return vec2(x.y, -x.x);
    }

    // Circle from 2 points and tangent vector at p
    void circ(vec2 p, vec2 q, vec2 t, out vec2 c, out float r2) {
        vec2 n = perp(t);
        vec2 d = q - p;
        float lambda = 0.5 * dot(d, d) / dot(n, d);
        c = p + lambda * n;
        r2 = lambda * lambda * dot(n, n);
    }

    float arc_sdf(vec2 p, vec2 q, vec2 c, float radius2, vec2 x) {
        if (cro(q - p, x - p) > 0.0) {
            return min(distance(x, p), distance(x, q));
        } else {
            return min(min(distance(x, p), distance(x, q)), abs(distance(x, c) - sqrt(radius2)));
        }
    }

    // Get SDF of circle arc while first constructing arc from two points and tangent vector
    float circle_arc_sdf(vec2 p, vec2 q, vec2 t, vec2 x) {
        vec2 n = perp(t);
        vec2 d = q - p;
        float lambda = 0.5 * dot(d, d) / dot(n, d);
        vec2 c = p + lambda * n;
        float r2 = lambda * lambda * dot(n, n);
        // Early out: If circle is very large, return line SDF.
        if (r2 > 1.e8) {
            return line_polygon_sdf(p, q, x);
        }
        // If point is inside cone (p, c, q), return min dist. to p & q
        // else, return distance to radius.
        p -= c;
        q -= c;
        x -= c;
        // Figure out sign of SDF by using even-odd rule.

        // bool p_to_q = is_clockwise(t, d);
        // float p_cross_q = cro(p, q);
        // // even-odd rule
        // float s = 1.0;
        // float y_on_circle = r2 - x.x * x.x;
        // if (y_on_circle >= 0.0) {
        //     // This implies abs(x.x) < r
        //     y_on_circle = sqrt(y_on_circle);
        //     // bool p_to_neg_c = is_clockwise(p, vec2(x.x, -y_on_circle));
        //     // bool neg_c_to_q = is_clockwise(vec2(x.x, -y_on_circle), q);
        //     // bool p_to_c = is_clockwise(p, vec2(x.x, y_on_circle));
        //     // bool c_to_q = is_clockwise(vec2(x.x, y_on_circle), q);
        //     // if (x.y < -y_on_circle && (p_to_neg_c == p_to_q && neg_c_to_q == p_to_q) == p_to_q) {
        //     //     s = -s;
        //     // }
        //     // if (x.y < y_on_circle && (p_to_c == p_to_q && c_to_q == p_to_q) == p_to_q) {
        //     //     s = -s;
        //     // }
        //     float p_cross_neg_c = cro(p, vec2(x.x, -y_on_circle));
        //     if (x.y < -y_on_circle &&
        //             sign(p_cross_q) * sign(p_cross_neg_c) > 0.0 &&
        //             abs(p_cross_neg_c) < abs(p_cross_q) ) {
        //         s = -s;
        //     }
        //     float p_cross_c = cro(p, vec2(x.x, y_on_circle));
        //     if (x.y < -y_on_circle &&
        //             sign(p_cross_q) * sign(p_cross_c) > 0.0 &&
        //             abs(p_cross_c) < abs(p_cross_q) ) {
        //         s = -s;
        //     }
        // }
        // float p_cross_x = cro(p, x) / length(x);
        // if (sign(p_cross_q) * sign(p_cross_x) > 0.0 &&
        //         abs(p_cross_x) < abs(p_cross_q) ) {
        //     return abs(length(x) - sqrt(r2)) * s;
        // }
        // vec2 xa = x - p;
        // vec2 xb = x - q;
        // return sqrt(min(dot(xa, xa), dot(xb, xb))) * s;

        // Redefine n to be the bisector of the triangle (p, c, q).
        n = lambda * perp(d);
        // This is missing |n|*|p| = |n|*r, but it often cancels out.
        float cos_opening_angle = dot(n, p);
        float s = 1.0;
        float y_on_circle = r2 - x.x * x.x;
        if (y_on_circle >= 0.0) {
            // This implies abs(x.x) < r.
            y_on_circle = sqrt(y_on_circle);
            // Check if line drawn straight from x to infinity
            // crosses the arc zero, one, or two times by checking if 
            // intersection points of circle with line are on arc.
            // alpha < beta => cos(alpha) > cos(beta)
            if (x.y < -y_on_circle && dot(n, vec2(x.x, -y_on_circle)) < cos_opening_angle ) {
                s = -s;
            }
            if (x.y < y_on_circle && dot(n, vec2(x.x, y_on_circle)) < cos_opening_angle) {
                s = -s;
            }
        }
        float dist_xc = length(x);
        float r = sqrt(r2);
        // Here's the only instance where the vector lengths in the
        // comparison of dot products doesn't cancel out.
        if (dot(n, x) * r < cos_opening_angle * dist_xc) {
            return abs(dist_xc - r) * s;
        }
        vec2 xa = x - p;
        vec2 xb = x - q;
        return sqrt(min(dot(xa, xa), dot(xb, xb))) * s;
    }

    void main() {
        fragColor = vec4(0.0);

        // biarc
        if (pointCount >= 4) {
            vec2 p0 = texelFetch(pointsTexture, 0).xy;
            vec2 t0 = texelFetch(pointsTexture, 1).xy;
            vec2 p1 = texelFetch(pointsTexture, 2).xy;
            vec2 t1 = texelFetch(pointsTexture, 3).xy;

            t0 -= p0;
            t1 -= p1;

            t0 = t0 / length(t0);
            t1 = t1 / length(t1);

            // chord given by points on circle
            vec2 d = p0 - p1;
            // vector along which center must lie
            vec2 r = perp(d);
            // center of circle describing locus of joint points
            vec2 c = 0.5 * ((p0 + p1) + dot(d, t0 + t1) / dot(r, t0 - t1) * r);
            vec2 p0_c = p0 - c;

            // radius squared of circle describing locus of joint points
            float r2 = dot(p0_c, p0_c);

            // Joint point is chosen as intersection of chord bisector with circle
            // The closer one is chosen, which gives good results for the tangents we care about.
            vec2 t = c + sign(cro(p0_c, d)) * sqrt(r2) * r / length(r);

            // Find both arcs
            // float radius_0, radius_1;
            // vec2 center_0, center_1;
            // circ(p0, t, t0, center_0, radius_0);
            // circ(p1, t, t1, center_1, radius_1);

            // Evaluate SDF for rendering
            // float sd = 1.0e10;
            // sd = min(sd, arc_sdf(p0, t, center_0, radius_0, gl_FragCoord.xy));
            // sd = min(sd, arc_sdf(t, p1, center_1, radius_1, gl_FragCoord.xy));

            // Find arcs and evaluate SDF in one go
            float sd1 = circle_arc_sdf(p0, t, t0, gl_FragCoord.xy);
            float sd2 = circle_arc_sdf(p1, t, -t1, gl_FragCoord.xy);
            float sd = min(abs(sd1), abs(sd2));
            float s = sign(sd1 * sd2);
            sd = sd * s;
            // sd = sd1;

            // Draw curve
            fragColor.rgb = vec3(1.0 - smoothstep(-1.0, 1.0, sd));

            // circle of joint points
            // d = gl_FragCoord.xy - c;
            // if (dot(d, d) <= r2) {
            //     fragColor.rgb = vec3(1.0);
            // }

            // Highlight joint point
            d = gl_FragCoord.xy - t;
            if (dot(d, d) <= 16.0) {
                fragColor.rgb = vec3(0.0, 0.0, 1.0);
            }           
        }

        for (int i = 0; i < pointCount; ++i) {
            vec2 point = texelFetch(pointsTexture, i).xy;
            float distance = length(gl_FragCoord.xy - point);
            if (distance <= (i == nearestIndex ? 8.0 : 5.0)) {
                fragColor = vec4(
                    i == nearestIndex ? vec3(1.0, 0.5, 0.5) : vec3(1.0, 0.0, 0.0),
                    1.0);
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
    int width = 1200, height = 675;
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
                ImVec4(0.f, 0.f, 0.f, 0.5f));  // Transparent background
            char idxString[64];
            snprintf(idxString, sizeof(idxString), "%zu", i);
            // Calculate the size of the text
            ImVec2 textSize = ImGui::CalcTextSize(idxString);
            ImVec2 textPos(point.x - textSize.x * 0.5f * app.dpi_scale,
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
                    // std::stable_sort(
                    //     pointList.begin(), pointList.end(),
                    //     [](const glm::vec2& a, const glm::vec2& b) {
                    //         return a.x < b.x;
                    //     });

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

                    // std::stable_sort(
                    //     pointListNew.begin(), pointListNew.end(),
                    //     [](const glm::vec2& a, const glm::vec2& b) {
                    //         return a.x < b.x;
                    //     });

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
