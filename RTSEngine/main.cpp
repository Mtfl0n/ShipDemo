#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <iostream>
#include <cmath>

float shipX = 0.0f, shipY = 0.5f, shipZ = 0.0f;
float shipVX = 0.0f, shipVZ = 0.0f;
float shipPitch = 0.0f, shipRoll = 0.0f, shipYaw = 0.0f;
const float submersionDepth = 0.1f;    

float getWaveHeight(float x, float z, float time) {
    return 0.1f * sin(2.0f * x + time) * cos(2.0f * z + time);
}

const char* waterVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float time;
out vec3 fragPos;
out vec3 normal;
void main() {
    vec3 pos = aPos;
    pos.y += 0.1 * sin(2.0 * pos.x + time) * cos(2.0 * pos.z + time);
    float dx = -0.2 * cos(2.0 * pos.x + time) * cos(2.0 * pos.z + time);
    float dz = -0.2 * sin(2.0 * pos.x + time) * sin(2.0 * pos.z + time);
    normal = normalize(vec3(dx, 1.0, dz));
    fragPos = pos;
    gl_Position = projection * view * model * vec4(pos, 1.0);
}
)";

const char* waterFragmentShader = R"(
#version 330 core
out vec4 FragColor;
in vec3 fragPos;
in vec3 normal;
uniform float time;
uniform vec3 lightPos;
uniform vec3 viewPos;
void main() {
    vec3 color = vec3(0.0, 0.5, 0.8);
    vec3 lightColor = vec3(1.0, 1.0, 1.0);
    
    vec3 ambient = 0.2 * color;
    vec3 norm = normalize(normal);
    vec3 lightDir = normalize(lightPos - fragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * color * lightColor;
    vec3 viewDir = normalize(viewPos - fragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = 0.5 * spec * lightColor;
    
    vec3 result = ambient + diffuse + specular;
    FragColor = vec4(result, 0.8);
}
)";

const char* basicVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* basicFragmentShader = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 color;
void main() {
    FragColor = vec4(color, 1.0);
}
)";

GLuint compileShader(const char* vertexSrc, const char* fragmentSrc) {
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertexSrc, nullptr);
    glCompileShader(vertex);

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fragmentSrc, nullptr);
    glCompileShader(fragment);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

GLuint waterVAO, waterVBO, waterEBO;
GLuint shipVAO, shipVBO, shipEBO;
GLuint skyboxVAO, skyboxVBO, skyboxEBO;
GLuint waterShader, basicShader;

glm::mat4 projection, view;

void processInput(GLFWwindow* window) {
    float acceleration = 0.01f;    
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
        shipVZ += acceleration;
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
        shipVZ -= acceleration;
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
        shipVX -= acceleration;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
        shipVX += acceleration;
}

void updatePhysics(float deltaTime) {
    shipX += shipVX * deltaTime;
    shipZ += shipVZ * deltaTime;
    shipVX *= 0.99f;  
    shipVZ *= 0.99f;

    float time = (float)glfwGetTime();

    float h_fl = getWaveHeight(shipX - 0.5f, shipZ - 1.0f, time);   
    float h_fr = getWaveHeight(shipX + 0.5f, shipZ - 1.0f, time);   
    float h_bl = getWaveHeight(shipX - 0.5f, shipZ + 1.0f, time);   
    float h_br = getWaveHeight(shipX + 0.5f, shipZ + 1.0f, time);   

    float avg_h = (h_fl + h_fr + h_bl + h_br) / 4.0f;
    float dynamicSubmersion = submersionDepth * (1.0f + 0.2f * sin(time));    
    shipY = avg_h - dynamicSubmersion;

    float front_avg = (h_fl + h_fr) / 2.0f;
    float back_avg = (h_bl + h_br) / 2.0f;
    float left_avg = (h_fl + h_bl) / 2.0f;
    float right_avg = (h_fr + h_br) / 2.0f;
    shipPitch = atan2(front_avg - back_avg, 2.0f) * 0.8f;     
    shipRoll = atan2(left_avg - right_avg, 1.0f) * 0.8f;      

    float velocityAngle = atan2(shipVX, shipVZ);
    shipYaw = glm::mix(shipYaw, velocityAngle, 0.1f);    
}

void init() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const int gridSize = 50;
    const float size = 10.0f;
    std::vector<float> waterVertices;
    std::vector<unsigned int> waterIndices;

    for (int i = 0; i <= gridSize; ++i) {
        for (int j = 0; j <= gridSize; ++j) {
            float x = -size + 2.0f * size * i / gridSize;
            float z = -size + 2.0f * size * j / gridSize;
            waterVertices.push_back(x);
            waterVertices.push_back(0.0f);
            waterVertices.push_back(z);
        }
    }

    for (int i = 0; i < gridSize; ++i) {
        for (int j = 0; j < gridSize; ++j) {
            int topLeft = i * (gridSize + 1) + j;
            int topRight = topLeft + 1;
            int bottomLeft = (i + 1) * (gridSize + 1) + j;
            int bottomRight = bottomLeft + 1;
            waterIndices.push_back(topLeft);
            waterIndices.push_back(bottomLeft);
            waterIndices.push_back(topRight);
            waterIndices.push_back(topRight);
            waterIndices.push_back(bottomLeft);
            waterIndices.push_back(bottomRight);
        }
    }

    glGenVertexArrays(1, &waterVAO);
    glGenBuffers(1, &waterVBO);
    glGenBuffers(1, &waterEBO);
    glBindVertexArray(waterVAO);
    glBindBuffer(GL_ARRAY_BUFFER, waterVBO);
    glBufferData(GL_ARRAY_BUFFER, waterVertices.size() * sizeof(float), waterVertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, waterEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, waterIndices.size() * sizeof(unsigned int), waterIndices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    float shipVertices[] = {
        0.0f, 0.5f,  0.0f,
       -0.5f, 0.0f, -1.0f,
        0.5f, 0.0f, -1.0f,
        0.0f, 0.0f,  1.0f
    };
    unsigned int shipIndices[] = { 0, 1, 2, 0, 1, 3, 0, 2, 3 };

    glGenVertexArrays(1, &shipVAO);
    glGenBuffers(1, &shipVBO);
    glGenBuffers(1, &shipEBO);
    glBindVertexArray(shipVAO);
    glBindBuffer(GL_ARRAY_BUFFER, shipVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(shipVertices), shipVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, shipEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(shipIndices), shipIndices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    float skyboxVertices[] = {
        -10.0f, -10.0f, -10.0f,  10.0f, -10.0f, -10.0f,  10.0f,  10.0f, -10.0f,  -10.0f,  10.0f, -10.0f,
        -10.0f, -10.0f,  10.0f,  10.0f, -10.0f,  10.0f,  10.0f,  10.0f,  10.0f,  -10.0f,  10.0f,  10.0f
    };
    unsigned int skyboxIndices[] = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        0, 4, 7, 7, 3, 0,
        1, 5, 6, 6, 2, 1,
        3, 2, 6, 6, 7, 3,
        0, 1, 5, 5, 4, 0
    };

    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glGenBuffers(1, &skyboxEBO);
    glBindVertexArray(skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skyboxEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(skyboxIndices), skyboxIndices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    waterShader = compileShader(waterVertexShader, waterFragmentShader);
    basicShader = compileShader(basicVertexShader, basicFragmentShader);

    projection = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);
    view = glm::lookAt(glm::vec3(0.0f, 5.0f, 10.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

void render() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(waterShader);
    glm::mat4 waterModel = glm::mat4(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(waterShader, "model"), 1, GL_FALSE, glm::value_ptr(waterModel));
    glUniformMatrix4fv(glGetUniformLocation(waterShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(waterShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(waterShader, "time"), (float)glfwGetTime());
    glUniform3f(glGetUniformLocation(waterShader, "lightPos"), 10.0f, 10.0f, 10.0f);
    glUniform3f(glGetUniformLocation(waterShader, "viewPos"), 0.0f, 5.0f, 10.0f);
    glBindVertexArray(waterVAO);
    glDrawElements(GL_TRIANGLES, 50 * 50 * 6, GL_UNSIGNED_INT, 0);

    glUseProgram(basicShader);
    glm::mat4 shipModel = glm::translate(glm::mat4(1.0f), glm::vec3(shipX, shipY, shipZ));
    shipModel = glm::rotate(shipModel, shipYaw, glm::vec3(0.0f, 1.0f, 0.0f));      
    shipModel = glm::rotate(shipModel, shipPitch, glm::vec3(1.0f, 0.0f, 0.0f));   
    shipModel = glm::rotate(shipModel, shipRoll, glm::vec3(0.0f, 0.0f, 1.0f));    
    glUniformMatrix4fv(glGetUniformLocation(basicShader, "model"), 1, GL_FALSE, glm::value_ptr(shipModel));
    glUniformMatrix4fv(glGetUniformLocation(basicShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(basicShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(basicShader, "color"), 1.0f, 0.0f, 0.0f);
    glBindVertexArray(shipVAO);
    glDrawElements(GL_TRIANGLES, 9, GL_UNSIGNED_INT, 0);

    glDepthFunc(GL_LEQUAL);
    glm::mat4 skyboxModel = glm::mat4(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(basicShader, "model"), 1, GL_FALSE, glm::value_ptr(skyboxModel));
    glUniform3f(glGetUniformLocation(basicShader, "color"), 0.5f, 0.7f, 1.0f);
    glBindVertexArray(skyboxVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    glDepthFunc(GL_LESS);
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    projection = glm::perspective(glm::radians(45.0f), (float)width / height, 0.1f, 100.0f);
}

int main() {
    if (!glfwInit()) {
        std::cerr << "Не удалось инициализировать GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Корабль на волнах", nullptr, nullptr);
    if (!window) {
        std::cerr << "Не удалось создать окно GLFW" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Не удалось инициализировать GLAD" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    init();

    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        float deltaTime = (float)(currentTime - lastTime);
        lastTime = currentTime;

        processInput(window);    
        updatePhysics(deltaTime);
        render();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &waterVAO);
    glDeleteBuffers(1, &waterVBO);
    glDeleteBuffers(1, &waterEBO);
    glDeleteVertexArrays(1, &shipVAO);
    glDeleteBuffers(1, &shipVBO);
    glDeleteBuffers(1, &shipEBO);
    glDeleteVertexArrays(1, &skyboxVAO);
    glDeleteBuffers(1, &skyboxVBO);
    glDeleteBuffers(1, &skyboxEBO);
    glDeleteProgram(waterShader);
    glDeleteProgram(basicShader);

    glfwTerminate();
    return 0;
}