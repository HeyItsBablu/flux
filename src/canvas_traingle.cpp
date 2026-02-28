// test_canvas_triangle_simple.cpp
#include "flux.hpp"

// ============================================================================
// TriangleSurface
// ============================================================================

class TriangleSurface : public RenderSurface {
public:
    void initialize(int w, int h) override {
        w_ = w; h_ = h;
        buildShaders();
        buildTriangleVAO();
    }

    void resize(int w, int h) override {
        w_ = w; h_ = h;
    }

    void update(double) override {}

    void render(const float mvp[16]) override {
        glClearColor(0.08f, 0.08f, 0.10f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        GL.useProgram(prog_);
        GL.uniformMatrix4fv(uMVP_, 1, GL_FALSE, mvp);
        GL.bindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        GL.bindVertexArray(0);
        GL.useProgram(0);
    }

    void destroy() override {
        if (prog_) { GL.deleteProgram(prog_); prog_ = 0; }
        if (vao_)  { GL.deleteVertexArrays(1, &vao_); vao_ = 0; }
        if (vbo_)  { GL.deleteBuffers(1, &vbo_);      vbo_ = 0; }
    }

private:
    int    w_ = 0, h_ = 0;
    GLuint prog_ = 0, vao_ = 0, vbo_ = 0;
    GLint  uMVP_ = -1;

    void buildShaders() {
        const char *vert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec3 aColor;
uniform mat4 uMVP;
out vec3 vColor;
void main(){
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)GLSL";
        const char *frag = R"GLSL(
#version 330 core
in vec3 vColor;
out vec4 fragColor;
void main(){
    fragColor = vec4(vColor, 1.0);
}
)GLSL";
        prog_ = glutil::linkProgram(vert, frag);
        assert(prog_);
        uMVP_ = GL.getUniformLocation(prog_, "uMVP");
    }

    void buildTriangleVAO() {
        // Triangle in canvas space — canvas is 512×512, origin bottom-left
        // x, y,   r, g, b
        float verts[] = {
            256.f, 460.f,   1.0f, 0.2f, 0.2f,   // top          — red
             80.f,  80.f,   0.2f, 1.0f, 0.3f,   // bottom-left  — green
            432.f,  80.f,   0.2f, 0.4f, 1.0f,   // bottom-right — blue
        };
        GL.genVertexArrays(1, &vao_);
        GL.genBuffers(1, &vbo_);
        GL.bindVertexArray(vao_);
        GL.bindBuffer(GL_ARRAY_BUFFER, vbo_);
        GL.bufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
        GL.enableVertexAttribArray(0);
        GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*5, (void*)0);
        GL.enableVertexAttribArray(1);
        GL.vertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*5, (void*)(sizeof(float)*2));
        GL.bindVertexArray(0);
    }
};

// ============================================================================
// TriangleApp
// ============================================================================

class TriangleApp : public Component {
public:
    WidgetPtr build() override {

        auto canvas = Canvas(512, 512);
        canvas->setCanvasSize(512, 512);
        canvas->setViewportEnabled(false);
        canvas->setSurface<TriangleSurface>();

        return Scaffold(
            AppBar("Triangle — No Viewport"),
            canvas
        );
    }
};

// ============================================================================
// Entry point
// ============================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    FluxUI app(hInstance);
    app.build([&](){
        return FluxApp("Triangle", BuildComponent<TriangleApp>(), AppTheme::dark());
    });
    app.createWindow("Triangle", 560, 610);
    return app.run();
}