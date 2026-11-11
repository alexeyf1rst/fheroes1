#ifndef HOMM1_BASE_GOLDBUTTON_H
#define HOMM1_BASE_GOLDBUTTON_H

#include <BASE/bmap2.h>
#include <BASE/bitmap.h>
#include <BASE/button.h>
#include <BASE/icon.h>
#include <BASE/Icon2b.h>

#include <algorithm>
#include <vector>
#include <BASE/heroWindow.h>
#include <BASE/font.h>
#include <BASE/heroWindowManager.h>
#include <BASE/palette.h>
#include <BASE/textWidget.h>
#include <SOURCE/KB.h>

#include <cstdlib>
#include <cstring>

class goldButton : public textWidget {
public:
    enum Mark { Label, Left, Right, Exchange };

    goldButton(i16 x, i16 y, i16 width, i16 height, i16 id, const char* label, Mark mark = Label)
        : textWidget(x, y, width, height, strdup(label), "smalfont.fnt", TEXT_WIDGET_PLAIN_COLOR,
                     id, WIDGET_KIND_TEXT), m_mark(mark) {}

    void Draw() override {
        const int x = m_owner->m_posX + m_x;
        const int y = m_owner->m_posY + m_y;
        const int shade = MatchGold(25, 15, 2);
        const int shine = MatchGold(63, 49, 13);
        const int gold = MatchGold(49, 31, 3);
        bitmap* screen = gWindowManager->m_screen;
        const int bevel = MatchGold(39, 23, 2);
        const int highlight = MatchGold(58, 42, 5);
        FillBitmapArea(screen, x + 1, y, m_width - 2, m_height, shade);
        FillBitmapArea(screen, x, y + 1, m_width, m_height - 2, shade);
        FillBitmapArea(screen, x + 1, y + 1, m_width - 2, m_height - 3, bevel);
        FillBitmapArea(screen, x + 2, y + 1, m_width - 4, 1, shine);
        FillBitmapArea(screen, x + 1, y + 2, 1, m_height - 5, highlight);
        FillBitmapArea(screen, x + 3, y + 3, m_width - 6, m_height - 6, gold);
        FillBitmapArea(screen, x + 3, y + 3, m_width - 6, 1, highlight);
        if (m_mark == Label) {
            const i16 top = y + (m_height - m_font->m_height) / 2;
            m_font->DrawBoundedString(m_text, x + 2, top + 1, m_width - 3, m_font->m_height, shade, m_alignment);
            m_font->DrawBoundedString(m_text, x + 1, top, m_width - 3, m_font->m_height, shine, m_alignment);
        } else {
            const auto arrow = [&](int cy, bool right) {
                const int cx = x + m_width / 2;
                FillBitmapArea(screen, cx - 7, cy - 1, 15, 3, shade);
                for (int i = 0; i < 5; ++i)
                    FillBitmapArea(screen, cx + (right ? 4 + i : -4 - i), cy - 4 + i, 1,
                                   9 - 2 * i, shade);
            };
            const int center = y + m_height / 2;
            if (m_mark == Exchange) {
                arrow(center - 3, true);
                arrow(center + 3, false);
            } else {
                arrow(center, m_mark == Right);
            }
        }
    }

    static int MatchGold(int r, int g, int b, bool stable = false) {
        int best = 10, distance = 3 * 64 * 64;
        for (int index = 10; index < 246; ++index) {
            if (stable && index >= PALETTE_CYCLE_FIRST && index < PALETTE_CYCLE_FIRST + PALETTE_CYCLE_COLOR_COUNT)
                continue;
            const auto* color = gPalette->m_data + 3 * index;
            const int dr = r - color[0], dg = g - color[1], db = b - color[2];
            const int error = dr * dr + dg * dg + db * db;
            if (error < distance) {
                best = index;
                distance = error;
            }
        }
        return best;
    }

private:
    Mark m_mark;
};

// Keep the original button's side bevels and extend its unlettered right margin.
class battleGoldButton : public textWidget {
public:
    battleGoldButton(i16 x, i16 y, i16 width, i16 height, i16 id, const char* label,
                     const heroWindow& combat, i16 sourceId)
        : textWidget(x, y, width, height, strdup(label), "smalfont.fnt",
                     TEXT_WIDGET_PLAIN_COLOR, id, WIDGET_KIND_TEXT) {
        for (auto* item = combat.m_widgetListHead; item; item = item->m_next) {
            if (item->m_id != sourceId)
                continue;
            if (const auto* original = dynamic_cast<const button*>(item)) {
                LoadFace(*original->m_icon, original->m_normalFrame, m_normal);
                LoadFace(*original->m_icon, original->m_pressedFrame, m_pressed);
            }
            break;
        }
    }

    void Draw() override {
        auto* screen = gWindowManager->m_screen;
        const int x = m_owner->m_posX + m_x;
        const int y = m_owner->m_posY + m_y;
        const bool pressed = (m_flags & WIDGET_FLAG_SELECTED) != 0;
        const auto& face = pressed && !m_pressed.empty() ? m_pressed : m_normal;
        if (face.empty()) {
            FillBitmapArea(screen, x, y, m_width, m_height, goldButton::MatchGold(41, 26, 5, true));
        } else {
            for (int row = 0; row < m_height; ++row)
                for (int col = 0; col < m_width; ++col) {
                    const auto pixel = face[row * m_width + col];
                    if (pixel != 0)
                        screen->m_pixels[(y + row) * screen->m_width + x + col] = pixel;
                }
        }
        const int top = y + (m_height - m_font->m_height) / 2 + pressed;
        const int dark = goldButton::MatchGold(13, 8, 2, true);
        const int light = goldButton::MatchGold(63, 55, 25, true);
        m_font->DrawBoundedString(m_text, x + 4, top + 1, m_width - 8, m_font->m_height, dark, FONT_ALIGN_CENTER);
        m_font->DrawBoundedString(m_text, x + 3, top, m_width - 8, m_font->m_height, light, FONT_ALIGN_CENTER);
    }

private:
    void LoadFace(icon& source, int index, std::vector<u8>& face) {
        const auto& frame = source.m_frames[index];
        constexpr int edge = 5;
        if (frame.w <= 2 * edge || frame.h <= 2 * edge)
            return;
        bitmap sprite(BITMAP_TYPE_MEMORY, frame.w, frame.h);
        std::memset(sprite.m_pixels, 0, frame.w * frame.h);
        IconToBitmap(&source, &sprite, -frame.x, -frame.y, index, ICON_DRAW_OFFSET_FULL);
        face.resize(m_width * m_height);
        for (int y = 0; y < m_height; ++y) {
            const int row = y < edge ? y : y >= m_height - edge ? frame.h - (m_height - y)
                : edge + (y - edge) * (frame.h - 2 * edge) / (m_height - 2 * edge);
            for (int x = 0; x < m_width; ++x) {
                const int col = x < edge ? x : x >= m_width - edge ? frame.w - (m_width - x)
                    : frame.w - edge - 1;
                face[y * m_width + x] = sprite.m_pixels[row * frame.w + col];
            }
        }
    }

    std::vector<u8> m_normal;
    std::vector<u8> m_pressed;
};

#endif
