# HMM2-VDAC4

**Игра для ZX Evolution с VDAC4, сделана на основе OpenHMM2 (fheroes2 1.1.17).**

**Игровой архив `HMM2.DAT` с графикой, картами, музыкой, звуками и роликами
не включён в репозиторий. Для запуска его нужно добавить отдельно в
`GAMES/VDAC4/HMM2 VDAC4/` на SD-карте. Без этого файла игра не запустится.**

Логика игры работает на ESP32-S3. Загрузчик Z80 обслуживает запуск, SD-карту
и ввод на ZX Evolution / TS-Config. Графику выводит эмулятор FT812 в Tang Slot 1:
спрайты, отсечение, прозрачность окон и тени задаются аппаратными командами.

## Исходники порта

В репозитории опубликована платформенная часть актуальной сборки 130:

| Каталог | Содержимое |
| --- | --- |
| `main/` | Приложение ESP32-S3, файловый слой, ввод, звук, построение команд FT812 |
| `prototype/` | Кэш текстур RAM_G |
| `launcher/` | Исходники загрузчика Z80 для ZX Evolution |
| `tools/` | Подготовка платформенных изменений, упаковка и сборка |
| `tests/`, `bench/` | Исходники стендов и проверок |
| `licenses/` | Лицензии и уведомления об используемых компонентах |
| `_SD/` | Готовые ELF/SPG, настройки и структура SD-карты; без HMM2.DAT |

Исходники самого OpenHMM2, отдельные игровые ассеты, `HMM2.DAT`, сохранения
и журналы в этот репозиторий **не включены**. Они исключены правилами `.gitignore`.
В `_SD/` включены готовые программные файлы ELF/SPG и начальные настройки.
SPG содержит встроенную заставку загрузчика.

## Зависимости и сборка

OpenHMM2 / fheroes2 1.1.17 предоставляется отдельно. Используемая локальная
копия содержит изменения PortBridge; чистый upstream той же версии может
потребовать их применения. Скрипты порта подготавливают платформенный вариант
из этой внешней копии, не меняя её на месте.

Нужны инструменты Xtensa для ESP32-S3, SDK VDAC4 и локальные исходники/заголовки
зависимостей. Текущие стенды ориентированы на Windows и MSVC.
Параметры и порядок подготовки описаны в [BUILD.md](BUILD.md).

Готовая SD-карта для запуска имеет структуру:

```text
GAMES/VDAC4/HMM2 VDAC4/_HMM2.SPG
GAMES/VDAC4/HMM2 VDAC4/HMM2.ELF
GAMES/VDAC4/HMM2 VDAC4/HMM2.DAT
WC/ESP/OGG.ELF
```

Скопируйте содержимое [`_SD/`](_SD/) в корень SD-карты и добавьте свой
`HMM2.DAT` в `GAMES/VDAC4/HMM2 VDAC4/`. Запускается `_HMM2.SPG`.
Размер совместимого DAT — **547 492 852 байта**.
Подробности и контрольная сумма архива — в [`_SD/README.md`](_SD/README.md).

## Лицензии и происхождение

Порт сделан на основе **OpenHMM2 / fheroes2**, версия 1.1.17.
Основной код порта сохраняет лицензию **GPL-2.0-or-later**.
Происхождение и лицензии остальных компонентов указаны в [THIRD_PARTY.md](THIRD_PARTY.md).
Первоначальная MIT-лицензия пустого репозитория сохранена отдельно в
`licenses/PROJECT_INITIAL_MIT.txt`; она не меняет лицензию кода, основанного на OpenHMM2.

Все права на данные оригинальной игры принадлежат их правообладателям.
Полный игровой архив и отдельные игровые ассеты здесь не распространяются.

## Источники

Ссылки на основу игры, использованные компоненты и инструменты:

| Источник | Использование |
| --- | --- |
| [OpenHMM2 / fheroes2 1.1.17](https://github.com/ihhub/fheroes2/tree/1.1.17) | Основа движка и игровой логики |
| [VDAC4](https://github.com/andrewinsidelazarev/VDAC4) | Аппаратная платформа, ABI и SDK |
| [VDAC4: Tang / FT812 / BL616](https://github.com/andrewinsidelazarev/VDAC4/tree/main/Tang%20Nano%2020k) | Эмулятор FT812 и сопроцессор |
| [VDAC4: Sound](https://github.com/andrewinsidelazarev/VDAC4/tree/main/ESP32%20S3%20Zero%20N8R8/Sound) | OGG.ELF, звуковой модуль и код плеера |
| [VDAC4: Network](https://github.com/andrewinsidelazarev/VDAC4/tree/main/ESP32%20S3%20Zero%20N8R8/Network) | Общий код Z80/SPI и передачи данных |
| [DOOM VDAC4](https://github.com/andrewinsidelazarev/DOOM-VDAC4) | Основа части загрузчика и стендов |
| [doomgeneric](https://github.com/ozkl/doomgeneric) | Исходная основа DOOM VDAC4 |
| [FAT32 Driver ZX Evolution](https://github.com/andrewinsidelazarev/FAT32-Driver-ZX-Evolution/tree/e72839070fcdc8eb0b6dbae2b9f6ff52b0a2a22d) | Драйвер FAT32 загрузчика |
| [R-Type Arcade VDAC2 FT812](https://github.com/andrewinsidelazarev/R-Type-Arcade-VDAC2-FT812) | Поиск каталога и последовательности SPI |
| [Wild Commander Improved](https://github.com/andrewinsidelazarev/Wild-Commander-Improved) | Среда запуска; FTVIEW использован для сверки работы с FT812 |
| [stb_vorbis](https://github.com/nothings/stb/blob/master/stb_vorbis.c) | Декодер Ogg Vorbis в звуковом модуле |
| [libsmacker](https://libsmacker.sourceforge.net/) | Декодер исходных SMK при подготовке данных и в эталонных стендах |
| [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2) | Интерфейсы и стенды ПК |
| [SDL_mixer](https://github.com/libsdl-org/SDL_mixer) | Звуковые интерфейсы стендов ПК |
| [zlib](https://github.com/madler/zlib) | Сжатие данных, сохранений и puff-декодер |
| [GNU GCC 15.2.0](https://ftp.gnu.org/gnu/gcc/gcc-15.2.0/gcc-15.2.0.tar.xz) | Источники GCC, libstdc++ и unwinder для PIC runtime |
| [ESP-IDF](https://github.com/espressif/esp-idf) | Среда ESP32-S3 и компоненты стенда |
| [Espressif crosstool-NG](https://github.com/espressif/crosstool-NG) | Инструменты Xtensa |
| [Espressif ELF loader 1.3.3](https://github.com/espressif/esp-iot-solution/tree/5d75f3f0dc499d9ed4b69284a3741187c2b75a70/components/elf_loader) | Загрузка ELF в прошивке VDAC4 |
| [sjasmplus](https://github.com/z00m128/sjasmplus) | Ассемблер загрузчика Z80 |
| [Espressif QEMU](https://github.com/espressif/qemu) | Стенд исполнения ESP32-S3 |
| [Unreal Speccy](https://sourceforge.net/projects/unrealspeccy/) | Основа эмулятора для совместных проверок Z80 |
| [FFmpeg](https://github.com/FFmpeg/FFmpeg) | Локальная подготовка роликов и звуковых дорожек |
| [Pillow](https://github.com/python-pillow/Pillow) | Локальная обработка изображений |
| [NumPy](https://github.com/numpy/numpy) | Подготовка и проверка графических данных |
| [python-zopfli](https://github.com/fonttools/py-zopfli) | Упаковка данных ELF |

Сведения о лицензиях находятся в [THIRD_PARTY.md](THIRD_PARTY.md) и `licenses/`.
Ссылки на исходники не означают включение игровых данных в этот репозиторий.
