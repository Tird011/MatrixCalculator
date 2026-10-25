#include "graphics/ui_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <vector>

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
const Color kHeading = {30, 90, 170, 255};

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

void heading(const Theme& theme, const char* text, int centerX, int y) {
    constexpr float size = 24.f;
    const Vector2 w = MeasureTextEx(theme.bold(), text, size, 0.f);
    DrawTextEx(theme.bold(), text, {static_cast<float>(centerX) - w.x / 2.f, static_cast<float>(y)}, size, 0.f, BLACK);
}

void grid(PanelState& s, char (*cells)[16], int base, const float rows[3]) {
    for (int i = 0; i < 9; ++i) {
        const Rectangle r = {kCellX[i % 3], rows[i / 3], kCellW, kCellH};
        if (GuiTextBox(r, cells[i], 16, s.editing == base + i))
            s.editing = (s.editing == base + i) ? -1 : base + i;
    }
}

// Buttons in the blank space beside a grid, lined up with the grid's rows.
std::optional<Op> sideButtons(PanelState& s, char (*cells)[16], int base, const float rows[3], Op transpose,
                              Op det, Op inv) {
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
        s.editing = base;  // ready to type the first cell
    }
    return cmd;
}

// ---------- Output Details ----------
struct Line {
    std::string text;
    bool heading = false;
};

// Splits text into display lines: "# " marks a heading, long lines wrap at word boundaries and keep
// their indentation.
std::vector<Line> layout(const std::string& text, std::size_t cols) {
    std::vector<Line> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        start = nl == std::string::npos ? text.size() + 1 : nl + 1;

        const bool isHeading = line.rfind("# ", 0) == 0;
        if (isHeading) line.erase(0, 2);
        const std::size_t firstChar = line.find_first_not_of(' ');
        const std::size_t indent = firstChar == std::string::npos ? 0 : firstChar;
        const std::string pad(indent, ' ');
        std::string rest = firstChar == std::string::npos ? std::string() : line.substr(indent);

        bool first = true;
        while (cols > indent + 8 && indent + rest.size() > cols) {
            std::size_t cut = rest.rfind(' ', cols - indent);
            if (cut == std::string::npos || cut == 0) cut = cols - indent;  // no space to break at
            out.push_back({pad + rest.substr(0, cut), isHeading && first});
            rest.erase(0, cut);
            const std::size_t next = rest.find_first_not_of(' ');
            rest.erase(0, next == std::string::npos ? rest.size() : next);
            first = false;
        }
        out.push_back({pad + rest, isHeading && first});
    }
    return out;
}

// Read-only, scrollable output box (mouse wheel while hovering).
void drawOutput(PanelState& s, Rectangle box, const Theme& theme) {
    constexpr float size = 15.f, lineH = 20.f, pad = 8.f;
    const auto cols = static_cast<std::size_t>(std::max(10.f, (box.width - 2.f * pad) / theme.advance(size)));
    const std::vector<Line> lines = layout(s.output, cols);

    DrawRectangleRec(box, WHITE);
    DrawRectangleLinesEx(box, 1.f, GRAY);

    const float maxScroll = std::max(0.f, static_cast<float>(lines.size()) * lineH + 2.f * pad - box.height);
    if (CheckCollisionPointRec(GetMousePosition(), box)) s.scroll -= GetMouseWheelMove() * lineH * 2.f;
    s.scroll = std::clamp(s.scroll, 0.f, maxScroll);

    BeginScissorMode(static_cast<int>(box.x) + 1, static_cast<int>(box.y) + 1, static_cast<int>(box.width) - 2,
                     static_cast<int>(box.height) - 2);
    float y = box.y + pad - s.scroll;
    for (const Line& l : lines) {
        if (y > box.y - lineH && y < box.y + box.height && !l.text.empty())
            DrawTextEx(l.heading ? theme.bold() : theme.regular(), l.text.c_str(), {box.x + pad, y}, size, 0.f,
                       l.heading ? kHeading : BLACK);
        y += lineH;
    }
    EndScissorMode();
}

}  // namespace

Matrix PanelState::matrixA() const { return parse(cellsA, 'A'); }
Matrix PanelState::matrixB() const { return parse(cellsB, 'B'); }

std::optional<Op> drawPanel(PanelState& s, const Theme& theme) {
    static constexpr Op kBinary[3] = {Op::Add, Op::Subtract, Op::Multiply};
    std::optional<Op> cmd;

    GuiSetStyle(DEFAULT, TEXT_SIZE, 18);
    GuiSetStyle(TEXTBOX, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);
    DrawRectangle(0, 0, kPanelW, kWindowH, kPanelBg);

    const int before = s.editing;  // keyboard navigation below is relative to where the cursor started
    const bool enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    heading(theme, "Matrix A:", 190, 84);
    heading(theme, "Matrix B:", 190, 334);
    DrawTextEx(theme.bold(), "Output Details:", {46.f, 545.f}, 24.f, 0.f, BLACK);
    DrawTextEx(theme.regular(), "Enter: next cell    Tab: next / Shift+Tab: back    Ctrl+Enter: Run", {37.f, 842.f},
               14.f, 0.f, GRAY);

    // While the dropdown list is open it covers other widgets, so lock them and draw it last.
    if (s.opOpen) GuiLock();

    grid(s, s.cellsA, 0, kRowsA);
    if (auto c = sideButtons(s, s.cellsA, 0, kRowsA, Op::TransposeA, Op::DeterminantA, Op::InverseA)) cmd = c;
    grid(s, s.cellsB, 9, kRowsB);
    if (auto c = sideButtons(s, s.cellsB, 9, kRowsB, Op::TransposeB, Op::DeterminantB, Op::InverseB)) cmd = c;

    // Typing flow: Enter / Tab moves to the next cell (A's last cell leads into B's first). Enter on the very
    // last cell ends editing. Ctrl+Enter runs the chosen operation and keeps the cursor where it was.
    if (before >= 0 && !s.opOpen) {
        if (enter && ctrl) {
            cmd = kBinary[s.opIndex];
            s.editing = before;
        } else if (enter) {
            s.editing = before == 17 ? -1 : before + 1;
        } else if (IsKeyPressed(KEY_TAB)) {
            s.editing = shift ? (before + 17) % 18 : (before + 1) % 18;
        }
    }

    if (GuiButton({300.f, kBandY, 90.f, kBandH}, "Run")) cmd = kBinary[s.opIndex];
    if (GuiButton({400.f, kBandY, 110.f, kBandH}, "Swap A<>B")) {
        std::swap(s.cellsA, s.cellsB);
        s.editing = -1;
    }
    GuiToggle({520.f, kBandY, 203.f, kBandH}, s.demoMode ? "Mode: 1 task per row" : "Mode: all cores",
              &s.demoMode);

    drawOutput(s, kOutputBox, theme);

    GuiUnlock();
    if (GuiDropdownBox({77.f, kBandY, 206.f, kBandH}, "A + B;A - B;A x B", &s.opIndex, s.opOpen))
        s.opOpen = !s.opOpen;

    return cmd;
}

}  // namespace ml