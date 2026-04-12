#include "flux/flux.hpp"

// ============================================================================
// TriangleSurface — draws a single colored triangle
// ============================================================================

static const char *kTriVert = R"GLSL(
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

static const char *kTriFrag = R"GLSL(
#version 330 core
in  vec3 vColor;
out vec4 fragColor;
void main(){
    fragColor = vec4(vColor, 1.0);
}
)GLSL";

class TriangleSurface : public RenderSurface {
public:
  void initialize(int w, int h) override {
    prog_ = glutil::linkProgram(kTriVert, kTriFrag);

    // Triangle vertices: position(x,y)  color(r,g,b)
    // Coordinates are in canvas space (0..w, 0..h)
    float cx = w * 0.5f, cy = h * 0.5f, r = std::min(w, h) * 0.35f;
    float verts[] = {
        cx,     cy - r, 1.f, 0.f, 0.f, // top    — red
        cx - r, cy + r, 0.f, 1.f, 0.f, // bottom-left  — green
        cx + r, cy + r, 0.f, 0.f, 1.f, // bottom-right — blue
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    // aPos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void *)0);
    // aColor
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void *)(2 * sizeof(float)));
    glBindVertexArray(0);
  }

  void resize(int, int) override {}
  void update(double) override {}

  void render(const float mvp[16]) override {
    if (!mvpValid(mvp))
      return;
    glClearColor(0.08f, 0.08f, 0.10f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(prog_);
    glUniformMatrix4fv(glGetUniformLocation(prog_, "uMVP"), 1, GL_FALSE, mvp);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
  }

  void destroy() override {
    if (prog_) {
      glDeleteProgram(prog_);
      prog_ = 0;
    }
    if (vao_) {
      glDeleteVertexArrays(1, &vao_);
      vao_ = 0;
    }
    if (vbo_) {
      glDeleteBuffers(1, &vbo_);
      vbo_ = 0;
    }
  }

private:
  GLuint prog_ = 0, vao_ = 0, vbo_ = 0;
};

// ============================================================================
// App
// ============================================================================

class TriangleApp : public Widget {
  static constexpr int kCanvasW = 512, kCanvasH = 512;

public:
  WidgetPtr build() override {
    auto canvas = std::make_shared<CanvasWidget>();
    canvas->setSize(kCanvasW, kCanvasH);
    canvas->setCanvasSize(kCanvasW, kCanvasH);
    canvas->setScrollbarsEnabled(false);
    canvas->setSurface<TriangleSurface>();

   return Scaffold(nullptr, Center(canvas));
  }
};

// ============================================================================
// Entry point
// ============================================================================

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Triangle", std::make_shared<TriangleApp>(), AppTheme::dark(),
                 false,  // debugShowWidgetBounds
                 512,    // width
                 512,    // height
                 false,  // maximize
                 false); // fullscreen
}