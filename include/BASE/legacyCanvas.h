#ifndef HOMM1_BASE_LEGACYCANVAS_H
#define HOMM1_BASE_LEGACYCANVAS_H

// Temporarily draws a complete legacy screen at its authored 640 x 480 size.
// Nested scopes share the outer canvas; the native screen buffer is preserved.
class LegacyCanvas {
public:
    LegacyCanvas();
    ~LegacyCanvas();
    LegacyCanvas(const LegacyCanvas&) = delete;
    LegacyCanvas& operator=(const LegacyCanvas&) = delete;

    // End the scope before redrawing the caller's scene. Safe to call twice.
    void Restore();

private:
    struct State;
    State* m_state = nullptr;
    bool m_entered = false;
};

#endif
