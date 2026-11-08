/* Клод: на экране курсор двигает своя задача (cursor_task, hmm_gpu.cpp); поток игры здесь только обновляет своё положение мыши. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void hmm_loading_tick(void);
void hmm_loading_mouse(int *x,int *y);
// Клод: отдать буферы петель фоновых звуков (hmm_audio.cpp) — перед боем и загрузкой мира, и по нехватке памяти.
void hmm_audio_drop_loops(void);
#ifdef HMM2_HOST
int hmm_gpu_move_cursor(int x,int y);
#endif
#ifdef __cplusplus
}
namespace fheroes2 {class Image;}
const fheroes2::Image &hmm_wait_cursor_image();
// Клод: подпись в рамке окна оригинала поверх кадра, чтобы было понятно, из-за чего пауза. once — только до следующего кадра игры,
// иначе до hmm_status_clear. Клод: видна, только если ожидание дольше 2 с — с начала третьей секунды (hmm_gpu.cpp, STATUS_DELAY_MS);
// hmm_status_clear убирает её со следующим кадром.
void hmm_status_show(const char *message,bool once);
void hmm_status_clear();
// Клод: подпись сразу и до первого ожидания ввода игрой после загрузки (или лёгких кадров карты); только в первом ходе после загрузки мира.
void hmm_status_hold(const char *message);
// Клод: загрузка кончилась — подпись hmm_status_hold уходит при первом ожидании ввода игрой.
extern "C" void hmm_status_hold_arm();
// Клод: держится ли «Map loading...» (hmm_status_hold) — её не снимает общее hmm_status_clear перед ходом игрока.
bool hmm_status_holding();
class HmmLoadingScope {
    int theme=0;bool visible=false;
public:
    explicit HmmLoadingScope(const char *message=nullptr);
    ~HmmLoadingScope();
    HmmLoadingScope(const HmmLoadingScope&)=delete;
};
#endif
