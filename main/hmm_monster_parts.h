#pragma once
#include <memory>
#include <utility>
#include <vector>
#include <atomic>
#include <stdexcept>
#include "ui_object_rendering.h"
#ifdef HMM2_HOST
#include <thread>
#else
extern "C" {
#include "hmm_core.h"
}
#endif
namespace Maps {
class Tile;
using HmmMonsterParts=std::shared_ptr<const std::vector<fheroes2::ObjectRenderingInfo>>;
std::pair<HmmMonsterParts,HmmMonsterParts> hmmMonsterPartsPair(const Tile &,bool);
}
extern "C" void hmm_monster_parts_clear();
extern "C" void hmm_monster_parts_stats(unsigned *,unsigned *,unsigned *,int);
