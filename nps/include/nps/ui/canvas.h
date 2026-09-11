#ifndef NPS_UI_CANVAS_H
#define NPS_UI_CANVAS_H

#include <cstdint>
#include <etl/vector.h>

namespace nps::ui {

struct Rect { int x = 0, y = 0, width = 0, height = 0; };
struct Color { uint8_t red = 0, green = 0, blue = 0; };
struct Fill { Rect bounds; Color color; };
enum class Icon : uint8_t { Check, Warning, Information, Left, Right };

class Canvas {
  public:
    Canvas(int width, int height);
    bool fill(Rect bounds, Color color);
    bool icon(Icon icon, int x, int y, Color foreground, Color background);
    bool panel(int header_height, int footer_height);
    bool good() const { return good_; }
    const etl::ivector<Fill> &fills() const { return fills_; }
    void paint(void *context, void (*fill_rect)(void *, const Fill &)) const;

  private:
    int width_, height_;
    bool good_ = true;
    etl::vector<Fill, 256> fills_;
};

}
#endif
