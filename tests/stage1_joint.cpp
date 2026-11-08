/* Codex: совместный стенд: неизменённые SpiLinkCore, Z80Transport и игровой
 * платформенный слой. Python моделирует только железо SPI, SD и процессор Z80.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "stage1_joint_app.h"
#include "../patches/slot0_autounload/after/include/zifi/ft_detach_guard.hpp"
#include <cstdlib>
#include <map>
#include "zifi/spi_link_core.hpp"
#include "zifi/z80_transport.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <stdexcept>
#include <SDL.h>
#define API extern "C" __declspec(dllexport)
static zifi::SpiLinkCore spi;
static uint8_t rxring[16384],txring[4096],bulk[512];
static std::atomic<uint32_t> last_activity{0},poll_period{1};
static std::atomic<uint32_t> commands[256],peak_rx{0},peak_tx{0};
static const auto epoch=std::chrono::steady_clock::now();
uint32_t millis(){return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-epoch).count());}
void delay(uint32_t ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
void yield(){std::this_thread::yield();}
struct Link: zifi::ByteLink {
    bool begin() override {return spi.begin(rxring,sizeof(rxring),txring,sizeof(txring));}
    size_t read(uint8_t*p,size_t n) override{return spi.read(p,n);}
    size_t write(const uint8_t*p,size_t n) override{return spi.write(p,n);}
    bool reserve(size_t n,uint32_t timeout) override{
        if(n>spi.writeCapacity())return false;
        uint32_t start=millis();while(n>spi.writeSpace()){
            uint32_t waited=millis()-start,quiet=millis()-last_activity.load();
            if(std::min(waited,quiet)>=std::max(timeout,5000u)||waited>=30000)return false;
            delay(1);
        }return true;
    }
    bool padsFrames()const override{return true;}
    void flush()override{}
    const char*name()const override{return "SPI";}
} link;
static zifi::Z80Transport transport(link);
// Codex: перезапуск модели: ACK, 100 мс до сброса, недоступный SPI и новый сеанс.
static std::mutex reboot_mutex;
static std::atomic<unsigned> reboot_delay{1500}, reboot_at{0}, reboot_ready{0};
static unsigned reboot_count=0, reboot_sessions=0, reboot_no_session=0, reboot_offline=0;
static bool reboot_cleared=false, reboot_needs_session=false;
API void joint_reboot_delay(unsigned ms){reboot_delay=ms;}
API void joint_reboot_stats(uint32_t*p){
    std::lock_guard<std::mutex>g(reboot_mutex);
    p[0]=reboot_count;p[1]=reboot_sessions;p[2]=reboot_no_session;p[3]=reboot_offline;
}
struct Packet {uint8_t command;std::vector<uint8_t> data;};
static std::deque<Packet> events,controls,control_replies;
static std::mutex event_mutex,log_mutex,control_mutex;
static FILE *journal;
static FILE *ft_trace;
static std::string output;
static const JointApp *application;
static std::mutex app_mutex;
static std::thread game_thread;
// Клод: задачи приложения кроме первой: на плате их несколько (курсор).
static std::vector<std::thread> extra_threads;
static std::atomic<bool> app_done_request{false}, app_loaded{false};
static unsigned saved_frames;static int saved_world,saved_result=-999,saved_state,saved_done;
static std::atomic<unsigned> stops{0},starts{0},closes{0},backs{0};
static bool in_command=false,auto_unload;
static zifi::FtDetachGuard detach_guard;
static std::atomic<bool> m_sel{false};
// Codex: как у ModuleApp, синхронизация и core.alloc принадлежат загруженному приложению.
static std::mutex resource_mutex;
struct SyncRecord{void *p;void(*destroy)(void*);};
static std::vector<SyncRecord> sync_records;
static std::map<void*,size_t> allocations;
static void *tracked_alloc(size_t n){void *p=std::malloc(n);if(p){std::lock_guard<std::mutex>g(resource_mutex);allocations[p]=n;}return p;}
static void tracked_free(void*p){if(!p)return;{std::lock_guard<std::mutex>g(resource_mutex);allocations.erase(p);}std::free(p);}
static void *tracked_realloc(void*p,size_t n){
    std::lock_guard<std::mutex>g(resource_mutex);void *q=std::realloc(p,n);
    if(q||!n){allocations.erase(p);if(q)allocations[q]=n;}return q;
}
static void logline(const char*format,...){
    std::lock_guard<std::mutex> guard(log_mutex);
    std::fprintf(journal,"%u ",millis());va_list ap;va_start(ap,format);std::vfprintf(journal,format,ap);va_end(ap);
    std::fputc('\n',journal);std::fflush(journal);
}
static void wire(const char*dir,uint8_t cmd,const uint8_t*p,unsigned n){
    if(cmd==0x75||cmd==0xf5||cmd==0x73||cmd==0xf3)return;
    char hex[111]={};for(unsigned i=0;i<n&&i<55;i++)std::sprintf(hex+i*2,"%02x",p[i]);
    logline("wire %s cmd=%02X n=%u in=%zu out=%zu keys=%u data=%s",dir,cmd,n,spi.pendingFromZ80(),spi.pendingToZ80(),commands[0x75].load(),hex);
}
struct Signal {std::mutex mutex;std::condition_variable cv;bool given=false;};
static void*signal_create(){auto*p=new Signal;std::lock_guard<std::mutex>g(resource_mutex);sync_records.push_back({p,[](void*v){delete static_cast<Signal*>(v);}});return p;}
static void signal_give(void*p){auto&s=*static_cast<Signal*>(p);std::lock_guard<std::mutex>g(s.mutex);s.given=true;s.cv.notify_one();}
static int signal_wait(void*p,uint32_t ms){auto&s=*static_cast<Signal*>(p);std::unique_lock<std::mutex>g(s.mutex);bool ok=s.cv.wait_for(g,std::chrono::milliseconds(ms),[&]{return s.given;});s.given=false;return ok?ZM_OK:ZM_TIMEOUT;}
static int event(uint8_t c,const void*p,uint16_t n,uint32_t wait){
    if(n>63)return ZM_ERROR;
    uint32_t started=millis();for(;;){
        {std::lock_guard<std::mutex>g(event_mutex);if(events.size()<8){events.push_back({c,std::vector<uint8_t>((const uint8_t*)p,(const uint8_t*)p+n)});wire("event",c,(const uint8_t*)p,n);return ZM_OK;}}
        if(millis()-started>=wait)return ZM_BUSY;delay(1);
    }
}
static int reply(uint8_t c,const void*p,uint16_t n){
    if(!in_command)return ZM_ERROR;wire("reply",c,(const uint8_t*)p,n);
    return transport.send(c,(const uint8_t*)p,n)?ZM_OK:ZM_ERROR;
}
static std::vector<uint8_t> ftmem(0x310000);
static std::mutex ft_mutex;
static std::atomic<uint32_t> ft_cfg{0},tang_core{1};
static bool ft_opened;
static std::atomic<unsigned> invalid_ft{0},link_errors{0};
static std::atomic<uint32_t> poll_resume{0};
API int joint_ft_spi(uint32_t a,const void *p,uint32_t n){
    // Codex: второй физический вход RAM_G; учитывается отдельно от LINK ESP.
    if(tang_core!=2 || a+n>0x300000 || (ft_cfg&4) || (n&3)){++invalid_ft;return ZM_ERROR;}
    std::lock_guard<std::mutex> guard(ft_mutex);
    uint32_t header[]={a|0x80000000u,n/4};std::fwrite(header,4,2,ft_trace);std::fwrite(p,1,n,ft_trace);
    std::memcpy(ftmem.data()+a,p,n);return ZM_OK;
}
static std::chrono::steady_clock::time_point swap_ready;
static int ft_write(uint32_t a,const uint32_t*p,uint32_t n){
    if(tang_core!=2 || a+size_t(n)*4>ftmem.size()){++invalid_ft;return ZM_ERROR;}
    std::lock_guard<std::mutex> guard(ft_mutex);
    std::fwrite(&a,4,1,ft_trace);std::fwrite(&n,4,1,ft_trace);std::fwrite(p,4,n,ft_trace);
    // Codex: фактический потолок LINK платы, без ускорения канала хостом.
    std::this_thread::sleep_for(std::chrono::duration<double>(double(n)*4/1330000.0));
    std::memcpy(ftmem.data()+a,p,n*4);
    if(a==0x3020fc)std::memcpy(ftmem.data()+0x3020f8,p,4);
    if(a==0x302054)swap_ready=std::chrono::steady_clock::now()+std::chrono::microseconds(16667);
    return ZM_OK;
}
static int ft_read(uint32_t a,uint32_t*p){if(tang_core!=2 || a+4>ftmem.size()){++invalid_ft;return ZM_ERROR;}*p=a==0x302054?(std::chrono::steady_clock::now()<swap_ready?2:0):*(const uint32_t*)(ftmem.data()+a);return ZM_OK;}
static zm_core_t core{};
static void firmware_poll();
static void frame_marker(unsigned frame){
    std::lock_guard<std::mutex> guard(ft_mutex);
    uint32_t marker[]={0xffffffffu,1,frame};std::fwrite(marker,4,3,ft_trace);std::fflush(ft_trace);
}
static bool stop_app(){
    std::lock_guard<std::mutex> guard(app_mutex);
    if(!application)return true;
    application->app->stop();
    if(!application->done())return false;
    if(game_thread.joinable())game_thread.join();
    for(auto &t:extra_threads)t.join();extra_threads.clear();
    saved_frames=application->frames();saved_world=application->world_ready();saved_result=application->result();
    saved_state=application->state();saved_done=application->done();
    application=nullptr;app_loaded=false;++stops;
    {std::lock_guard<std::mutex> lock(event_mutex);events.clear();}
    {std::lock_guard<std::mutex>g(resource_mutex);
        for(auto &s:sync_records)s.destroy(s.p);sync_records.clear();
        for(auto &a:allocations)std::free(a.first);allocations.clear();
    }
    detach_guard.reset();logline("APP_UNLOADED tasks=0 sync=0 allocations=0");return true;
}
API void* joint_core(){return &core;}
API void* joint_marker(){return reinterpret_cast<void*>(frame_marker);}
API int joint_bind(const JointApp*p){std::lock_guard<std::mutex> guard(app_mutex);if(application)return 0;application=p;app_loaded=true;return 1;}
API int joint_stop(){return stop_app()?1:0;}
API int joint_loaded(){return app_loaded?1:0;}
API void joint_evo_reset(){
    // Codex: аппаратный переход Tang в SLOT0; отмену I/O и завершение выполняет только код ELF.
    std::lock_guard<std::mutex> guard(ft_mutex);tang_core=1;ft_cfg=0;m_sel=false;logline("EVO_RESET M_SEL=0 Tang core 01 slot0");
}
API void joint_link_errors(unsigned count){link_errors=count;}
API void joint_pause_poll(unsigned ms){poll_resume=millis()+ms;}
API void joint_lifecycle(uint32_t*p){
    std::lock_guard<std::mutex> guard(app_mutex);
    p[0]=application?application->done():saved_done;p[1]=application?application->state():saved_state;
    p[2]=ft_cfg;p[3]=tang_core;p[4]=closes;p[5]=backs;p[6]=starts;p[7]=stops;p[8]=invalid_ft;p[9]=app_loaded;
}
API void joint_resources(uint32_t*p){
    std::lock_guard<std::mutex>g(resource_mutex);size_t bytes=0;for(auto&a:allocations)bytes+=a.second;
    p[0]=unsigned(allocations.size());p[1]=unsigned(bytes);p[2]=unsigned(sync_records.size());p[3]=app_loaded?1:0;
}
API void joint_init(const char*dir){
    auto_unload=std::getenv("HMM2_TEST_AUTOUNLOAD")!=nullptr;
    output=dir;journal=std::fopen((output+"/esp.log").c_str(),"wb");if(!journal)std::abort();
    ft_trace=std::fopen((output+"/frames.ft").c_str(),"wb");if(!ft_trace)std::abort();
    core.size=sizeof(core);core.abi_major=1;core.abi_minor=6;core.log=logline;core.millis=millis;core.delay=delay;
    core.mutex_create=[]()->void*{auto*p=new std::mutex;std::lock_guard<std::mutex>g(resource_mutex);sync_records.push_back({p,[](void*v){delete static_cast<std::mutex*>(v);}});return p;};
    core.mutex_lock=[](void*p){static_cast<std::mutex*>(p)->lock();};
    core.mutex_unlock=[](void*p){static_cast<std::mutex*>(p)->unlock();};
    core.signal_create=signal_create;core.signal_give=signal_give;core.signal_wait=signal_wait;
    core.event=event;core.reply=reply;core.ft_write=ft_write;core.ft_read=ft_read;
    core.ft_open=[](uint32_t)->int{tang_core=2;ft_cfg=5;ft_opened=true;m_sel=true;detach_guard.opened(true);return ZM_OK;};
    core.ft_close=[]{if(ft_opened){ft_opened=false;ft_cfg=ft_cfg&~4u;++closes;logline("FT_CLOSE host=Evo");}};
    core.core_back=[](uint32_t timeout)->int{if(timeout!=10000)return ZM_ERROR;tang_core=1;ft_cfg=0;++backs;logline("CORE_BACK slot0");return ZM_OK;};
    core.link_read=[](uint32_t a,uint32_t*p,uint32_t n)->int{
        if(link_errors){--link_errors;return ZM_ERROR;}
        for(unsigned i=0;i<n;++i,a+=4)p[i]=a==0?0x56444334u:a==4?(tang_core<<24)|0x0120u:a==0x5c?((ft_cfg&4)<<10)|((ft_cfg&1)<<27):0;
        return ZM_OK;
    };
    core.task_start=[](zm_task_fn fn,void *arg,uint32_t,uint8_t,int8_t)->int{if(game_thread.joinable())extra_threads.emplace_back([=]{fn(arg);});else game_thread=std::thread([=]{fn(arg);});return ZM_OK;};
    core.app_done=[]{if(in_command)app_done_request=true;};
    core.link_write=[](uint32_t address,const uint32_t*p,uint32_t)->int{if(address==0x5c)ft_cfg=*p;return ZM_OK;};
    core.alloc=[](size_t n,uint32_t)->void*{return tracked_alloc(n);};core.free=tracked_free;core.realloc=tracked_realloc;
    transport.begin();
    std::thread([]{for(;;){firmware_poll();delay(poll_period.load());}}).detach();
    // Codex: app.poll вызывается независимо от команд и ожиданий транспорта, как networkCore ESP.
    std::thread([]{for(;;){{std::lock_guard<std::mutex>g(app_mutex);if(application&&int32_t(millis()-poll_resume.load())>=0)application->app->poll(millis());}delay(10);}}).detach();
}
API int joint_start(){
    std::lock_guard<std::mutex> guard(app_mutex);if(!application)return ZM_ERROR;
    ++starts;logline("APP_START %u",starts.load());return application->app->start(&core);
}
// Codex: Z80 должен продолжать вычитывать SPI, пока command выдаёт поток ответов.
// Блокировка диагностического опроса здесь останавливала потребителя заполненной очереди.
API unsigned joint_frames(){std::unique_lock<std::mutex> g(app_mutex,std::try_to_lock);return g.owns_lock()?(application?application->frames():saved_frames):0;}
API int joint_world(){std::unique_lock<std::mutex> g(app_mutex,std::try_to_lock);return g.owns_lock()?(application?application->world_ready():saved_world):0;}
API int joint_result(){std::unique_lock<std::mutex> g(app_mutex,std::try_to_lock);return g.owns_lock()?(application?application->result():saved_result):-999;}
static void firmware_poll(){
    if(reboot_at){
        if(int32_t(millis()-reboot_at.load())<0)return;
        std::lock_guard<std::mutex>g(reboot_mutex);
        if(!reboot_cleared){
            // Codex: уничтожаем парсер, кольца, TID и состояние DMA прежнего ESP.
            transport=zifi::Z80Transport(link);transport.begin();
            std::memset(bulk,0,sizeof(bulk));
            reboot_cleared=true;reboot_needs_session=true;++reboot_count;
            logline("SYS_RESET boot: transport cleared, Wi-Fi reconnecting, SMB/FTP sessions lost");
        }
        if(int32_t(millis()-reboot_ready.load())<0)return;
        reboot_at=0;logline("SYS_RESET ready: waiting for SESSION|DROP");
    }
    if(auto_unload&&detach_guard.due(millis())&&detach_guard.observe(0x56444334u,(tang_core.load()<<24)|0x00010120u,m_sel?1u<<12:0)){
        logline("CORE_SLOT0_UNLOAD M_SEL=0");stop_app();
    }
    {std::lock_guard<std::mutex>g(control_mutex);if(!control_replies.empty()){
        auto e=std::move(control_replies.front());control_replies.pop_front();
        if(!transport.send(e.command,e.data.data(),uint16_t(e.data.size())))logline("CONTROL_SEND_FAILED %02X",e.command);
    }}
    // Codex: порядок Application::pollLink: одно событие, затем одна команда.
    Packet e{};bool have=false;{
        std::lock_guard<std::mutex>g(event_mutex);if(!events.empty()){e=std::move(events.front());events.pop_front();have=true;}
    }
    if(have){bool ok=transport.send(e.command,e.data.data(),uint16_t(e.data.size()));logline("event_sent cmd=%02X ok=%d",e.command,int(ok));}
    zifi::PacketView p;
    if(transport.poll(p)){
        ++commands[p.command];
        wire("rx",p.command,p.data,p.length);
        if(p.command==0x0f){
            // Codex: SYS_RESET не имеет ответа 8F; прошивка возвращает пустой FE.
            transport.sendAck();
            {std::lock_guard<std::mutex>g(reboot_mutex);
                reboot_cleared=false;reboot_ready=millis()+100+reboot_delay.load();reboot_at=millis()+100;}
            logline("SYS_RESET ACK; restart in 100 ms");
            std::lock_guard<std::mutex>g(control_mutex);controls.push_back({p.command,{}});return;
        }
        if(p.command==0x31||(p.command>=0x70&&p.command<=0x77)){
            {std::lock_guard<std::mutex> guard(app_mutex);if(application){
                in_command=true;int rc=application->app->command(p.command,p.data,p.length);in_command=false;
                if(rc!=ZM_OK)logline("COMMAND_ERROR %02X %d",p.command,rc);
            }}
            if(app_done_request.exchange(false))stop_app();
        }
        else {std::lock_guard<std::mutex>g(control_mutex);controls.push_back({p.command,std::vector<uint8_t>(p.data,p.data+p.length)});}
    }
}
API void joint_pump(){}
API void joint_period(unsigned ms){poll_period=ms;}
API unsigned joint_control(uint8_t*p){std::lock_guard<std::mutex>g(control_mutex);if(controls.empty())return 0;auto e=std::move(controls.front());controls.pop_front();p[0]=e.command;std::memcpy(p+1,e.data.data(),e.data.size());return unsigned(e.data.size()+1);}
API int joint_send(uint8_t c,const uint8_t*p,unsigned n){wire("control",c,p,n);std::lock_guard<std::mutex>g(control_mutex);control_replies.push_back({c,std::vector<uint8_t>(p,p+n)});return 1;}
API unsigned joint_spi(const uint8_t*req,uint8_t*out,int state,int available,const uint8_t*data,unsigned n){
    std::lock_guard<std::mutex>g(reboot_mutex);
    if(reboot_cleared&&reboot_at&&int32_t(millis()-reboot_ready.load())<0){++reboot_offline;return 0;}
    if(reboot_needs_session){
        if((req[2]&0x81)!=0x81){++reboot_no_session;return 0;}
        reboot_needs_session=false;++reboot_sessions;
        logline("SYS_RESET new SESSION|DROP");
    }
    last_activity=millis();
    zifi::SpiLinkPutInput put;put.available=available!=0;put.received=data;put.receivedLength=n;put.capacity=8192;
    size_t count=0;if(!spi.handleRequest(req,out,count,zifi::SpiLinkCore::BulkState(state),bulk,put))return 0;
    peak_rx=std::max(peak_rx.load(),uint32_t(spi.pendingFromZ80()));peak_tx=std::max(peak_tx.load(),uint32_t(spi.pendingToZ80()));
    return unsigned(count);
}
API void joint_state(uint8_t*p){
    std::lock_guard<std::mutex>g(reboot_mutex);
    if(reboot_cleared&&reboot_at&&int32_t(millis()-reboot_ready.load())<0){std::memset(p,0,4);++reboot_offline;return;}
    spi.stateWord(p,uint8_t(0x42+reboot_count));
}
API void joint_bulk(uint8_t*p){std::memcpy(p,bulk,512);}
API void joint_stats(uint32_t*p){auto s=spi.stats();std::memcpy(p,&s,sizeof(s));p[11]=peak_rx;p[12]=peak_tx;for(unsigned i=0;i<256;i++)p[13+i]=commands[i];}
