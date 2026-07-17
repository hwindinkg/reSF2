#include "input_symbian.h"
#include <e32math.h>

TouchInput::TouchInput()
    : iTouchDown(EFalse)
    , iTouchX(0)
    , iTouchY(0)
    , iTouchStartX(0)
    , iTouchStartY(0)
{
    Mem::FillZ(iKeys, sizeof(iKeys));
}

TouchInput::~TouchInput() {}

void TouchInput::HandleTouchDown(TInt aX, TInt aY) {
    iTouchDown = ETrue;
    iTouchX = aX;
    iTouchY = aY;
    iTouchStartX = aX;
    iTouchStartY = aY;

    TZone zone = DetectZone(aX, aY);
    switch (zone) {
        case EZone_Punch: iKeys[EGameKey_Punch] = ETrue; break;
        case EZone_Kick:  iKeys[EGameKey_Kick] = ETrue; break;
        default: break;
    }
}

void TouchInput::HandleTouchMove(TInt aX, TInt aY) {
    iTouchX = aX;
    iTouchY = aY;
}

void TouchInput::HandleTouchUp(TInt aX, TInt aY) {
    // Detect swipe gestures
    TInt dirX, dirY;
    if (DetectSwipe(iTouchStartX, iTouchStartY, aX, aY, dirX, dirY)) {
        if (dirY < -30) iKeys[EGameKey_Up] = ETrue;
        else if (dirY > 30) iKeys[EGameKey_Down] = ETrue;
        if (dirX < -30) iKeys[EGameKey_Left] = ETrue;
        else if (dirX > 30) iKeys[EGameKey_Right] = ETrue;
    }

    iTouchDown = EFalse;
    iTouchX = aX;
    iTouchY = aY;
}

TBool TouchInput::IsGameKeyDown(TGameKey aKey) const {
    if (aKey < 0 || aKey >= EGameKey_Count) return EFalse;
    return iKeys[aKey];
}

void TouchInput::ClearFrame() {
    Mem::FillZ(iKeys, sizeof(iKeys));
}

TouchInput::TZone TouchInput::DetectZone(TInt aX, TInt aY) const {
    // Screen zones for Nokia N8 (640x360)
    // Bottom-left: move controls
    // Bottom-right: action buttons
    // Top-left: menu
    // Top-right: special

    if (aY < 50) return EZone_None; // status bar area

    // Left half: movement
    if (aX < 320) {
        if (aY > 260) return EZone_Punch;  // bottom-left = punch
        return EZone_MoveLeft;
    }
    // Right half: actions
    if (aX >= 320) {
        if (aY > 260) return EZone_Kick;   // bottom-right = kick
        return EZone_MoveRight;
    }
    return EZone_None;
}

TBool TouchInput::DetectSwipe(TInt aStartX, TInt aStartY, TInt aEndX, TInt aEndY,
                               TInt& aDirX, TInt& aDirY) const {
    aDirX = aEndX - aStartX;
    aDirY = aEndY - aStartY;
    TInt dist = (TInt)sqrt((TReal)(aDirX*aDirX + aDirY*aDirY));
    return dist > 30; // minimum swipe distance
}
