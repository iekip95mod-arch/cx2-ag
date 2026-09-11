#include "nps/platform/nspire/native_canvas.h"
#include "nps/ui/canvas.h"

#include <ngc.h>

namespace nps {
bool paint_native_canvas(const ui::Canvas &canvas, void **graphics_context) {
    if (!graphics_context || !canvas.good()) return false;
    canvas.paint(graphics_context, [](void *context, const ui::Fill &fill) {
        Gc gc = static_cast<Gc>(context);
        gui_gc_setColorRGB(gc, fill.color.red, fill.color.green, fill.color.blue);
        gui_gc_fillRect(gc, fill.bounds.x, fill.bounds.y, fill.bounds.width, fill.bounds.height);
    });
    return true;
}
}
