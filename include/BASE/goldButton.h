#ifndef HOMM1_BASE_GOLDBUTTON_H
#define HOMM1_BASE_GOLDBUTTON_H

#include <BASE/bmap2.h>
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

#endif
