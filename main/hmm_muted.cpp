/* Codex: платформенный запуск; звук подключён в hmm_audio.cpp. Клод: ролики — hmm_clip.cpp.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "audio.h"
#include "audio_manager.h"
#include "core.h"
#include "localevent.h"
#include "timing.h"
#include "hmm_platform.h"
#include "battle_port_bridge.h"
/* Codex: наблюдатель боя сохранён; файловый CLI-мост x64 в приложение не входит. */
namespace Battle::PortBridge {
static Observer *observer;
void setObserver(Observer *p){observer=p;}
void beforeAction(Arena &a,const Command &c){if(observer)observer->beforeAction(a,c);}
void afterAction(Arena &a){if(observer)observer->afterAction(a);}
void afterStrike(Arena &a,const Unit &u,const TargetsInfo &t){if(observer)observer->afterStrike(a,u,t);}
void markDamageRandom(uint32_t,uint32_t,uint32_t){}
}
namespace fheroes2 {
HardwareInitializer::HardwareInitializer()=default; HardwareInitializer::~HardwareInitializer()=default;
CoreInitializer::CoreInitializer(const std::set<SystemInitializationComponent>&c){if(c.count(SystemInitializationComponent::Audio))Audio::Init();LocalEvent::initEventEngine();}
CoreInitializer::~CoreInitializer(){Audio::Quit();}
bool isComponentInitialized(SystemInitializationComponent c){return c==SystemInitializationComponent::Video||(c==SystemInitializationComponent::Audio&&Audio::isValid());}
void delayforMs(uint32_t ms){hmm_delay(ms);}
}
