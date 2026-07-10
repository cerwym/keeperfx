/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ImGuiCheatPanel.cpp
 *     ImGui in-game cheat menu panel.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx/imgui/ImGuiCheatPanel.hpp"

#ifdef KEEPERFX_IMGUI_ENABLED

#include "game_legacy.h"
#include "player_data.h"
#include "player_instances.h"
#include "creature_instances.h"
#include "thing_data.h"
#include "packets.h"
#include "net_game.h"

#include <imgui.h>

#include <atomic>

namespace {

std::atomic<bool> s_visible{false};
std::atomic<int> s_section{1}; // 1=Main, 2=Creature, 3=Instance, 4=Secondary
float s_ui_scale = 1.0f;
ImVec4 s_window_bg = ImVec4(0.10f, 0.10f, 0.10f, 0.92f);
ImVec4 s_text_color = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
bool s_auto_resize = true;

struct PlayerStateAction {
    const char* label;
    int state;
    int arg2;
    bool single_player_only;
};

static const PlayerStateAction kMainActions[] = {
    {"Null mode",              PSt_None,              0, false},
    {"Place digger mode",      PSt_MkDigger,          0, false},
    {"Place creature mode",    PSt_MkBadCreatr,       0, false},
    {"Place hero mode",        PSt_MkGoodCreatr,      4, false},
    {"Destroy walls mode",     PSt_FreeDestroyWalls,  0, false},
    {"Disease mode",           PSt_FreeCastDisease,   0, false},
    {"Peter mode",             PSt_FreeTurnChicken,   0, false},
    {"Create gold mode",       PSt_MkGoldPot,         0, false},
    {"Steal room mode",        PSt_StealRoom,         0, false},
    {"Destroy room mode",      PSt_DestroyRoom,       0, false},
    {"Steal slab mode",        PSt_StealSlab,         0, false},
    {"Place terrain mode",     PSt_PlaceTerrain,      0, false},
    {"Passenger control mode", PSt_FreeCtrlPassngr,   0, false},
    {"Direct control mode",    PSt_FreeCtrlDirect,    0, false},
    {"Order creature mode",    PSt_OrderCreatr,       0, false},
    {"Kill creature mode",     PSt_KillCreatr,        0, false},
    {"Destroy thing mode",     PSt_DestroyThing,      0, false},
    {"Turncoat mode",          PSt_ConvertCreatr,     0, false},
    {"Level up mode",          PSt_LevelCreatureUp,   0, false},
    {"Level down mode",        PSt_LevelCreatureDown, 0, false},
    {"Query mode",             PSt_QueryAll,          0, false},
    {"Make happy mode",        PSt_MkHappy,           0, false},
    {"Make angry mode",        PSt_MkAngry,           0, false},
    {"Kill player mode",       PSt_KillPlayer,        0, false},
    {"Edit heart health",      PSt_HeartHealth,       0, true},
};

struct CreatureSpellAction {
    const char* label;
    int spell_id;
};

static const CreatureSpellAction kCreatureBuffs[] = {
    {"Speed", 11}, {"Armour", 4}, {"Rebound", 6}, {"Invisibility", 9},
    {"Flight", 20}, {"Sight", 21}, {"Heal", 7}, {"Cleanse", 32}, {"Illumination", 19},
};

static const CreatureSpellAction kCreatureDebuffs[] = {
    {"Slow", 12}, {"Freeze", 3}, {"Chicken", 27}, {"Disease", 26},
};

struct InstanceAction {
    const char* label;
    int instance_id;
};

static const InstanceAction kInstanceActions[] = {
    {"Fireball", CrInst_FIREBALL}, {"Meteor", CrInst_FIRE_BOMB},
    {"Freeze", CrInst_FREEZE}, {"Armour", CrInst_ARMOUR},
    {"Lightning", CrInst_LIGHTNING}, {"Rebound", CrInst_REBOUND},
    {"Heal", CrInst_HEAL}, {"Poison Cloud", CrInst_POISON_CLOUD},
    {"Invisibility", CrInst_INVISIBILITY}, {"Teleport", CrInst_TELEPORT},
    {"Speed", CrInst_SPEED}, {"Slow", CrInst_SLOW},
    {"Drain", CrInst_DRAIN}, {"Fear", CrInst_FEAR},
    {"Missile", CrInst_MISSILE}, {"Homer", CrInst_NAVIGATING_MISSILE},
    {"Breath", CrInst_FLAME_BREATH}, {"Wind", CrInst_WIND},
    {"Light", CrInst_LIGHT}, {"Fly", CrInst_FLY},
    {"Sight", CrInst_SIGHT}, {"Grenade", CrInst_GRENADE},
    {"Hail", CrInst_HAILSTORM}, {"Word of Power", CrInst_WORD_OF_POWER},
    {"Fart", CrInst_FART}, {"Dig", CrInst_FIRST_PERSON_DIG},
    {"Arrow", CrInst_FIRE_ARROW}, {"Lizard", CrInst_LIZARD},
    {"Disease", CrInst_CAST_SPELL_DISEASE}, {"Chicken", CrInst_CAST_SPELL_CHICKEN},
    {"Time Bomb", CrInst_CAST_SPELL_TIME_BOMB}, {"Cleanse", CrInst_CLEANSE},
    {"Ranged Cleanse", CrInst_RANGED_CLEANSE}, {"Ranged Heal", CrInst_RANGED_HEAL},
};

struct Thing* GetControlledCreature(struct PlayerInfo* player)
{
    if (player == nullptr) {
        return nullptr;
    }
    struct Thing* thing = thing_get(player->controlled_thing_idx);
    if (!thing_is_creature(thing)) {
        return nullptr;
    }
    return thing;
}

bool IsCheatPanelAllowedNow()
{
    struct PlayerInfo* player = get_my_player();
    if (player == nullptr) {
        return false;
    }
    return player_exists(player);
}

bool CanLevelUp(struct PlayerInfo* player)
{
    struct Thing* creature = GetControlledCreature(player);
    if (creature == nullptr) {
        return false;
    }
    struct CreatureControl* cctrl = creature_control_get_from_thing(creature);
    return cctrl->exp_level < CREATURE_MAX_LEVEL - 1;
}

bool CanLevelDown(struct PlayerInfo* player)
{
    struct Thing* creature = GetControlledCreature(player);
    if (creature == nullptr) {
        return false;
    }
    struct CreatureControl* cctrl = creature_control_get_from_thing(creature);
    return cctrl->exp_level > 0;
}

void DrawMainSection(struct PlayerInfo* player)
{
    for (const PlayerStateAction& action : kMainActions)
    {
        const bool enabled = !action.single_player_only || !network_is_active();
        if (!enabled) {
            ImGui::BeginDisabled();
        }

        const bool selected = (player->work_state == action.state);
        if (ImGui::Selectable(action.label, selected))
        {
            set_players_packet_action(player, PckA_SetPlyrState, action.state, action.arg2, 0, 0);
        }
        if (!enabled) {
            ImGui::EndDisabled();
        }
    }
}

void DrawCreatureSection(struct PlayerInfo* player)
{
    bool can_level_up = CanLevelUp(player);
    bool can_level_down = CanLevelDown(player);
    bool is_creature = (GetControlledCreature(player) != nullptr);

    ImGui::BeginDisabled(!can_level_up);
    if (ImGui::Button("Level up")) {
        set_players_packet_action(player, PckA_CheatLevelUp, 0, 0, 0, 0);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!can_level_down);
    if (ImGui::Button("Level down")) {
        set_players_packet_action(player, PckA_CheatLevelDown, 0, 0, 0, 0);
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Buff spells");
    ImGui::BeginDisabled(!is_creature);
    for (const CreatureSpellAction& action : kCreatureBuffs)
    {
        if (ImGui::Button(action.label)) {
            set_players_packet_action(player, PckA_CheatApplySpell, action.spell_id, 0, 0, 0);
        }
    }

    ImGui::SeparatorText("Debuff spells");
    for (const CreatureSpellAction& action : kCreatureDebuffs)
    {
        if (ImGui::Button(action.label)) {
            set_players_packet_action(player, PckA_CheatApplySpell, action.spell_id, 0, 0, 0);
        }
    }
    if (ImGui::Button("Kill")) {
        set_players_packet_action(player, PckA_CheatKillCreature, 0, 0, 0, 0);
    }
    ImGui::EndDisabled();
}

void DrawInstanceSection(struct PlayerInfo* player)
{
    struct Thing* creature = GetControlledCreature(player);
    const bool has_creature = (creature != nullptr);
    int active_instance = -1;
    if (has_creature)
    {
        struct CreatureControl* cctrl = creature_control_get_from_thing(creature);
        active_instance = cctrl->active_instance_id;
    }

    ImGui::BeginDisabled(!has_creature);
    for (const InstanceAction& action : kInstanceActions)
    {
        const bool selected = (active_instance == action.instance_id);
        if (ImGui::Selectable(action.label, selected)) {
            set_players_packet_action(player, PckA_CheatCtrlCrtrSetInstnc, action.instance_id, 0, 0, 0);
        }
    }
    ImGui::EndDisabled();
}

void DrawSecondarySection(struct PlayerInfo* player)
{
    if (ImGui::Button("Everything is free")) {
        set_players_packet_action(player, PckA_CheatAllFree, 0, 0, 0, 0);
    }
    if (ImGui::Button("Explore everywhere")) {
        set_players_packet_action(player, PckA_CheatRevealMap, 0, 0, 0, 0);
    }
    if (ImGui::Button("All rooms and magic researchable")) {
        set_players_packet_action(player, PckA_CheatAllResrchbl, 0, 0, 0, 0);
    }
    if (ImGui::Button("Research all magic")) {
        set_players_packet_action(player, PckA_CheatAllMagic, 0, 0, 0, 0);
    }
    if (ImGui::Button("Research all rooms")) {
        set_players_packet_action(player, PckA_CheatAllRooms, 0, 0, 0, 0);
    }
    if (ImGui::Button("All doors manufacturable")) {
        set_players_packet_action(player, PckA_CheatAllDoors, 0, 0, 0, 0);
    }
    if (ImGui::Button("All traps manufacturable")) {
        set_players_packet_action(player, PckA_CheatAllTraps, 0, 0, 0, 0);
    }
    if (ImGui::Button("Increment doors and traps count")) {
        set_players_packet_action(player, PckA_CheatGiveDoorTrap, 0, 0, 0, 0);
    }
    if (ImGui::Button("Win level")) {
        set_players_packet_action(player, PckA_CheatWinLevel, 0, 0, 0, 0);
    }
    if (ImGui::Button("Lose level")) {
        set_players_packet_action(player, PckA_CheatLoseLevel, 0, 0, 0, 0);
    }
}

} // namespace

extern "C" void ImGuiCheatPanel_Draw(void)
{
    if (!s_visible.load()) {
        return;
    }
    if (!IsCheatPanelAllowedNow()) {
        s_visible.store(false);
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_WindowBg, s_window_bg);
    ImGui::PushStyleColor(ImGuiCol_Text, s_text_color);
    bool open = true;
    ImGuiWindowFlags window_flags = s_auto_resize ? ImGuiWindowFlags_AlwaysAutoResize : 0;
    ImGui::Begin("Cheat Menu", &open, window_flags);
    ImGui::SetWindowFontScale(s_ui_scale);

    if (ImGui::CollapsingHeader("Panel style", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Font scale", &s_ui_scale, 0.70f, 2.20f, "%.2fx");
        ImGui::Checkbox("Auto-size window to contents", &s_auto_resize);
        ImGui::ColorEdit4("Window background", (float*)&s_window_bg);
        ImGui::ColorEdit4("Text color", (float*)&s_text_color);
        ImGui::Separator();
    }

    if (!game.easter_eggs_enabled)
    {
        ImGui::TextUnformatted("Cheat mode is disabled.");
        ImGui::TextUnformatted("Enable easter eggs / cheat mode to use this panel.");
        ImGui::End();
        ImGui::PopStyleColor(2);
        if (!open) {
            s_visible.store(false);
        }
        return;
    }

    struct PlayerInfo* player = get_my_player();
    if (player == nullptr)
    {
        ImGui::TextUnformatted("No local player is available.");
        ImGui::End();
        ImGui::PopStyleColor(2);
        if (!open) {
            s_visible.store(false);
        }
        return;
    }

    const char* sections[] = {"Main", "Creature", "Instance", "Secondary"};
    int section = s_section.load();
    if (section < 1 || section > 4) {
        section = 1;
    }
    int section_idx = section - 1;
    if (ImGui::Combo("Section", &section_idx, sections, IM_ARRAYSIZE(sections))) {
        s_section.store(section_idx + 1);
    }

    ImGui::Separator();
    switch (s_section.load())
    {
    case 1: DrawMainSection(player); break;
    case 2: DrawCreatureSection(player); break;
    case 3: DrawInstanceSection(player); break;
    case 4: DrawSecondarySection(player); break;
    default: break;
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    if (!open) {
        s_visible.store(false);
    }
}

extern "C" void ImGuiCheatPanel_SetVisible(int visible)
{
    if ((visible != 0) && !IsCheatPanelAllowedNow()) {
        s_visible.store(false);
        return;
    }
    s_visible.store(visible != 0);
}

extern "C" void ImGuiCheatPanel_ToggleVisible(void)
{
    const bool current = s_visible.load();
    if (current) {
        s_visible.store(false);
        return;
    }
    if (IsCheatPanelAllowedNow()) {
        s_visible.store(true);
    }
}

extern "C" int ImGuiCheatPanel_IsVisible(void)
{
    return s_visible.load() ? 1 : 0;
}

extern "C" void ImGuiCheatPanel_SetSection(int section)
{
    if (section >= 1 && section <= 4) {
        s_section.store(section);
    }
}

#endif

#include "post_inc.h"
