// Live Shan Shui - the one and only wallpaper scene: an endless Chinese ink
// landscape scroll rendered with Direct2D. The scene owns the ss::World, keeps
// a small ring of pre-baked screen-sized tiles (paper + painting), advances a
// slow camera, and replays two bitmaps per frame. Based on shan-shui-inf by
// LingDong (MIT); see ShanShuiLib.h.
#pragma once
#include "../gfx/Scene.h"
#include "ShanShuiLib.h"
#include <d2d1_1.h>
#include <vector>

namespace lp {

Scene* CreateShanShuiScene();

} // namespace lp
