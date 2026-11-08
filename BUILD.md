# Подготовка сборки

Это исходники платформенной части сборки 130 для ZX Evolution VDAC4.
Сторонний движок и игровые данные в Git не включены; автоматической загрузки
игровых ассетов нет.

## Внешние компоненты

- OpenHMM2 / fheroes2 1.1.17 с используемыми изменениями PortBridge.
- Xtensa ESP32-S3 GCC, ESP-IDF и SDK VDAC4.
- SDL2 headers; MSVC и Windows SDK для инструментов и стендов ПК.
- zlib, PIC runtime GCC и прочие компоненты, перечисленные в `THIRD_PARTY.md`.
- Собственные игровые данные для локального формирования DAT и заставки.

`tools/compile_probe.py` принимает пути через переменные среды:

```powershell
$env:HMM2_OPENHMM2_SRC = 'E:/external/OpenHMM2/src'
$env:HMM2_XTENSA_BIN = 'E:/tools/xtensa-esp32s3-elf/bin'
$env:HMM2_SDL_INCLUDE = 'E:/external/SDL2/include'
$env:HMM2_STAGE1_BASE = 'E:/build/hmm2-stage1'
```

Другие скрипты содержат пути существующего Windows-окружения; перед первым
запуском настройте пути к SDK VDAC4, MSVC, Windows SDK и PIC runtime в скриптах.

## Основные этапы

1. Предоставить внешнюю копию OpenHMM2 и остальные зависимости.
2. Сгенерировать `main/hmm_objects.inc` из определений объектов внешнего движка:
   `python tools/build_stage1_object_table.py`. Это генерируемая таблица, в Git её нет.
3. Подготовить платформенные изменения и скомпилировать:
   `python tools/build_stage1.py --packed --compile-only`.
4. Связать ELF: `python tools/link_stage1.py --lto-partitions 6 --compact-branches`.
5. Загрузчик Z80 и DAT подготавливаются отдельно соответствующими инструментами
   из `tools/`. Для загрузочной картинки `tools/make_loader_picture.py` нужны
   локальные изображения и палитра; они намеренно не опубликованы.

Из-за зависимости от внешней копии движка и установленного SDK этот репозиторий
не является самостоятельным комплектом всех входных файлов сборки.
Готовые ELF, SPG, DAT, изображения и результаты стендов остаются локальными.
