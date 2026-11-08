/* Codex: исполняем Xtensa ELF настоящим загрузчиком VDAC4 в QEMU ESP32-S3. */
#include "zm_app.h"
#include "esp_elf.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

extern const uint8_t module_start[] asm("_binary_HRT_ELF_start");
static void logline(const char*f,...){va_list a;va_start(a,f);vprintf(f,a);putchar('\n');va_end(a);}
static void *mutex_create(void){return xSemaphoreCreateMutex();}
static void mutex_lock(void*m){xSemaphoreTake(m,portMAX_DELAY);}
static void mutex_unlock(void*m){xSemaphoreGive(m);}
static void delay(uint32_t ms){vTaskDelay(pdMS_TO_TICKS(ms));}
static uint32_t millis(void){return (uint32_t)(esp_timer_get_time()/1000);}
void app_main(void){
    /* Codex: это ровно экспорты zm_symbols.cpp, дополнительных услуг модулю стенд не даёт. */
    static const struct esp_elfsym extra[]={
        {"memmove",memmove},{"memcmp",memcmp},{"memchr",memchr},{"strtoul",strtoul},ESP_ELFSYM_END};
    esp_elf_register_symbol(extra);
    static zm_core_t core={.size=sizeof(zm_core_t),.abi_major=1,.abi_minor=3,.log=logline,.millis=millis,.delay=delay,
       .mutex_create=mutex_create,.mutex_lock=mutex_lock,.mutex_unlock=mutex_unlock};
    esp_elf_t elf;int result=esp_elf_init(&elf);printf("QEMU: init=%d\n",result);
    if(result)return;
    result=esp_elf_relocate(&elf,module_start);printf("QEMU: relocate=%d\n",result);
    if(result)return;
    const zm_app_t *app=0;char*argv[]={"HRT.ELF",(char*)&core,(char*)&app};
    result=elf.entry(3,argv);printf("QEMU: entry=%d app=%p\n",result,app);
    if(result!=1||!app)return;
    result=app->start(&core);printf("QEMU: start=%d\n",result);
    app->stop();esp_elf_deinit(&elf);printf("QEMU: завершено\n");fflush(stdout);
}
