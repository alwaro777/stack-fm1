# HARDWARE — M-VAVE FM-1 (Fase 0)

Datos recogidos de las fuentes, no supuestos. Cada fila cita fichero:línea. Rutas `base/…` = submódulo
[fm1-base](https://github.com/charlesvestal/fm1-base) @ `c8e0599` (2026-11-13), `base/bsp/…` = felucca-bsp @ `18eadad`.
Otras: `fm1_open` = [m-vave/fm1_open](https://github.com/m-vave/fm1_open) (`main`, 2026-10-21), Felucca =
[hugelton/Felucca](https://github.com/hugelton/Felucca) @ `6b1115b`, hortator = [DeadActive/hortator](https://github.com/DeadActive/hortator) @ `44c9ad4`.
**NO ENCONTRADO** = no está en ninguna fuente: se mide (columna "Cómo medirlo").

## SoC y CPU
| Dato | Valor | Fuente |
|---|---|---|
| SoC | JieLi AC791N (familia WL82 / AC79) | fm1_open `app/board/fm1_gpio.h:3`; fm1_open `THIRD_PARTY.md:59` |
| CPU | pi32v2 (`-mcpu=r3`), toolchain JieLi clang 4.0.1 | fm1_open `sdk/Build.ps1:358`; `base/tools/build.py:362-409` |
| Núcleos | 2 (core 0 + core 1 "beta", usable como worker del render: `cpu2 = true`) | `base/bsp/README.md` (chip/fm1_cpu1.*); `base/tools/build.py` (`cpu2`) |
| Reloj | **360 MHz** de serie (396 opcional, no recomendado) | `base/include/plat.h:494`; `base/tools/build.py:170,888` |
| RTOS | ninguno en fm1-base (bucle principal + IRQs); FreeRTOS solo en el firmware oficial | `base/device/main_fm1.c`; fm1_open `THIRD_PARTY.md:65` |
| FPU | float simple usable (la plantilla renderiza en `float`); `double` prohibido por el build | `base/template/app.c:63-82`; `base/DESIGN.md` ("no `double`") |

## Memoria
| Dato | Valor | Fuente |
|---|---|---|
| SRAM interna | ≈511 KiB (0x1C00000..0x1C7FE00), sin PSRAM | fm1_open `sdk/config/linker.ld.in:13-26` |
| Reparto fm1-base (layout "fixed") | código RAM 32 KiB, `.data/.bss` 96 KiB, **pool 336 KiB** (buffers grandes con `PLAT_POOL`) | `base/tools/build.py:142`; `base/device/app.ld:10,50` |
| Cache como RAM | 2/4/7 vías de 4 KiB opcionales (`cache_ram_ways`) | `base/tools/build.py` (`cache_ram_ways`) |
| Flash | 1 MiB; área de datos de app 0x97000..0xDFFFF; objetos de store ≤3840 B (pares A/B de 4 KiB) | fm1_open `sdk/Package.cs:149`; `base/include/plat.h:345-363,482-489` |
| Chips de flash | JEDEC 0x856014 (Puya P25Q80) y 0xB36014 (UC25HQ80), ambos aceptados | `base/device/fm1_flash_ok.h:21` |
| RAM libre real | **NO ENCONTRADO** — lo imprime cada build de hardware | Medir: salida de `build.py` en CI |

## Audio
| Dato | Valor | Fuente |
|---|---|---|
| Frecuencia | 44 100 Hz nominal; real 24 MHz/544 = 44 117,6 Hz | `base/bsp/boards/fm1/fm1_audio.h:53-55` |
| Formato | estéreo intercalado, muestras de 24 bits en `int32_t` | `base/template/app.h:23` (`app_render`) |
| Bloque | `APP_BLOCK` frames por media-DMA; por defecto **256 (5,8 ms)**, configurable (fm1_open usa 128) | `base/device/unity.c:15`; fm1_open `sdk/include/fm1_block.h:5-8` |
| Callback | `app_render(int32_t *lr, uint32_t n, uint32_t master_q12)` desde la IRQ de audio | `base/template/app.h:23` |
| Presupuesto | 360 MHz × 5,8 ms ≈ **2,09 M ciclos por bloque de 256** (≈8 160 ciclos/muestra); `plat_cpu_pct()` mide carga, `APP_OVERLOAD` avisa >90 % | `base/include/plat.h:493-499,611` |
| Código de audio | `PLAT_RAM_CODE` (fuera de la XIP cache), verificado por el build | `base/include/plat.h:15-45` |
| Motor DX7 | fm1_open: binario cerrado `libfm1_engine.a`. **No lo usamos**: FM vía msfa portado a entero (Felucca `fm6_core.c`, Apache-2.0) | fm1_open `THIRD_PARTY.md:27-35`; Felucca `firmware/src/fm6_core.c:1-25` |
| Ciclos por motor | **NO ENCONTRADO** | Medir: `plat_cpu_pct` + `plat_cycles*` en el bench de F1/F6 |

## Pantalla
| Dato | Valor | Fuente |
|---|---|---|
| Resolución | **240×240** | `base/bsp/boards/fm1/fm1_lcd.h:17`; fm1_open `sdk/include/fm1.h:109-110` |
| Controlador | ST7789V por SPI1 (solo escritura) | fm1_open `app/board/lcd.inc:3`; `base/device/lcd.c` (`LCD_SEQ`) |
| Color | RGB565, **big-endian en el bus** (el canvas guarda píxeles byte-swapped) | `base/device/gfx.c:6,27,61`; `base/bsp/loader/ldr_screen.c:190` |
| Framebuffer | **no hay uno completo**: canvas en tiras (`cv_begin/cv_blit`) o `lcd_blit(x,y,w,h,px)` por DMA | `base/device/lcd.c:104-112`; Felucca `gfx.c:3,70` |
| Refresco | UI llamada ~60 veces/s (`app_frame`); el envío depende del área sucia | `base/template/app.h:27` |
| Velocidad SPI | **NO ENCONTRADO** en Hz (fm1-emu modela 12 MHz ≈ 77 ms/frame completo) | Medir: `plat_ticks24` alrededor de un `lcd_blit` de 240×240 |

## Controles (inventario completo)
| Control | Cantidad / nombres | Lectura en fm1-base | Fuente |
|---|---|---|---|
| Botones | 14: FX, SEL(SCL), ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC, OCT−, OCT+ | `plat_buttons()` bitmask `B_*` | `base/include/plat.h:49-50` |
| Encoders | 7 sin fin: SELECT, ALGO, PRESET, K1–K4 | `plat_enc(EN_*)` → detents desde la última llamada | `base/include/plat.h:51,58` |
| Pot MASTER | 1 absoluto | `plat_master()` 0..4096 (ganancia ya aplicada en `master_q12`) | `base/include/plat.h:59` |
| Teclas | 27 (F3–G5): **16 blancas + 11 negras** | `plat_keys()` bitmask; `PLAT_WHITE_KEYS`/`PLAT_BLACK_KEYS` | `base/include/plat.h:53-56` |
| Velocidad/presión | opcional (`APP_TOUCH`, lectura analógica de teclas); sin ella, sin velocidad | `base/template/app.h:12`; fm1_open `screen.c:2550` (vel fija 100) |
| LEDs | uno por botón y tecla, 3 niveles + PLAY verde/rojo | `plat_lights`, `plat_leds` | `base/include/plat.h:70-95` |
| Escaneo | matriz 11 columnas, 10 kHz por defecto (`input_hz`) | — | `base/tools/build.py:179-201` |

## MIDI
USB y BLE in/out, TRS solo entrada (`PORT_USB/BLE/TRS`, `plat_midi_in/send`), clock 24 ppqn por software
(`base/include/plat.h:98-120`). BLE ocupa mucha RAM: opcional (`ble = true`).

## Build, paquete, instalación, rescate
| Tema | Dato | Fuente |
|---|---|---|
| Build | `base/tools/build.py app [--release X.Y] [--host] [--web]`; toolchain JieLi solo Linux x86-64 (Docker en macOS) | `base/tools/build.py:5-16,273-275` |
| Paquete `.fwsc` | contenedor UFW JieLi: CRC16 + cifrado XOR (clave chip 0x980F), app + `ota.bin` (update loader de felucca-bsp), **sin firma** | Felucca `tools/fm1pkg_make.py:4-205`; `base/bsp/tools/fm1pkg.py` |
| Update loader | `base/bsp/loader/` sin modificar; nunca escribe la cabecera ni el store 0x97000..0xE0000 | `base/bsp/loader/loader.c:1-30` |
| Instalación | Web MIDI SysEx (Chrome/Edge escritorio) o `fm1_install.py` | `base/web/fm1ota.js`; `base/tools/fm1_install.py` |
| CI / Hub | `zvenson/fm1-build@v1` (ubuntu-24.04, AC79 SDK fijado, atestación + SHA256SUMS); plantilla en `base/template/.github/workflows/release.yml` | thefirmwarehub.com/submit, /kb |
| Arranque con teclas (fm1-base) | **REC** al encender → modo update del chip; OCT−+OCT+ → calibración del panel; 2 arranques fallidos → modo seguro (USB activo, sin audio) | `base/device/main_fm1.c:1365-1370,1163-1200,1538-1543` |
| OCT− al encender | rescate del firmware oficial / otros; **fm1-base no lo implementa** en el runtime | ver riesgo R6 en PLAN.md |
| Último recurso | modo ROM "WL82 UBOOT1.00" + `fm1_rescue` / V15 oficial `FM-1_015.fwsc` | `base/bsp/tools/fm1_rescue.py`; fm1_open `THIRD_PARTY.md:20` |
