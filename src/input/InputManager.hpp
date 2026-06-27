/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file InputManager.hpp
 *     Modern C++ input abstraction layer for context-aware input handling.
 * @par Purpose:
 *     Provides a clean abstraction layer over raw input (keyboard/mouse/gamepad)
 *     with support for multiple input contexts (game, UI, debug overlay).
 * @par Comment:
 *     Allows input to be routed to different systems based on context,
 *     solving issues like ImGui overlay blocking game input.
 * @author   Community
 * @date     18 Jan 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include "../bflib_basics.h"
#include "../bflib_keybrd.h"
#include "../globals.h"

#ifdef __cplusplus
#include <array>
#include <stack>
#include <cstring>

/******************************************************************************/
/** Input Manager - Modern C++ singleton for context-aware input routing */
class InputManager {
public:
    /** Input context enumeration */
    enum class Context {
        None = 0,       /**< No input processing */
        Game,           /**< Normal gameplay input */
        UI,             /**< Frontend/menu input */
        Debug,          /**< Debug overlay (ImGui) input */
        Blocked,        /**< Input blocked (e.g., during cinematics) */
    };

    /** Input state snapshot for a particular frame/context */
    struct State {
        std::array<uint8_t, KC_LIST_END> keys{};  /**< Key press states */
        TbKeyCode lastKey = KC_UNASSIGNED;        /**< Last key pressed */

        long mouseX = 0;                          /**< Mouse X position */
        long mouseY = 0;                          /**< Mouse Y position */
        bool leftButton = false;                 /**< Left mouse button pressed */
        bool rightButton = false;                /**< Right mouse button pressed */
        bool middleButton = false;               /**< Middle mouse button pressed */
        bool wheelUp = false;                    /**< Mouse wheel scrolled up */
        bool wheelDown = false;                  /**< Mouse wheel scrolled down */

        bool leftButtonClicked = false;          /**< Left button just pressed */
        bool rightButtonClicked = false;         /**< Right button just pressed */
        bool leftButtonReleased = false;         /**< Left button just released */
        bool rightButtonReleased = false;        /**< Right button just released */

        void clear()
        {
            keys.fill(0);
            lastKey = KC_UNASSIGNED;
            mouseX = mouseY = 0;
            leftButton = rightButton = middleButton = false;
            wheelUp = wheelDown = false;
            leftButtonClicked = rightButtonClicked = false;
            leftButtonReleased = rightButtonReleased = false;
        }

        void clearPerFrameState()
        {
            wheelUp = wheelDown = false;
            leftButtonClicked = rightButtonClicked = false;
            leftButtonReleased = rightButtonReleased = false;
        }
    };

private:
    InputManager();
    ~InputManager() = default;

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;
    InputManager(InputManager&&) = delete;
    InputManager& operator=(InputManager&&) = delete;

    Context activeContext_ = Context::Game;
    std::stack<Context> contextStack_;

    State gameInput_;
    State uiInput_;
    State debugInput_;
    State rawInput_;

    bool initialized_ = false;

    State* getContextState(Context context);
    const State* getContextState(Context context) const;

public:
    static InputManager& instance()
    {
        static InputManager instance;
        return instance;
    }

    void initialize();
    void shutdown();
    void newFrame();

    bool isInitialized() const { return initialized_; }

    void pushContext(Context context);
    void popContext();
    void setContext(Context context);

    Context getActiveContext() const { return activeContext_; }

    bool isKeyPressed(TbKeyCode key) const;
    TbKeyCode getLastKey() const;
    void clearLastKey();
    long getMouseX() const;
    long getMouseY() const;
    bool isLeftButtonPressed() const;
    bool isRightButtonPressed() const;
    bool isMiddleButtonPressed() const;
    bool isWheelUp() const;
    bool isWheelDown() const;
    bool isLeftButtonClicked() const;
    bool isRightButtonClicked() const;

    void updateKey(TbKeyCode key, bool pressed);
    void updateMousePosition(long x, long y);
    void updateMouseButton(int button, bool pressed);
    void updateMouseWheel(bool up);

    State* getActiveState();
    const State* getActiveState() const;

    State* getRawState() { return &rawInput_; }
    const State* getRawState() const { return &rawInput_; }
};

#endif // __cplusplus

/******************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif
    void InputManager_Initialize(void);
    void InputManager_Shutdown(void);
    void InputManager_NewFrame(void);

    void InputManager_PushContext(int context);
    void InputManager_PopContext(void);
    void InputManager_SetContext(int context);
    int InputManager_GetActiveContext(void);

    TbBool InputManager_IsKeyPressed(TbKeyCode key);
    TbKeyCode InputManager_GetLastKey(void);
    void InputManager_ClearLastKey(void);
    long InputManager_GetMouseX(void);
    long InputManager_GetMouseY(void);
    TbBool InputManager_IsLeftButtonPressed(void);
    TbBool InputManager_IsRightButtonPressed(void);
    TbBool InputManager_IsMiddleButtonPressed(void);
    TbBool InputManager_IsWheelUp(void);
    TbBool InputManager_IsWheelDown(void);
    TbBool InputManager_IsLeftButtonClicked(void);
    TbBool InputManager_IsRightButtonClicked(void);

    void InputManager_UpdateKey(TbKeyCode key, TbBool pressed);
    void InputManager_UpdateMousePosition(long x, long y);
    void InputManager_UpdateMouseButton(int button, TbBool pressed);
    void InputManager_UpdateMouseWheel(TbBool up);

    TbBool input_key_pressed(TbKeyCode key);
    TbKeyCode input_last_key(void);
    void input_clear_last_key(void);

#ifdef __cplusplus
}
#endif

#endif
