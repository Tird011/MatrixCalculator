#pragma once
#include <string>

#include "raylib.h"

namespace ml {

// Loads JetBrains Mono (regular + bold) from `fontDir` and makes raygui use it.
// A monospaced font keeps matrix columns lined up. Falls back to raylib's built-in font if the
// files are missing, so the app still runs. Create after InitWindow, destroy before CloseWindow.
class Theme {
public:
    explicit Theme(const std::string& fontDir);
    ~Theme();
    Theme(const Theme&) = delete;
    Theme& operator=(const Theme&) = delete;

    const Font& regular() const { return regular_; }
    const Font& bold() const { return bold_; }
    float advance(float size) const { return MeasureTextEx(regular_, "M", size, 0.0f).x; }  // one glyph's width

private:
    Font regular_{};
    Font bold_{};
    bool owned_ = false;
};

}  // namespace ml
