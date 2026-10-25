// The "Merge": the only file that knows about both calculations and graphics.
#include <exception>
#include <optional>
#include <string>

#include "raylib.h"

#include "calculations/ops.h"
#include "graphics/scene3d.h"
#include "graphics/theme.h"
#include "graphics/ui_panel.h"

namespace {

struct Outcome {
    std::string text;                      // for Output Details
    std::optional<ml::SceneInput> scene;   // for the 3D view (empty when something went wrong)
};

// Runs the requested operation on the panel's matrices.
Outcome run(ml::Op op, const ml::PanelState& p) {
    const ml::Schedule sched = p.demoMode ? ml::Schedule::OneTaskPerRow : ml::Schedule::AllCores;
    try {
        ml::SceneInput in;
        switch (op) {
            case ml::Op::Add:
            case ml::Op::Subtract:
            case ml::Op::Multiply:
                in.a = p.matrixA();
                in.b = p.matrixB();
                if (op == ml::Op::Add) in.result = ml::add(in.a, in.b);
                else if (op == ml::Op::Subtract) in.result = ml::subtract(in.a, in.b);
                else {
                    in.result = ml::multiply(in.a, in.b, sched);
                    in.result.comparison = ml::compareMultiply(in.a, in.b, sched);
                }
                break;
            case ml::Op::TransposeA:   in.a = p.matrixA(); in.result = ml::transpose(in.a, 'A'); break;
            case ml::Op::TransposeB:   in.a = p.matrixB(); in.result = ml::transpose(in.a, 'B'); break;
            case ml::Op::DeterminantA: in.a = p.matrixA(); in.result = ml::determinant(in.a, 'A'); break;
            case ml::Op::DeterminantB: in.a = p.matrixB(); in.result = ml::determinant(in.a, 'B'); break;
            case ml::Op::InverseA:     in.a = p.matrixA(); in.result = ml::inverse(in.a, 'A'); break;
            case ml::Op::InverseB:     in.a = p.matrixB(); in.result = ml::inverse(in.a, 'B'); break;
        }
        in.kind = in.result.kind;
        return {ml::describe(in.result), in};
    } catch (const std::exception& e) {
        return {std::string("# Error\n  ") + e.what() + "\n", std::nullopt};
    }
}

}  // namespace

int main() {
    constexpr int width = 1536, height = 864;
    const Rectangle sceneView = {width / 2.0f, 0.0f, width / 2.0f, static_cast<float>(height)};

    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(width, height, "Matrix Lab");
    SetTargetFPS(60);

    {  // everything that owns GPU resources lives in here, so it is released before CloseWindow()
        const ml::Theme theme(std::string(GetApplicationDirectory()) + "assets/fonts/");
        ml::Scene scene(static_cast<int>(sceneView.width), static_cast<int>(sceneView.height));
        ml::PanelState panel;
        panel.output = "# Matrix Lab\n  Type values into the grids (empty = 0).\n"
                       "  Enter moves to the next cell. Ctrl+Enter runs the chosen operation.\n";

        while (!WindowShouldClose()) {
            scene.update(GetFrameTime(), sceneView, theme);

            BeginDrawing();
            ClearBackground(BLACK);
            const auto cmd = ml::drawPanel(panel, theme);
            scene.draw(sceneView, theme);
            EndDrawing();

            if (cmd) {
                const Outcome out = run(*cmd, panel);
                panel.output = out.text;
                panel.scroll = 0.0f;
                if (out.scene) scene.show(*out.scene);
            }
        }
    }
    CloseWindow();
    return 0;
}