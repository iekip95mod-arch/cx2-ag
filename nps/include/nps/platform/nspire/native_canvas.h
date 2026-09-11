#ifndef NPS_NATIVE_CANVAS_H
#define NPS_NATIVE_CANVAS_H

namespace nps::ui { class Canvas; }
namespace nps {
bool paint_native_canvas(const ui::Canvas &canvas, void **graphics_context);
}
#endif
