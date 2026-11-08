# Происхождение и лицензии

Игра для **ZX Evolution VDAC4** сделана на основе **OpenHMM2 / fheroes2 1.1.17**.
Исходный движок предоставляется отдельно и в этот репозиторий не скопирован.
Платформенные изменения и адаптеры ориентированы на локальную версию с PortBridge.

| Компонент | Использование | Лицензия |
| --- | --- | --- |
| [OpenHMM2 / fheroes2](https://github.com/ihhub/fheroes2/tree/1.1.17) | Основа логики игры; исходники движка не опубликованы здесь | GPL-2.0-or-later |
| [DOOM VDAC4](https://github.com/andrewinsidelazarev/DOOM-VDAC4) | Основа части загрузчика Z80 и стендов | GPL-2.0-or-later, уведомления сохранены |
| [FAT32 Driver ZX Evolution](https://github.com/andrewinsidelazarev/FAT32-Driver-ZX-Evolution) | Внешняя зависимость загрузчика | MIT, `licenses/FAT32-MIT.txt` |
| [R-Type](https://github.com/andrewinsidelazarev/R-Type-Arcade-VDAC2-FT812) / [VDAC4 и плеер](https://github.com/andrewinsidelazarev/VDAC4) | Использованные последовательности SD/SPI и интерфейсы | MIT, соответствующие файлы в `licenses/` |
| [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2), [zlib](https://github.com/madler/zlib), [libsmacker](https://libsmacker.sourceforge.net/) | Внешние зависимости, исходники и бинарники не включены | Исходные лицензии соответствующих проектов |
| [GCC runtime / unwinder](https://ftp.gnu.org/gnu/gcc/gcc-15.2.0/gcc-15.2.0.tar.xz) | Внешняя PIC-сборка runtime | GPL-3.0 с GCC Runtime Library Exception 3.1 |
| [SDK VDAC4](https://github.com/andrewinsidelazarev/VDAC4) и [ELF loader](https://github.com/espressif/esp-iot-solution/tree/master/components/elf_loader) | Внешние ABI и загрузчик | MIT / Apache-2.0 |

Звуковой модуль `OGG.ELF` использует [stb_vorbis](https://github.com/nothings/stb): MIT / public domain; уведомления сохранены в `licenses/STB-VORBIS.txt`.

Файл `LICENSE` содержит GPL-2.0. Указание «or later» сохранено в заголовках
файлов порта. Отдельные компоненты сохраняют собственные авторские уведомления.
Первоначальная MIT-лицензия пустого GitHub-репозитория сохранена как
`licenses/PROJECT_INITIAL_MIT.txt` и не заменяет GPL для производного кода.

Графика, музыка, звуки, карты, ролики, AGG, DAT и прочие игровые данные
не включены. Их права принадлежат правообладателям оригинальной игры.
