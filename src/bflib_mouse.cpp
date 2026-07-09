/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_mouse.cpp
 *     Mouse related routines.
 * @par Purpose:
 *     Pointer position, movement and cursor support.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     12 Feb 2008 - 26 Oct 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "bflib_mouse.h"

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <SDL3/SDL.h>

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_video.h"
#include "bflib_sprite.h"
#include "bflib_vidraw.h"
#include "bflib_mshandler.hpp"
#include "bflib_inputctrl.h"
#include "platform/PlatformManager.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

volatile TbBool lbMouseGrab = true;
volatile TbBool lbMouseGrabbed = true;
/** Mouse sensitivity ratio in 8.8 fixed point. */
short lbMouseMoveRatio;
volatile struct MouseWheelState lbMouseWheel;
/******************************************************************************/
TbResult LbMouseChangeSpriteAndHotspot(const struct TbSprite *pointerSprite, long hot_x, long hot_y)
{
#if (BFDEBUG_LEVEL > 18)
  if (pointerSprite == NULL)
    SYNCLOG("Setting to %s","NONE");
  else
    SYNCLOG("Setting to %dx%d, data at %p",(int)pointerSprite->SWidth,(int)pointerSprite->SHeight,pointerSprite);
#endif
  if (!lbMouseInstalled)
    return Lb_FAIL;
  if (!pointerHandler.SetMousePointerAndOffset(pointerSprite, hot_x, hot_y))
    return Lb_FAIL;
  return Lb_SUCCESS;
}

TbResult LbMouseSetup(struct TbSprite *pointerSprite)
{
  TbResult ret;
  if (lbMouseInstalled)
    LbMouseSuspend();
  pointerHandler.Install();
  lbMouseOffline = true;
  lbMouseInstalled = true;
  LbMouseSetWindow(0,0,LbGraphicsScreenWidth(),LbGraphicsScreenHeight());
  LbGrabMouseCheck(MG_InitMouse);
  ret = Lb_SUCCESS;
  if (LbMouseChangeSprite(pointerSprite) != Lb_SUCCESS)
    ret = Lb_FAIL;
  lbMouseInstalled = (ret == Lb_SUCCESS);
  lbMouseOffline = false;
  LbGrabMouseCheck(MG_OnFocusGained);
  return ret;
}

TbResult LbMouseSetPointerHotspot(long hot_x, long hot_y)
{
  if (!lbMouseInstalled)
    return Lb_FAIL;
  if (!pointerHandler.SetPointerOffset(hot_x, hot_y))
    return Lb_FAIL;
  return Lb_SUCCESS;
}

TbResult LbMouseSetPositionInitial(long x, long y)
{
  if (!lbMouseInstalled)
    return Lb_FAIL;
  if (!pointerHandler.SetMousePosition(x, y))
    return Lb_FAIL;
  return Lb_SUCCESS;
}

TbResult LbMouseSetPosition(long x, long y)
{
  if (!lbMouseInstalled) {
    return Lb_FAIL;
  }

  if (!pointerHandler.SetMousePosition(x,y)) {
    return Lb_FAIL;
  }

  PlatformManager_WarpCursor(x,y);
  return Lb_SUCCESS;
}

void LbMoveHostCursorToGameCursor(void)
{
    int game_cursor_x = lbMouse.MMouseX;
    int game_cursor_y = lbMouse.MMouseY;
    float hcx_f = 0.0f, hcy_f = 0.0f;
    SDL_GetMouseState(&hcx_f, &hcy_f);
    int host_cursor_x = (int)hcx_f, host_cursor_y = (int)hcy_f;
    if ((host_cursor_x != game_cursor_x) || (host_cursor_y != game_cursor_y))
    {
        LbMouseSetPosition(game_cursor_x, game_cursor_y);
    }
}

TbResult LbMoveGameCursorToHostCursor(void)
{
    int game_cursor_x = lbMouse.MMouseX;
    int game_cursor_y = lbMouse.MMouseY;
    float hcx_f = 0.0f, hcy_f = 0.0f;
    SDL_GetMouseState(&hcx_f, &hcy_f);
    int host_cursor_x = (int)hcx_f, host_cursor_y = (int)hcy_f;
    if (((host_cursor_x != game_cursor_x) || (host_cursor_y != game_cursor_y)) &&
        PlatformManager::Get()->GetWindowSystem()->IsAppActive())
    {
        if (!pointerHandler.SetMousePosition(host_cursor_x, host_cursor_y))
        {
            return Lb_FAIL;
        }
    }
    return Lb_SUCCESS;
}

TbBool IsMouseInsideWindow(void)
{
    SDL_Window *window = SDL_GetMouseFocus();
    TbBool isMouseInsideWindow = ((window != NULL) ? true : false); // if window == NULL then the mouse must be outside the kfx window
    if (!LbIsMouseActive() && !lbMouseGrabbed)
    {
        isMouseInsideWindow = false; // LbIsMouseActive() == false when mouse cursor outside window
    }
    return isMouseInsideWindow;
}

TbResult LbMouseChangeSprite(const struct TbSprite *pointerSprite)
{
#if (BFDEBUG_LEVEL > 18)
  if (pointerSprite == NULL)
    SYNCLOG("Setting to %s","NONE");
  else
    SYNCLOG("Setting to %dx%d, data at %p",(int)pointerSprite->SWidth,(int)pointerSprite->SHeight,pointerSprite);
#endif
  if (!lbMouseInstalled)
    return Lb_FAIL;
  if (!pointerHandler.SetMousePointer(pointerSprite))
    return Lb_FAIL;
  return Lb_SUCCESS;
}

void GetPointerHotspot(int32_t *hot_x, int32_t *hot_y)
{
  struct TbPoint *hotspot;
  hotspot = pointerHandler.GetPointerOffset();
  if (hotspot == NULL)
    return;
  *hot_x = hotspot->x;
  *hot_y = hotspot->y;
}

TbResult LbMouseIsInstalled(void)
{
  if (!lbMouseInstalled)
    return Lb_FAIL;
  if (!pointerHandler.IsInstalled())
    return Lb_FAIL;
  return Lb_SUCCESS;
}

TbResult LbMouseSetWindow(long x, long y, long width, long height)
{
  if (!lbMouseInstalled)
    return Lb_FAIL;
  if (!pointerHandler.SetMouseWindow(x, y, width, height))
    return Lb_FAIL;
  return Lb_SUCCESS;
}

TbResult LbMouseOnMove(struct TbPoint shift)
{
  if ((!lbMouseInstalled) || (lbMouseOffline))
    return Lb_FAIL;
  if (!pointerHandler.SetMousePosition(lbMouse.MMouseX+shift.x, lbMouse.MMouseY+shift.y))
    return Lb_FAIL;
  return Lb_SUCCESS;
}

TbResult LbMouseSuspend(void)
{
  if (!lbMouseInstalled)
    return Lb_FAIL;
  if (!pointerHandler.Release())
    return Lb_FAIL;
  return Lb_SUCCESS;
}

TbResult LbMouseOnBeginSwap(void)
{
    if (!pointerHandler.PointerBeginSwap())
        return Lb_FAIL;
    return Lb_SUCCESS;
}

TbResult LbMouseOnEndSwap(void)
{
    if (!pointerHandler.PointerEndSwap())
        return Lb_FAIL;
    return Lb_SUCCESS;
}

void mouseControl(unsigned int action, struct TbPoint *pos)
{
    struct TbPoint dstPos;
    dstPos.x = pos->x;
    dstPos.y = pos->y;
    switch ( action )
    {
    case MActn_MOUSEMOVE:
        LbMouseOnMove(dstPos);
        break;
    case MActn_LBUTTONDOWN:
        lbMouse.MLeftButton = 1;
        if ( !lbMouse.LeftButton )
        {
            LbMouseOnMove(dstPos);
            lbMouse.MouseX = lbMouse.MMouseX;
            lbMouse.MouseY = lbMouse.MMouseY;
            lbMouse.RLeftButton = 0;
            lbMouse.LeftButton = 1;
        }
        break;
    case MActn_LBUTTONUP:
        lbMouse.MLeftButton = 0;
        if ( !lbMouse.RLeftButton )
        {
            LbMouseOnMove(dstPos);
            lbMouse.RMouseX = lbMouse.MMouseX;
            lbMouse.RMouseY = lbMouse.MMouseY;
            lbMouse.RLeftButton = 1;
        }
        break;
    case MActn_RBUTTONDOWN:
        lbMouse.MRightButton = 1;
        if ( !lbMouse.RightButton )
        {
            LbMouseOnMove(dstPos);
            lbMouse.MouseX = lbMouse.MMouseX;
            lbMouse.MouseY = lbMouse.MMouseY;
            lbMouse.RRightButton = 0;
            lbMouse.RightButton = 1;
        }
        break;
    case MActn_RBUTTONUP:
        lbMouse.MRightButton = 0;
        if ( !lbMouse.RRightButton )
        {
            LbMouseOnMove(dstPos);
            lbMouse.RMouseX = lbMouse.MMouseX;
            lbMouse.RMouseY = lbMouse.MMouseY;
            lbMouse.RRightButton = 1;
        }
        break;
    case MActn_MBUTTONDOWN:
        lbMouse.MMiddleButton = 1;
        if ( !lbMouse.MiddleButton )
        {
            LbMouseOnMove(dstPos);
            lbMouse.MouseX = lbMouse.MMouseX;
            lbMouse.MouseY = lbMouse.MMouseY;
            lbMouse.MiddleButton = 1;
            lbMouse.RMiddleButton = 0;
        }
        break;
    case MActn_MBUTTONUP:
        lbMouse.MMiddleButton = 0;
        if ( !lbMouse.RMiddleButton )
        {
            LbMouseOnMove(dstPos);
            lbMouse.RMouseX = lbMouse.MMouseX;
            lbMouse.RMouseY = lbMouse.MMouseY;
            lbMouse.RMiddleButton = 1;
            lbMouse.MiddleButton = 0; // lbMouse.MiddleButton is not handled as well as lbMouse.LeftButton and lbMouse.RightButton, so reset it here
        }
        break;
    case MActn_WHEELMOVEUP:
        lbMouseWheel.WheelPosition = lbMouseWheel.WheelPosition - 1;
        lbMouseWheel.WheelMoveUp = lbMouseWheel.WheelMoveUp + 1;
        lbMouseWheel.WheelMoveDown = 0;
        break;
    case MActn_WHEELMOVEDOWN:
        lbMouseWheel.WheelPosition = lbMouseWheel.WheelPosition + 1;
        lbMouseWheel.WheelMoveUp = 0;
        lbMouseWheel.WheelMoveDown = lbMouseWheel.WheelMoveDown + 1;
        break;
    default:
        break;
    }
}

/**
 * Changes mouse movement ratio.
 * Note that this function can be run even before mouse setup. Still, the factor
 *  will be reset during the installation - so use it after LbMouseSetup().
 *
 * @param ratio_x Movement ratio in X direction; 256 means unchanged ratio from OS.
 * @param ratio_y Movement ratio in Y direction; 256 means unchanged ratio from OS.
 * @return Lb_SUCCESS if the ratio values were of correct range and have been set.
 */
TbResult LbMouseChangeMoveRatio(long ratio_x, long ratio_y)
{
    if ((ratio_x < -8192) || (ratio_x > 8192) || (ratio_x == 0))
        return Lb_FAIL;
    if ((ratio_y < -8192) || (ratio_y > 8192) || (ratio_y == 0))
        return Lb_FAIL;
    SYNCLOG("New ratio %ldx%ld",ratio_x, ratio_y);
    // Currently we don't have two ratio factors, so let's store an average
    lbMouseMoveRatio = (ratio_x + ratio_y)/2;
    //TODO INPUT Separate mouse ratios in X and Y direction when lbDisplay from DLL will no longer be used.
    //minfo.XMoveRatio = ratio_x;
    //minfo.YMoveRatio = ratio_y;
    return Lb_SUCCESS;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
