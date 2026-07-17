#pragma once

#include <e32std.h>

// Virtual key codes for reSF2 on Symbian
// Maps N8 keyboard + touch events to game actions
enum TGameKey {
    EGameKey_Up        = 0,
    EGameKey_Down      = 1,
    EGameKey_Left      = 2,
    EGameKey_Right     = 3,
    EGameKey_Punch     = 4,  // O
    EGameKey_Kick      = 5,  // P
    EGameKey_Menu      = 6,  // M
    EGameKey_Dialog    = 7,  // T
    EGameKey_Esc       = 8,  // Escape
    EGameKey_Count     = 9
};

class TouchInput {
public:
    TouchInput();
    ~TouchInput();

    // Process touch events (called from CCoeControl::HandlePointerEventL)
    void HandleTouchDown(TInt aX, TInt aY);
    void HandleTouchMove(TInt aX, TInt aY);
    void HandleTouchUp(TInt aX, TInt aY);

    // Map touch to game keys
    TBool IsGameKeyDown(TGameKey aKey) const;
    void ClearFrame();

    // Touch zones for virtual buttons
    enum TZone {
        EZone_None,
        EZone_MoveLeft,
        EZone_MoveRight,
        EZone_Jump,
        EZone_Crouch,
        EZone_Punch,
        EZone_Kick,
        EZone_Special
    };

private:
    TBool iTouchDown;
    TInt iTouchX;
    TInt iTouchY;
    TInt iTouchStartX;
    TInt iTouchStartY;
    TGameKey iKeys[EGameKey_Count];

    TZone DetectZone(TInt aX, TInt aY) const;
    TBool DetectSwipe(TInt aStartX, TInt aStartY, TInt aEndX, TInt aEndY,
                       TInt& aDirX, TInt& aDirY) const;
};
