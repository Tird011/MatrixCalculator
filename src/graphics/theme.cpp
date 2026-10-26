#include "graphics/theme.h"

#include "raygui.h"  // declarations only; the implementation lives in ui_panel.cpp

namespace ml {

Theme::Theme(const std::string& fontDir) {
    const std::string regularPath = fontDir + "JetBrainsMono-Regular.ttf";
    const std::string boldPath = fontDir + "JetBrainsMono-Bold.ttf";

    if (FileExists(regularPath.c_str()) && FileExists(boldPath.c_str())) {
        int codepoints[95];  // printable ASCII: all the app's text is plain ASCII
        for (int i = 0; i < 95; ++i) codepoints[i] = 32 + i;
        regular_ = LoadFontEx(regularPath.c_str(), 32, codepoints, 95);
        bold_ = LoadFontEx(boldPath.c_str(), 32, codepoints, 95);
        SetTextureFilter(regular_.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(bold_.texture, TEXTURE_FILTER_BILINEAR);
        owned_ = true;
    } else {
        TraceLog(LOG_WARNING, "MatrixLab: fonts not found in %s, using the built-in font", fontDir.c_str());
        regular_ = bold_ = GetFontDefault();
    }
    GuiSetFont(regular_);
}

Theme::~Theme() {
    GuiSetFont(GetFontDefault());
    if (owned_) {
        UnloadFont(regular_);
        UnloadFont(bold_);
    }
}

}  // namespace ml
