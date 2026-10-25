#pragma once
#include "raylib.h"

#include "graphics/scene_math.h"
#include "graphics/theme.h"

// Right half of the window: the 3D view of what was just computed. The geometry (what to draw at which
// moment) lives in scene_math.h; this class only draws it, orbits the camera and plays the animation.
namespace ml {

class Scene {
public:
    Scene(int width, int height);  // needs an open window: creates the offscreen texture
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // Start (or restart) the replay for a finished calculation.
    void show(const SceneInput& input);

    // Camera input, animation clock, and drawing into the offscreen texture. Call before BeginDrawing().
    void update(float dt, Rectangle viewport, const Theme& theme);

    // Puts the texture on screen and draws captions, thread lanes and the replay controls.
    // Call between BeginDrawing() and EndDrawing().
    void draw(Rectangle viewport, const Theme& theme);

private:
    Camera3D camera() const;
    void render(const Theme& theme);

    RenderTexture2D target_{};
    SceneInput input_;
    bool hasInput_ = false;
    Frame frame_;

    float t_ = 0.0f;  // animation position, 0..1
    bool playing_ = false;

    float yaw_ = 0.9f, pitch_ = 0.5f, dist_ = 14.0f;  // orbit camera around the origin
    bool dragging_ = false;

    float axisLen_ = 5.0f, tickStep_ = 1.0f, unit_ = 1.0f;  // sized to fit the numbers being shown
};

}  // namespace ml