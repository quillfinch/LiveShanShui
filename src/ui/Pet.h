// Live Shan Shui - desktop pets (tamagotchi companions above the taskbar).
//
// A small always-on-top window that floats just above the taskbar and renders a
// tamagotchi-style companion with Direct2D. Six selectable species (see PetKind)
// share one stat model: click to pet (hearts + XP), the cookie button feeds, the
// ball button plays. Hunger and happiness decay slowly; XP accumulates into
// levels; everything persists per species in pet.ini so each pet remembers you
// between sessions. The pet hides itself behind fullscreen apps and comes back
// when the desktop is visible again.
#pragma once
#include <windows.h>
#include <cstdint>

namespace lp {

namespace gfx { class Device; }

// The selectable companions. Positional wiring: kPetKinds[] in Pet.cpp must stay
// in the same order, and its size must equal kPetKindCount.
enum class PetKind {
    Cat = 0,      // Mochi, the cream blob cat
    Shiba,        // Taro, the tan puppy
    Axolotl,      // Lumi, the pink axolotl
    Chick,        // Pip, the round yellow chick
    Ghost,        // Boo, the floating translucent blob
    Panda,        // Momo, the white-and-black panda
};
constexpr int kPetKindCount = 6;

// Pet window (and selftest surface) size in pixels.
constexpr int kPetWindowW = 150;
constexpr int kPetWindowH = 170;

const wchar_t* PetKindKey(PetKind kind);        // "cat" .. used by config/CLI
const wchar_t* PetKindName(PetKind kind);       // "Mochi" .. display name
uint32_t PetKindAccent(PetKind kind);           // 0xRRGGBB swatch color
PetKind PetKindDefault();
// Accepts a kind key; also tolerates a display name. Returns false when unknown.
bool PetKindFromKey(const wchar_t* key, PetKind* out);

class Pet {
public:
    Pet();
    ~Pet();
    Pet(const Pet&) = delete;
    Pet& operator=(const Pet&) = delete;

    bool Create(HINSTANCE instance);
    void Destroy();

    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enabled);          // show/hide (creates or hides the window)

    // Suppressed = the wallpaper would pause (fullscreen app): hide the pet too.
    void SetSuppressed(bool suppressed);

    // Card background behind the pet (off = transparent, just the creature).
    void SetCard(bool card);

    // Switch the species. Stats are stored per kind in pet.ini, so each pet
    // keeps its own hunger/happiness/xp across switches and sessions.
    void SetKind(PetKind kind);
    PetKind GetKind() const;

    void Tick(float dt);                    // advance state + decay
    void Render();                          // draw one frame (engine-driven)

    // Renders one frame of the given kind into a caller-provided device
    // (offscreen, not the pet window). Used by --selftest.
    static bool RenderTestFrame(gfx::Device& device, PetKind kind, float time);

    // Called from the pet's own window procedure.
    bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);

    HWND Window() const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
    bool m_enabled = false;
};

// Validates the kind table and renders every species (and one PNG each, named
// "<pngPrefix>-<key>.png"). Returns the number of failures (0 = pass).
int PetSelfTest(gfx::Device& device, const wchar_t* pngPrefix);

} // namespace lp
