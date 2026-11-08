# Codex: приложение из файла, загрузчик взят из VDAC4 без изменений.
idf_component_register(SRCS "main.c" INCLUDE_DIRS "E:/zx/Tang Nano 20k VDAC/VDAC4/ESP32 S3 Zero N8R8/Firmware/modules/sdk/include" PRIV_REQUIRES elf_loader esp_timer EMBED_FILES "HMM2.ELF" "OGG.ELF")
