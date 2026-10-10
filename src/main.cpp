// CS2 Mode for GTA V Enhanced - native Script Hook V plugin (v0.2, UNTESTED)
// Written against the Script Hook V SDK (inc/main.h, natives.h, types.h).
// If your SDK's natives.h uses newer namespace names (HUD::, MISC::, ...), rename the
// calls marked "old SDK naming" accordingly. No Counter-Strike assets are used.
#include <windows.h>
#include <math.h>
#include <string>
#include <set>
#include "main.h"
#include "natives.h"
#include "types.h"

// ---------------- data (mirrors sheets/weapons.json, equipment.json) ----------------
struct Gun { const char* name; const char* wname; Hash hash; int price; int reward; float dmg; float kick; float sway; int rpm; };
static Gun Guns[] = {
    {"Glock-18",     "WEAPON_COMBATPISTOL", 0, 200,  300, 1.00f, 0.35f, 0.10f, 400},
    {"Desert Eagle", "WEAPON_PISTOL50",     0, 700,  300, 0.95f, 1.10f, 0.15f, 267},
    {"AK-47",        "WEAPON_ASSAULTRIFLE", 0, 2700, 300, 1.20f, 0.55f, 0.30f, 600},
    {"M4A4",         "WEAPON_CARBINERIFLE", 0, 2900, 300, 1.00f, 0.45f, 0.25f, 666},
    {"AWP",          "WEAPON_HEAVYSNIPER",  0, 4750, 100, 0.55f, 1.80f, 0.00f, 41},
    {"MP9",          "WEAPON_MICROSMG",     0, 1250, 600, 1.20f, 0.30f, 0.30f, 857},
    {"Nova",         "WEAPON_PUMPSHOTGUN",  0, 1050, 900, 0.90f, 1.40f, 0.10f, 68},
};
static const int NG = sizeof(Guns) / sizeof(Guns[0]);
struct Gear { const char* name; int price; };
static Gear Gears[] = { {"Kevlar", 650}, {"Kevlar + Helmet", 1000}, {"HE Grenade", 300}, {"Smoke Grenade", 300} };
static const int NGEAR = sizeof(Gears) / sizeof(Gears[0]);
static const int NITEMS = NG + NGEAR;

// ---------------- state ----------------
static bool modOn = true, buyOpen = false, flying = false;
static int money = 5000, cursor = 0, skin = 0, burst = 0;
static ULONGLONG lastShot = 0, noticeUntil = 0;
static char notice[96] = "";
static std::set<int> rewarded;
static bool prevKey[256];

enum Act { A_TOGGLE, A_BUY, A_FLY, A_TP, A_CAR, A_NPC, A_WEAPONS, A_SKIN, A_MONEY, A_COUNT };

// ---------------- helpers ----------------
static void Notice(const char* s) { strncpy_s(notice, s, _TRUNCATE); noticeUntil = GetTickCount64() + 3000; }

static bool KeyHit(int vk) {
    bool d = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool h = d && !prevKey[vk];
    prevKey[vk] = d;
    return h;
}
static bool PadHit(int id) {
    return CONTROLS::IS_CONTROL_JUST_PRESSED(0, id) || CONTROLS::IS_DISABLED_CONTROL_JUST_PRESSED(0, id);
}

static void Rect(float x, float y, float w, float h, int r, int g, int b, int a) {
    GRAPHICS::DRAW_RECT(x, y, w, h, r, g, b, a);
}
static void Txt(const char* s, float x, float y, float scale, int r = 235, int g = 235, int b = 235) {
    UI::SET_TEXT_FONT(0);
    UI::SET_TEXT_SCALE(0.0f, scale);
    UI::SET_TEXT_COLOUR(r, g, b, 255);
    UI::SET_TEXT_OUTLINE();
    UI::_SET_TEXT_ENTRY((char*)"STRING");            // old SDK naming
    UI::_ADD_TEXT_COMPONENT_STRING((char*)s);        // old SDK naming
    UI::_DRAW_TEXT(x, y);                            // old SDK naming
}

static void ApplyDamage(bool on) {
    for (int i = 0; i < NG; i++)
        invoke<Void>(0x4757F00BC6323CFEull, Guns[i].hash, on ? Guns[i].dmg : 1.0f); // SET_WEAPON_DAMAGE_MODIFIER
}

static int CurrentGun(Ped p) {
    Hash w = 0;
    WEAPON::GET_CURRENT_PED_WEAPON(p, &w, TRUE);
    for (int i = 0; i < NG; i++) if (Guns[i].hash == w) return i;
    return -1;
}

static void LoadModel(Hash m) {
    STREAMING::REQUEST_MODEL(m);
    int guard = 0;
    while (!STREAMING::HAS_MODEL_LOADED(m) && guard++ < 200) WAIT(0);
}

// ---------------- actions ----------------
static void Buy(int i, Ped p) {
    int price = i < NG ? Guns[i].price : Gears[i - NG].price;
    const char* name = i < NG ? Guns[i].name : Gears[i - NG].name;
    if (money < price) { Notice("Not enough money"); return; }
    money -= price;
    if (i < NG) WEAPON::GIVE_DELAYED_WEAPON_TO_PED(p, Guns[i].hash, 9999, TRUE);
    else switch (i - NG) {
        case 0: case 1: PED::SET_PED_ARMOUR(p, 100); break;
        case 2: WEAPON::GIVE_DELAYED_WEAPON_TO_PED(p, GAMEPLAY::GET_HASH_KEY((char*)"WEAPON_GRENADE"), 1, FALSE); break;
        case 3: WEAPON::GIVE_DELAYED_WEAPON_TO_PED(p, GAMEPLAY::GET_HASH_KEY((char*)"WEAPON_SMOKEGRENADE"), 1, FALSE); break;
    }
    char buf[96]; sprintf_s(buf, "Bought %s", name); Notice(buf);
}

static void TeleportWaypoint(Ped p) {
    Blip b = UI::GET_FIRST_BLIP_INFO_ID(8);
    if (!UI::DOES_BLIP_EXIST(b)) { Notice("Set a waypoint first"); return; }
    Vector3 c = UI::GET_BLIP_COORDS(b);
    float z = 100.0f;
    for (float h = 1000.0f; h > 0.0f; h -= 100.0f) {
        ENTITY::SET_ENTITY_COORDS_NO_OFFSET(p, c.x, c.y, h, 0, 0, 0);
        WAIT(30);
        float gz;
        if (GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(c.x, c.y, h, &gz, 0)) { z = gz + 1.0f; break; }
    }
    ENTITY::SET_ENTITY_COORDS_NO_OFFSET(p, c.x, c.y, z, 0, 0, 0);
}

static void Spawn(Ped p, bool car) {
    Vector3 pos = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(p, 0.0f, 5.0f, 0.0f);
    float hd = ENTITY::GET_ENTITY_HEADING(p);
    Hash m = GAMEPLAY::GET_HASH_KEY((char*)(car ? "adder" : "a_m_y_business_01"));
    LoadModel(m);
    if (!STREAMING::HAS_MODEL_LOADED(m)) return;
    if (car) VEHICLE::CREATE_VEHICLE(m, pos.x, pos.y, pos.z, hd, FALSE, FALSE);
    else     PED::CREATE_PED(26, m, pos.x, pos.y, pos.z, hd, FALSE, FALSE);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(m);
}

static void SetMode(bool on, Ped p) {
    modOn = on; buyOpen = false;
    if (flying) { flying = false; ENTITY::FREEZE_ENTITY_POSITION(p, FALSE); }
    ApplyDamage(on);
    if (!on) UI::DISPLAY_RADAR(TRUE);
    Notice(on ? "CS2 Mode ON" : "CS2 Mode OFF (GTA restored)");
}

// ---------------- drawing ----------------
static void DrawCrosshair(Ped p) {
    Vector3 v = ENTITY::GET_ENTITY_VELOCITY(p);
    float speed = sqrtf(v.x * v.x + v.y * v.y);
    float gap = 0.004f + fminf(0.02f, speed * 0.003f) + burst * 0.0008f;
    float len = 0.008f, ar = 16.0f / 9.0f;
    Rect(0.5f - gap - len / 2, 0.5f, len, 0.0025f * ar, 80, 255, 80, 220);
    Rect(0.5f + gap + len / 2, 0.5f, len, 0.0025f * ar, 80, 255, 80, 220);
    Rect(0.5f, 0.5f - (gap + len / 2) * ar, 0.0015f, len * ar, 80, 255, 80, 220);
    Rect(0.5f, 0.5f + (gap + len / 2) * ar, 0.0015f, len * ar, 80, 255, 80, 220);
}

static void DrawRadar(Ped p, const int* peds, int n) {
    const float cx = 0.085f, cy = 0.17f, rad = 0.07f, ar = 16.0f / 9.0f, range = 60.0f;
    Rect(cx, cy, 2 * rad / ar, 2 * rad, 10, 10, 10, 150);
    Rect(cx, cy, 0.004f, 0.007f, 80, 160, 255, 255);
    Vector3 me = ENTITY::GET_ENTITY_COORDS(p, TRUE);
    float yaw = CAM::GET_GAMEPLAY_CAM_ROT(2).z * 3.14159265f / 180.0f;
    for (int i = 0; i < n; i++) {
        Ped o = peds[i];
        if (o == p || ENTITY::IS_ENTITY_DEAD(o)) continue;
        Vector3 q = ENTITY::GET_ENTITY_COORDS(o, TRUE);
        float dx = q.x - me.x, dy = q.y - me.y;
        float rx = dx * cosf(yaw) + dy * sinf(yaw);
        float ry = -dx * sinf(yaw) + dy * cosf(yaw);
        if (fabsf(rx) > range || fabsf(ry) > range) continue;
        Rect(cx + rx / range * (rad / ar), cy - ry / range * rad, 0.003f, 0.005f, 255, 80, 80, 255);
    }
}

static void DrawHud(Ped p, const int* peds, int n) {
    int hp = ENTITY::GET_ENTITY_HEALTH(p) - 100, mhp = ENTITY::GET_ENTITY_MAX_HEALTH(p) - 100;
    if (hp < 0) hp = 0; if (mhp < 1) mhp = 1;
    int pct = (int)(100.0f * hp / mhp);
    char b[64];
    Rect(0.08f, 0.94f, 0.14f, 0.07f, 0, 0, 0, 120);
    sprintf_s(b, "+ %d", pct);  Txt(b, 0.02f, 0.915f, 0.8f, pct > 30 ? 235 : 255, pct > 30 ? 235 : 80, pct > 30 ? 235 : 80);
    sprintf_s(b, "A %d", PED::GET_PED_ARMOUR(p)); Txt(b, 0.09f, 0.915f, 0.8f);

    Hash w = 0; int clip = 0;
    WEAPON::GET_CURRENT_PED_WEAPON(p, &w, TRUE);
    WEAPON::GET_AMMO_IN_CLIP(p, w, &clip);
    int total = WEAPON::GET_AMMO_IN_PED_WEAPON(p, w);
    Rect(0.92f, 0.94f, 0.14f, 0.07f, 0, 0, 0, 120);
    sprintf_s(b, "%d / %d", clip, total - clip); Txt(b, 0.87f, 0.915f, 0.8f);

    Rect(0.065f, 0.52f, 0.11f, 0.05f, 0, 0, 0, 120);
    sprintf_s(b, "$%d", money); Txt(b, 0.02f, 0.50f, 0.7f, 160, 230, 120);

    DrawCrosshair(p);
    DrawRadar(p, peds, n);
    if (GetTickCount64() < noticeUntil) Txt(notice, 0.42f, 0.80f, 0.6f, 255, 220, 120);
}

static void DrawBuy() {
    Rect(0.5f, 0.5f, 0.32f, 0.70f, 0, 0, 0, 190);
    char b[96];
    sprintf_s(b, "BUY MENU  $%d   (Up/Down, A/Enter = buy, B/Esc = close)", money);
    Txt(b, 0.35f, 0.17f, 0.5f);
    float y = 0.23f;
    for (int i = 0; i < NITEMS; i++) {
        int price = i < NG ? Guns[i].price : Gears[i - NG].price;
        const char* name = i < NG ? Guns[i].name : Gears[i - NG].name;
        sprintf_s(b, "%s%s  $%d", i == cursor ? "> " : "   ", name, price);
        if (i == cursor) Txt(b, 0.36f, y, 0.55f, 255, 220, 80);
        else Txt(b, 0.36f, y, 0.55f, money >= price ? 235 : 130, money >= price ? 235 : 130, money >= price ? 235 : 130);
        y += 0.04f;
        if (i == NG - 1) y += 0.02f;
    }
}

// ---------------- gameplay ----------------
static void Recoil(Ped p) {
    int gi = CurrentGun(p);
    if (gi < 0) { burst = 0; return; }
    ULONGLONG now = GetTickCount64();
    if (PED::IS_PED_SHOOTING(p)) {
        ULONGLONG interval = 60000 / Guns[gi].rpm;
        if (now - lastShot >= interval * 3 / 4) {
            lastShot = now;
            float k = Guns[gi].kick * (1.0f + fminf((float)burst, 10.0f) * 0.08f);
            float sway = Guns[gi].sway * sinf(burst * 0.9f) * fminf(1.0f, burst / 6.0f);
            CAM::SET_GAMEPLAY_CAM_RELATIVE_PITCH(CAM::GET_GAMEPLAY_CAM_RELATIVE_PITCH() + k, 1.0f);
            CAM::SET_GAMEPLAY_CAM_RELATIVE_HEADING(CAM::GET_GAMEPLAY_CAM_RELATIVE_HEADING() - sway);
            burst++;
        }
    } else if (now - lastShot > 350) burst = 0;
}

static void Movement(Ped p) {
    if (flying || PED::IS_PED_IN_ANY_VEHICLE(p, FALSE)) return;
    bool air = ENTITY::IS_ENTITY_IN_AIR(p) != 0;
    if (!air && CONTROLS::IS_CONTROL_PRESSED(0, 22) && !PED::IS_PED_JUMPING(p))
        AI::TASK_JUMP(p, TRUE);                                    // auto-hop; verify in game
    if (!air) return;
    float fwd = -CONTROLS::GET_CONTROL_NORMAL(0, 31), side = CONTROLS::GET_CONTROL_NORMAL(0, 30);
    if (fabsf(fwd) < 0.1f && fabsf(side) < 0.1f) return;
    const float U = 0.0254f, maxSpeed = 250 * U, airCap = 30 * U, airAccel = 12.0f;
    float yaw = CAM::GET_GAMEPLAY_CAM_ROT(2).z * 3.14159265f / 180.0f;
    float wx = -sinf(yaw) * fwd + cosf(yaw) * side, wy = cosf(yaw) * fwd + sinf(yaw) * side;
    float len = sqrtf(wx * wx + wy * wy); if (len < 0.001f) return;
    float wishSpeed = fminf(len * maxSpeed, maxSpeed);
    wx /= len; wy /= len;
    Vector3 v = ENTITY::GET_ENTITY_VELOCITY(p);
    float cur = v.x * wx + v.y * wy;
    float add = fminf(wishSpeed, airCap) - cur;
    if (add <= 0) return;
    float acc = fminf(airAccel * fminf(wishSpeed, airCap) * GAMEPLAY::GET_FRAME_TIME(), add);
    ENTITY::SET_ENTITY_VELOCITY(p, v.x + wx * acc, v.y + wy * acc, v.z);
}

static void Fly(Ped p) {
    Vector3 r = CAM::GET_GAMEPLAY_CAM_ROT(2);
    float pit = r.x * 3.14159265f / 180.0f, yaw = r.z * 3.14159265f / 180.0f;
    float dx = -sinf(yaw) * cosf(pit), dy = cosf(yaw) * cosf(pit), dz = sinf(pit);
    float in = -CONTROLS::GET_CONTROL_NORMAL(0, 31);
    float spd = (CONTROLS::IS_CONTROL_PRESSED(0, 21) ? 2.0f : 0.6f) * in;
    Vector3 c = ENTITY::GET_ENTITY_COORDS(p, TRUE);
    ENTITY::SET_ENTITY_COORDS_NO_OFFSET(p, c.x + dx * spd, c.y + dy * spd, c.z + dz * spd, 0, 0, 0);
}

static void KillRewards(Ped p, const int* peds, int n) {
    int gi = CurrentGun(p);
    for (int i = 0; i < n; i++) {
        Ped o = peds[i];
        if (o == p || !ENTITY::IS_ENTITY_DEAD(o) || rewarded.count(o)) continue;
        if (PED::GET_PED_SOURCE_OF_DEATH(o) == p) {
            rewarded.insert(o);
            int r = gi >= 0 ? Guns[gi].reward : 300;
            money = money + r > 16000 ? 16000 : money + r;
            char b[32]; sprintf_s(b, "+$%d", r); Notice(b);
        }
    }
    if (rewarded.size() > 500) rewarded.clear();
}

// ---------------- main ----------------
static void Update() {
    Ped p = PLAYER::PLAYER_PED_ID();
    if (!ENTITY::DOES_ENTITY_EXIST(p) || ENTITY::IS_ENTITY_DEAD(p)) return;

    // collect actions: keyboard F-keys, or hold View/Back + a pad button (Steam Deck)
    bool act[A_COUNT] = {};
    act[A_TOGGLE]  = KeyHit(VK_F9);
    bool kBuy = KeyHit('B');
    act[A_FLY]     = KeyHit(VK_F7);
    act[A_TP]      = KeyHit(VK_F8);
    act[A_CAR]     = KeyHit(VK_F6);
    act[A_NPC]     = KeyHit(VK_F5);
    act[A_WEAPONS] = KeyHit(VK_F4);
    act[A_SKIN]    = KeyHit(VK_F3);
    act[A_MONEY]   = KeyHit(VK_F2);
    bool kUp = KeyHit(VK_UP), kDown = KeyHit(VK_DOWN), kEnter = KeyHit(VK_RETURN), kEsc = KeyHit(VK_ESCAPE);

    bool chord = CONTROLS::IS_CONTROL_PRESSED(0, 217) || CONTROLS::IS_DISABLED_CONTROL_PRESSED(0, 217); // View/Back
    if (chord) {
        CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
        act[A_BUY]     = PadHit(188);  // DPad Up
        act[A_TOGGLE] |= PadHit(187);  // DPad Down
        act[A_FLY]    |= PadHit(189);  // DPad Left
        act[A_TP]     |= PadHit(190);  // DPad Right
        act[A_CAR]    |= PadHit(204);  // Y
        act[A_NPC]    |= PadHit(203);  // X
        act[A_WEAPONS]|= PadHit(201);  // A
        act[A_SKIN]   |= PadHit(202);  // B
        act[A_MONEY]  |= PadHit(206);  // RB
    }
    if (kBuy) act[A_BUY] = true;

    if (act[A_TOGGLE]) { SetMode(!modOn, p); }
    if (!modOn) { if (GetTickCount64() < noticeUntil) Txt(notice, 0.42f, 0.90f, 0.6f, 255, 220, 120); return; }

    if (act[A_BUY]) buyOpen = !buyOpen;
    if (act[A_FLY]) { flying = !flying; ENTITY::FREEZE_ENTITY_POSITION(p, flying); Notice(flying ? "Fly ON" : "Fly OFF"); }
    if (act[A_TP]) TeleportWaypoint(p);
    if (act[A_CAR]) Spawn(p, true);
    if (act[A_NPC]) Spawn(p, false);
    if (act[A_WEAPONS]) { for (int i = 0; i < NG; i++) WEAPON::GIVE_DELAYED_WEAPON_TO_PED(p, Guns[i].hash, 9999, FALSE); Notice("All CS2 weapons given"); }
    if (act[A_SKIN]) { skin = (skin + 1) % 8; WEAPON::SET_PED_WEAPON_TINT_INDEX(p, GAMEPLAY::GET_HASH_KEY((char*)"WEAPON_ASSAULTRIFLE"), skin);
                       Hash w = 0; WEAPON::GET_CURRENT_PED_WEAPON(p, &w, TRUE); WEAPON::SET_PED_WEAPON_TINT_INDEX(p, w, skin); Notice("Weapon tint changed"); }
    if (act[A_MONEY]) { money += 10000; Notice("+$10000"); }

    // hide default HUD, draw ours
    UI::HIDE_HUD_COMPONENT_THIS_FRAME(2); UI::HIDE_HUD_COMPONENT_THIS_FRAME(3); UI::HIDE_HUD_COMPONENT_THIS_FRAME(4);
    UI::DISPLAY_RADAR(FALSE);
    int peds[64]; int n = worldGetAllPeds(peds, 64);
    DrawHud(p, peds, n);

    if (buyOpen) {
        CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
        if (kUp || PadHit(188)) cursor = (cursor + NITEMS - 1) % NITEMS;
        if (kDown || PadHit(187)) cursor = (cursor + 1) % NITEMS;
        if (kEnter || PadHit(201)) Buy(cursor, p);
        if (kEsc || PadHit(202)) buyOpen = false;
        DrawBuy();
        return;
    }

    Recoil(p);
    Movement(p);
    if (flying) Fly(p);
    KillRewards(p, peds, n);
}

void ScriptMain() {
    for (int i = 0; i < NG; i++) Guns[i].hash = GAMEPLAY::GET_HASH_KEY((char*)Guns[i].wname);
    ApplyDamage(true);
    Notice("CS2 Mode loaded");
    while (true) { Update(); WAIT(0); }
}

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) scriptRegister(hInstance, ScriptMain);
    else if (reason == DLL_PROCESS_DETACH) scriptUnregister(hInstance);
    return TRUE;
}
