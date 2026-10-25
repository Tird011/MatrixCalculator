#include "graphics/scene3d.h"

namespace ml {

void drawScene(Rectangle viewport) {
    DrawRectangleRec(viewport, BLACK);
    const char* text = "3D view - coming in step 2";
    DrawText(text, static_cast<int>(viewport.x + viewport.width / 2) - MeasureText(text, 20) / 2,
             static_cast<int>(viewport.y + viewport.height / 2) - 10, 20, GRAY);
}

}  // namespace ml