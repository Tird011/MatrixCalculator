#include "graphics/ui_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <utility>

#include "raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

namespace ml {
namespace {

// Layout copied from the mockup (window 1536x864, panel is the left 768 px).
constexpr int kPanelW = 768;
constexpr int kWindowH = 864;
constexpr float kCellX[3] = {37.f, 133.f, 229.f};
constexpr float kCellW = 81.f, kCellH = 25.f;
constexpr float kRowsA[3] = {130.f, 168.f, 206.f};
constexpr float kRowsB[3] = {382.f, 420.f, 458.f};
constexpr float kSideX1 = 340.f, kSideX2 = 520.f, kSideW = 170.f;  // buttons beside each grid
constexpr float kBandY = 284.f, kBandH = 26.f;                     // dropdown / Run / Swap / mode row
constexpr Rectangle kOutputBox = {37.f, 581.f, 686.f, 254.f};
const Color kPanelBg = {236, 236, 236, 255};

Matrix parse(const char (*cells)[16], char name) {
    Matrix m(3, 3);
    for (int i = 0; i < 9; ++i) {
        const char* text = cells[i];
        if (*text == '\0') continue;  // empty cell counts as 0
        char* end = nullptr;
        const double v = std::strtod(text, &end);
        while (*end == ' ') ++end;
        if (*end != '\0' || !std::isfinite(v))
            throw std::invalid_argument(std::string("Matrix ") + name + " row " + std::to_string(i / 3 + 1) +
                                        ", column " + std::to_string(i % 3 + 1) + " is not a number: \"" +
                                        text + "\"");
        m(i / 3, i % 3) = v;
    }
    return m;
}

void heading(const char* text, int centerX, int y) {
    DrawText(text, centerX - MeasureText(text, 24) / 2, y, 24, BLACK);
}

void grid(PanelState& s, char (*cells)[16], int base, const float rows[3]) {
    for (int i = 0; i < 9; ++i) {
        const Rectangle r = {kCellX[i % 3], rows[i / 3], kCellW, kCellH};
        if (GuiTextBox(r, cells[i], 16, s.editing == base + i))
            s.editing = (s.editing == base + i) ? -1 : base + i;
    }
}

// Buttons in the blank space beside a grid, lined up with the grid's rows.
std::optional<Op> sideButtons(PanelState& s, char (*cells)[16], const float rows[3], Op transpose, Op det,
                              Op inv) {
    std::optional<Op> cmd;
    if (GuiButton({kSideX1, rows[0], kSideW, kCellH}, "Transpose")) cmd = transpose;
    if (GuiButton({kSideX2, rows[0], kSideW, kCellH}, "Determinant")) cmd = det;
    if (GuiButton({kSideX1, rows[1], kSideW, kCellH}, "Inverse")) cmd = inv;
    if (GuiButton({kSideX2, rows[1], kSideW, kCellH}, "Random")) {
        for (int i = 0; i < 9; ++i) std::snprintf(cells[i], 16, "%d", GetRandomValue(-9, 9));
        s.editing = -1;
    }
    if (GuiButton({kSideX1, rows[2], kSideW, kCellH}, "Clear")) {
        for (int i = 0; i < 9; ++i) cells[i][0] = '\0';
        s.editing = -1;
    }
    return cmd;
}

// Read-only, scrollable, monospaced text (the default font is not monospaced, so place each glyph).
void drawOutput(PanelState& s, Rectangle box) {
    constexpr float fontSize = 16.f, advance = 9.f, lineH = 20.f, pad = 8.f;
    DrawRectangleRec(box, WHITE);
    DrawRectangleLinesEx(box, 1.f, GRAY);

    const float lines = static_cast<float>(std::count(s.output.begin(), s.output.end(), '\n') + 1);
    const float maxScroll = std::max(0.f, lines * lineH + 2 * pad - box.height);
    if (CheckCollisionPointRec(GetMousePosition(), box)) s.scroll -= GetMouseWheelMove() * lineH * 2.f;
    s.scroll = std::clamp(s.scroll, 0.f, maxScroll);

    BeginScissorMode(static_cast<int>(box.x) + 1, static_cast<int>(box.y) + 1, static_cast<int>(box.width) - 2,
                     static_cast<int>(box.height) - 2);
    float x = box.x + pad, y = box.y + pad - s.scroll;
    for (const char c : s.output) {
        if (c == '\n') { y += lineH; x = box.x + pad; continue; }
        if (y > box.y - lineH && y < box.y + box.height) {
            const char glyph[2] = {c, '\0'};
            DrawTextEx(GetFontDefault(), glyph, {x, y}, fontSize, 0.f, BLACK);
        }
        x += advance;
    }
    EndScissorMode();
}

}  // namespace

Matrix PanelState::matrixA() const { return parse(cellsA, 'A'); }
Matrix PanelState::matrixB() const { return parse(cellsB, 'B'); }

std::optional<Op> drawPanel(PanelState& s) {
    static constexpr Op kBinary[3] = {Op::Add, Op::Subtract, Op::Multiply};
    std::optional<Op> cmd;

    GuiSetStyle(DEFAULT, TEXT_SIZE, 18);
    GuiSetStyle(TEXTBOX, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);
    DrawRectangle(0, 0, kPanelW, kWindowH, kPanelBg);
    if (s.editing >= 0 && IsKeyPressed(KEY_TAB)) s.editing = (s.editing + 1) % 18;

    heading("Matrix A:", 190, 84);
    heading("Matrix B:", 190, 334);
    DrawText("Output Details:", 46, 545, 24, BLACK);

    // While the dropdown list is open it covers other widgets, so lock them and draw it last.
    if (s.opOpen) GuiLock();

    grid(s, s.cellsA, 0, kRowsA);
    if (auto c = sideButtons(s, s.cellsA, kRowsA, Op::TransposeA, Op::DeterminantA, Op::InverseA)) cmd = c;
    grid(s, s.cellsB, 9, kRowsB);
    if (auto c = sideButtons(s, s.cellsB, kRowsB, Op::TransposeB, Op::DeterminantB, Op::InverseB)) cmd = c;

    if (GuiButton({300.f, kBandY, 90.f, kBandH}, "Run")) cmd = kBinary[s.opIndex];
    if (GuiButton({400.f, kBandY, 110.f, kBandH}, "Swap A<>B")) {
        std::swap(s.cellsA, s.cellsB);
        s.editing = -1;
    }
    GuiToggle({520.f, kBandY, 203.f, kBandH}, s.demoMode ? "Mode: 1 task per row" : "Mode: all cores",
              &s.demoMode);

    drawOutput(s, kOutputBox);

    GuiUnlock();
    if (GuiDropdownBox({77.f, kBandY, 206.f, kBandH}, "A + B;A - B;A x B", &s.opIndex, s.opOpen))
        s.opOpen = !s.opOpen;

    return cmd;
}

}  // namespace ml