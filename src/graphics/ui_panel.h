#pragma once
#include <optional>
#include <string>

#include "graphics/theme.h"
#include "vectors/matrix.h"

// Left half of the window (x 0-768): matrix grids, operation dropdown, buttons, Output Details.
// Knows nothing about calculations: it only reports which operation the user asked for.
namespace ml {

enum class Op {
    Add, Subtract, Multiply,
    TransposeA, TransposeB,
    DeterminantA, DeterminantB,
    InverseA, InverseB,
};

struct PanelState {
    char cellsA[9][16] = {};  // text typed into the 3x3 grids, row by row
    char cellsB[9][16] = {};
    int editing = 0;          // which cell has the cursor: 0-8 = A, 9-17 = B, -1 = none (starts on A[1][1])
    int opIndex = 0;          // dropdown choice: 0 "A + B", 1 "A - B", 2 "A x B"
    bool opOpen = false;
    bool demoMode = true;     // true: one task per row, false: all cores
    std::string output;       // text shown in Output Details ("# " at the start of a line = heading)
    float scroll = 0.0f;      // Output Details scroll offset in pixels

    // Parse the grids. Empty cells count as 0. Throws std::invalid_argument on bad text.
    Matrix matrixA() const;
    Matrix matrixB() const;
};

// Draws the panel and handles its input. Returns the operation requested this frame, if any.
//   Enter = next cell, Tab / Shift+Tab = next / previous cell, Ctrl+Enter = Run.
std::optional<Op> drawPanel(PanelState& s, const Theme& theme);

}  // namespace ml