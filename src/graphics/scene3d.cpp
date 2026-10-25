#include "graphics/scene3d.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "raylib.h"
#include "rlgl.h"
#include "raygui.h"  // declarations only; the implementation lives in ui_panel.cpp

namespace ml {
namespace {

constexpr float kControlsH = 60.0f;  // bottom strip of the viewport reserved for Replay / Play / slider
constexpr float kDuration = 8.0f;    // seconds for one full replay

const Color kColumn[3] = {{255, 170, 60, 255}, {255, 95, 80, 255}, {120, 200, 100, 255}};  // orange, red, green
const Color kLane[6] = {{255, 170, 60, 255}, {255, 95, 80, 255},   {120, 200, 100, 255},
                        {90, 190, 230, 255}, {200, 130, 230, 255}, {240, 220, 90, 255}};
const Color kAxis = {150, 150, 150, 255};

// Maths has z up; raylib has y up. This keeps the right-handed orientation.
Vector3 world(V3 p) { return {static_cast<float>(p.x), static_cast<float>(p.z), static_cast<float>(-p.y)}; }

void arrow(V3 from, V3 to, Color c, float radius, float headLen, float headRadius) {
    const Vector3 a = world(from), b = world(to);
    const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1e-4f) return;
    const float h = std::min(headLen, len * 0.5f);
    const Vector3 base = {b.x - dx / len * h, b.y - dy / len * h, b.z - dz / len * h};
    DrawCylinderEx(a, base, radius, radius, 8, c);
    DrawCylinderEx(base, b, headRadius, 0.0f, 12, c);
}

float niceStep(float length) {
    for (const float step : {1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f})
        if (length / step <= 10.0f) return step;
    return 100.0f;
}

bool mirrored(const Box& b) { return triple(b.u, b.v, b.w) < -1e-9; }

void drawBoxEdges(const Box& b) {
    const auto c = corners(b.u, b.v, b.w);
    Color edge = b.style == 1 ? Color{120, 120, 120, 255} : mirrored(b) ? Color{255, 120, 120, 255} : Color{90, 190, 230, 255};
    edge = Fade(edge, static_cast<float>(std::max(0.25, b.alpha)));
    for (int i = 0; i < 8; ++i)
        for (int bit = 0; bit < 3; ++bit)
            if (!(i & (1 << bit))) DrawLine3D(world(c[i]), world(c[i | (1 << bit)]), edge);
}

void drawBoxFaces(const Box& b) {
    if (b.style == 1 || b.alpha <= 0.0) return;
    const auto c = corners(b.u, b.v, b.w);
    const Color face = Fade(mirrored(b) ? Color{200, 60, 60, 255} : Color{50, 130, 170, 255},
                            0.22f * static_cast<float>(b.alpha));
    for (int axis = 0; axis < 3; ++axis)
        for (int side = 0; side < 2; ++side) {
            const auto f = faceCorners(axis, side);
            DrawTriangle3D(world(c[f[0]]), world(c[f[1]]), world(c[f[2]]), face);
            DrawTriangle3D(world(c[f[0]]), world(c[f[2]]), world(c[f[3]]), face);
        }
}

// Word-wrapped text; returns the height it used.
float drawWrapped(const Font& font, const std::string& text, Vector2 pos, float size, float maxWidth, Color color) {
    std::string line, word;
    float y = pos.y;
    auto flush = [&] {
        DrawTextEx(font, line.c_str(), {pos.x, y}, size, 0.0f, color);
        y += size * 1.3f;
        line.clear();
    };
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size() && text[i] != ' ') { word += text[i]; continue; }
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && MeasureTextEx(font, candidate.c_str(), size, 0.0f).x > maxWidth) flush();
        line = line.empty() ? word : line + " " + word;
        word.clear();
    }
    if (!line.empty()) flush();
    return y - pos.y;
}

}  // namespace

Scene::Scene(int width, int height) : target_(LoadRenderTexture(width, height)) {
    SetTextureFilter(target_.texture, TEXTURE_FILTER_BILINEAR);
}

Scene::~Scene() { UnloadRenderTexture(target_); }

void Scene::show(const SceneInput& in) {
    input_ = in;
    hasInput_ = true;
    t_ = 0.0f;
    playing_ = true;
    const float extent = static_cast<float>(extentOf(in));
    unit_ = std::max(1.0f, extent / 5.0f);
    axisLen_ = extent + 1.0f;
    tickStep_ = niceStep(axisLen_);
    dist_ = 7.0f + extent * 1.9f;
}

Camera3D Scene::camera() const {
    Camera3D c{};
    c.target = {0.0f, 0.0f, 0.0f};
    c.position = {dist_ * std::cos(pitch_) * std::sin(yaw_), dist_ * std::sin(pitch_),
                  dist_ * std::cos(pitch_) * std::cos(yaw_)};
    c.up = {0.0f, 1.0f, 0.0f};
    c.fovy = 45.0f;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}

void Scene::update(float dt, Rectangle view, const Theme& theme) {
    const Vector2 mouse = GetMousePosition();
    const bool inView = CheckCollisionPointRec(mouse, view);
    const bool onControls = mouse.y > view.y + view.height - kControlsH;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && inView && !onControls) dragging_ = true;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) dragging_ = false;
    if (dragging_) {  // drag to orbit
        const Vector2 d = GetMouseDelta();
        yaw_ -= d.x * 0.008f;
        pitch_ = std::clamp(pitch_ + d.y * 0.008f, -1.4f, 1.4f);
    }
    if (inView && !onControls) dist_ = std::clamp(dist_ * (1.0f - GetMouseWheelMove() * 0.08f), 4.0f, 120.0f);

    if (playing_ && hasInput_) {
        t_ += dt / kDuration;
        if (t_ >= 1.0f) { t_ = 1.0f; playing_ = false; }
    }
    render(theme);
}

void Scene::render(const Theme& theme) {
    frame_ = hasInput_ ? buildFrame(input_, t_) : Frame{};
    const Camera3D cam = camera();

    BeginTextureMode(target_);
    ClearBackground(BLACK);
    BeginMode3D(cam);

    // axes with tick marks and arrow heads
    for (int k = 0; k < 3; ++k) {
        const V3 dir = k == 0 ? V3{1, 0, 0} : k == 1 ? V3{0, 1, 0} : V3{0, 0, 1};
        const V3 side = k == 0 ? V3{0, 1, 0} : k == 1 ? V3{0, 0, 1} : V3{1, 0, 0};
        arrow(dir * -static_cast<double>(axisLen_), dir * static_cast<double>(axisLen_), kAxis, 0.012f * unit_,
              0.45f * unit_, 0.07f * unit_);
        for (float t = tickStep_; t <= axisLen_; t += tickStep_)
            for (const double sign : {1.0, -1.0}) {
                const V3 p = dir * (sign * t);
                DrawLine3D(world(p + side * (0.1 * unit_)), world(p - side * (0.1 * unit_)), kAxis);
            }
    }

    // arrows, then box edges, then the translucent faces last (no depth writes, so they blend cleanly)
    for (const Arrow& a : frame_.arrows) {
        Color c = kColumn[a.column % 3];
        if (a.light) c = ColorBrightness(c, 0.35f);
        arrow(a.from, a.to, c, 0.035f * unit_ * static_cast<float>(a.weight), 0.32f * unit_, 0.09f * unit_ * static_cast<float>(a.weight));
    }
    for (const Box& b : frame_.boxes) drawBoxEdges(b);
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    for (const Box& b : frame_.boxes) drawBoxFaces(b);
    rlEnableDepthMask();
    rlEnableBackfaceCulling();

    EndMode3D();

    // axis letters: project the axis tips into the texture
    for (int k = 0; k < 3; ++k) {
        const V3 tip = (k == 0 ? V3{1, 0, 0} : k == 1 ? V3{0, 1, 0} : V3{0, 0, 1}) * (axisLen_ + 0.9f * unit_);
        const Vector2 p = GetWorldToScreenEx(world(tip), cam, target_.texture.width, target_.texture.height);
        DrawTextEx(theme.bold(), k == 0 ? "x" : k == 1 ? "y" : "z", {p.x - 5.0f, p.y - 10.0f}, 20.0f, 0.0f, kAxis);
    }
    EndTextureMode();
}

void Scene::draw(Rectangle view, const Theme& theme) {
    // textures are stored upside down, hence the negative height
    DrawTextureRec(target_.texture, {0.0f, 0.0f, static_cast<float>(target_.texture.width),
                                     -static_cast<float>(target_.texture.height)}, {view.x, view.y}, WHITE);

    const float left = view.x + 16.0f, textW = view.width - 32.0f;
    if (!hasInput_) {
        const char* hint = "Run an operation on the left to see it here";
        const Vector2 w = MeasureTextEx(theme.regular(), hint, 18.0f, 0.0f);
        DrawTextEx(theme.regular(), hint, {view.x + (view.width - w.x) / 2.0f, view.y + 40.0f}, 18.0f, 0.0f, GRAY);
    } else {
        const float used = drawWrapped(theme.bold(), frame_.caption, {left, view.y + 14.0f}, 16.0f, textW, WHITE);
        drawWrapped(theme.regular(), "Arrows = columns of the matrix. Box = the volume they span: blue keeps orientation, red is mirrored.",
                    {left, view.y + 14.0f + used + 4.0f}, 13.0f, textW, GRAY);
    }

    // thread lanes: each thread's bar fills as its rows of the answer are finished
    if (hasInput_ && input_.kind == Kind::Multiply && input_.result.steps.size() > 1) {
        const auto& steps = input_.result.steps;
        const std::size_t n = std::min<std::size_t>(steps.size(), 6);
        const float y0 = view.y + view.height - kControlsH - 22.0f * static_cast<float>(n) - 26.0f;
        DrawTextEx(theme.bold(), "Threads (animation speed, not real time)", {left, y0}, 14.0f, 0.0f, LIGHTGRAY);
        const double phase = threadPhase(t_);
        for (std::size_t k = 0; k < n; ++k) {
            const float y = y0 + 24.0f + 22.0f * static_cast<float>(k);
            char label[48];
            std::snprintf(label, sizeof label, "thread %llu rows %llu-%llu", static_cast<unsigned long long>(k),
                          static_cast<unsigned long long>(steps[k].firstRow + 1),
                          static_cast<unsigned long long>(steps[k].lastRow));
            DrawTextEx(theme.regular(), label, {left, y - 2.0f}, 14.0f, 0.0f, LIGHTGRAY);
            const Rectangle track = {left + 200.0f, y, 300.0f, 12.0f};
            DrawRectangleRec(track, {40, 40, 40, 255});
            DrawRectangle(static_cast<int>(track.x), static_cast<int>(track.y),
                          static_cast<int>(track.width * static_cast<float>(laneFill(steps, k, phase))),
                          static_cast<int>(track.height), kLane[k % 6]);
        }
    }

    // replay controls
    const float y = view.y + view.height - kControlsH + 14.0f;
    if (!hasInput_) GuiDisable();
    if (GuiButton({left, y, 90.0f, 30.0f}, "Replay")) { t_ = 0.0f; playing_ = true; }
    if (GuiButton({left + 98.0f, y, 90.0f, 30.0f}, playing_ ? "Pause" : "Play")) {
        if (!playing_ && t_ >= 1.0f) t_ = 0.0f;
        playing_ = !playing_;
    }
    const float before = t_;
    GuiSlider({left + 200.0f, y + 6.0f, textW - 200.0f, 18.0f}, nullptr, nullptr, &t_, 0.0f, 1.0f);
    if (t_ != before) playing_ = false;  // scrubbing pauses
    if (!hasInput_) GuiEnable();
}

}  // namespace ml