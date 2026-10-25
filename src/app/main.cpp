// The "Merge": the only file that knows about both calculations and graphics.
#include <exception>
#include <string>

#include "raylib.h"

#include "calculations/ops.h"
#include "graphics/scene3d.h"
#include "graphics/ui_panel.h"

namespace {

// Runs the requested operation on the panel's matrices and returns the text for Output Details.
std::string run(ml::Op op, const ml::PanelState& p) {
    const ml::Schedule sched = p.demoMode ? ml::Schedule::OneTaskPerRow : ml::Schedule::AllCores;
    try {
        ml::Result r;
        switch (op) {
            case ml::Op::Add:          r = ml::add(p.matrixA(), p.matrixB()); break;
            case ml::Op::Subtract:     r = ml::subtract(p.matrixA(), p.matrixB()); break;
            case ml::Op::Multiply:     r = ml::multiply(p.matrixA(), p.matrixB(), sched); break;
            case ml::Op::TransposeA:   r = ml::transpose(p.matrixA());   r.title = "transpose(A)"; break;
            case ml::Op::TransposeB:   r = ml::transpose(p.matrixB());   r.title = "transpose(B)"; break;
            case ml::Op::DeterminantA: r = ml::determinant(p.matrixA()); r.title = "det(A)"; break;
            case ml::Op::DeterminantB: r = ml::determinant(p.matrixB()); r.title = "det(B)"; break;
            case ml::Op::InverseA:     r = ml::inverse(p.matrixA());     r.title = "inverse(A)"; break;
            case ml::Op::InverseB:     r = ml::inverse(p.matrixB());     r.title = "inverse(B)"; break;
        }
        return ml::describe(r);
    } catch (const std::exception& e) {
        return std::string("Error: ") + e.what() + "\n";
    }
}

}  // namespace

int main() {
    constexpr int width = 1536, height = 864;
    InitWindow(width, height, "Matrix Lab");
    SetTargetFPS(60);

    ml::PanelState panel;
    panel.output = "Type values into the grids (empty = 0),\npick an operation and press Run.\n";

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(BLACK);
        const auto cmd = ml::drawPanel(panel);
        ml::drawScene({width / 2.0f, 0.0f, width / 2.0f, static_cast<float>(height)});
        EndDrawing();

        if (cmd) {
            panel.output = run(*cmd, panel);
            panel.scroll = 0.0f;
        }
    }
    CloseWindow();
    return 0;
}