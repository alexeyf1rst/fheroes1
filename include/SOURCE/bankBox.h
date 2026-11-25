#ifndef HOMM1_SOURCE_BANKBOX_H
#define HOMM1_SOURCE_BANKBOX_H

class playerData;
class heroWindow;

class bankBox {
public:
    heroWindow* m_window;
    bankBox(i16 x, i16 y, class playerData* player);
    ~bankBox();
    void Update(void);
};
#endif
