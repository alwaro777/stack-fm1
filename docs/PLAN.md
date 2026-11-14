# STACK — Plan

Groovebox open-source para el M-VAVE FM-1 cuyo elemento central es un sound system pixel-art que crece con el patrón.
Datos de hardware y fuentes: [HARDWARE.md](HARDWARE.md). Referencia visual: `reference/kawa-soundsystem.jpg` (solo
inspiración: ningún logo ni nombre de ese sistema aparece en el firmware).

## Decisión de base: app sobre fm1-base (submódulo `base/`, con felucca-bsp en `base/bsp/`)
Cambio respecto al borrador aprobado (que proponía felucca-bsp + herramientas de Felucca + emulador propio): durante la
Fase 1 apareció [fm1-base](https://github.com/charlesvestal/fm1-base), que ya es exactamente eso, mantenido:
- **Licencia**: todo fm1-base es MPL-2.0 (por fichero); una app encima puede ser GPL-3.0. Sin binarios cerrados: el
  FM vendrá de msfa (Apache-2.0) portado a entero, no de `libfm1_engine.a`.
- **Mismo código en tres sitios**: la app solo ve `plat.h`; corre sin cambios en el FM-1, en el simulador host
  (scripts, PNG, WAV, `expect`) y en el emulador web (Emscripten + AudioWorklet).
- **Build reproducible y Hub**: `base/tools/build.py` + plantilla de CI con `zvenson/fm1-build@v1` (badge "built from
  source"), identidad `FM-1_` + dígitos, comprobaciones de RAM code, `double`, MMIO, flash chips.
- **Seguridad**: update loader y bootloader son los de felucca-bsp, sin tocar.
- **Descartados**: fork de fm1_open (motor cerrado, build solo Windows, su web corre otro motor); módulo fm1-dock
  (pantalla compartida con el host, se publica el dock); Felucca directo (GPL monolítico, sin simulador desde 2026-11-04).

## Arquitectura
```
app/                 la app (APP_DIR de build.py)
  fm1.toml app.h     configuración y contrato con la plataforma
  app.c              pegamento plat.h ⇄ core/ui (entrada, flush de bandas sucias, render) — es nuestro "hal"
  core/              C99 portable, sin HW ni malloc: secuenciador, motores, voces, mezclador, FX, modelo, serialización
  ui/                framebuffer 4 bpp indexado 240×240 (28,1 KiB), paleta 16 colores, fuente pixel propia, vistas
base/                fm1-base (submódulo fijado) = hal/fm1 (device/), hal/host (host/), hal/web (web/emu)
tests/unit/          tests de core/ui con `cc` puro (sin plataforma), snapshots por hash del framebuffer
app/tests/           scripts del simulador host (botones, teclas, capturas PNG, WAV)
```
- **Audio**: Q15/Q24 en entero dentro de `core/` para que web/host/HW den el mismo resultado bit a bit (tests golden
  por hash). `APP_BLOCK` 256; motores en sub-bloques de 32 (tasa de control). Ruta de audio en `PLAT_RAM_CODE`.
- **UI**: `ui/` dibuja en su framebuffer 4 bpp y marca bandas de 16 filas sucias; `app.c` convierte solo esas bandas a
  RGB565 big-endian y las envía con `lcd_blit`. Mismo framebuffer ⇒ snapshot trivial.
- **Comunicación UI→audio**: cola SPSC sin bloqueo (notas, triggers) + parámetros atómicos de 32 bits.
- **Concurrencia**: audio en IRQ (prioridad sobre la UI); la UI nunca bloquea al audio. `app_tick` (1 kHz) para el
  reloj del secuenciador desde F2. Segundo core solo si el presupuesto lo exige (F6).
- **8 pistas**: como fm1_open/Felucca/hortator, encaja con las 8 primeras teclas negras y con el layout.
- **Voces**: pool estático de 16; drums 1–2 por pista, melódicas hasta 4. Robo determinista: misma pista → liberada
  más antigua → más antigua.
- **Estado**: 16 patrones × 8 pistas × 64 pasos (8 B/paso) + p-locks + kits + song; registro versionado (magic +
  versión + CRC) en el área de datos de flash y como fichero `.stk` exportable desde el emulador.

## Mapa de controles
| Control | Vista HOME / SEQ | Con SHIFT (mantener HOME) |
|---|---|---|
| 16 teclas blancas | pasos 1–16 de la página actual | — |
| Negras 1–8 | seleccionar pista (en HOME: mute) | solo |
| Negra 9 / 10 / 11 | PAGE (16→64) / FILL momentáneo / modo KEYS↔STEP | — |
| Teclas en modo KEYS | 27 teclas cromáticas para la pista; OCT± transpone | — |
| PLAY / REC | play-stop / grabación en vivo cuantizada | — / limpiar pista |
| HOME | vista sound system | (SHIFT) |
| SEQ / EDIT | grid ↔ piano-roll / página de pista | copiar / pegar |
| ENV / LFO | envolvente / p-locks (mantener LFO + paso) | — |
| FX / SCL / GLO | FX globales / escala-tónica / BPM-swing-MIDI-clock | — / — / ayuda (overlay) |
| ARP / SAVE | patrón-cadena-song / guardar | — / undo |
| SELECT / ALGO / PRESET | pista o cursor / página de parámetros / preset y categoría | — |
| K1–K4 | parámetros de la página (valor + barra); en HOME: BPM, swing, filtro DJ, pump | ajuste fino |
| MASTER | volumen (la plataforma lo aplica) | — |

Regla ≤2 pulsaciones: mute/solo (1–2), cambiar paso (1), preset (girar PRESET), BPM (K1 en HOME), parámetro de pista
(EDIT + knob), p-lock (mantener LFO + paso + knob, gesto simultáneo), patrón (ARP + tecla).

## Sound system (240×240)
Cabecera 12 px (BPM, patrón, PLAY/REC) · escenario ~200 px · pie 14 px (16 pasos de la pista seleccionada).
Categorías y cajas: SUB 56×56 (máx 4/fila) abajo, MID 48×44 (máx 4) en medio, TOP 40×32 (máx 5) arriba; orden por
índice de pista; si una categoría desborda, apila otra fila de la misma ⇒ pirámide determinista. Sprites PNG originales
→ 4 bpp con `tools/sprites.py`; conos con 3 fotogramas de excursión ∝ velocidad (solo se redibuja la caja); entrada con
caída y rebote (tabla de easing fija); mute = gris, solo = resto atenuado, selección = marco parpadeante; rejillas con
dithering ordenado; scanlines opcionales; flightcase decorativo.

## Presupuesto (objetivos; se miden en F2–F6)
Bloque de 256 frames a 360 MHz ≈ 2,09 M ciclos (≈8 160 ciclos/muestra).
| Elemento | CPU objetivo | RAM |
|---|---|---|
| 8 pistas drums / acid / subs | ≤30 % | ~8 KiB |
| FM (msfa entero, ≤6 voces) | ≤25 % | ~12 KiB |
| supersaw / pads (≤4 voces) | ≤15 % | ~4 KiB |
| FX (delay dub, reverb, comp, filtro DJ) | ≤15 % | delay ~44 KiB (16 bit, 0,5 s) + reverb ~16 KiB en `PLAT_POOL` |
| UI (fuera de IRQ) | — | framebuffer 28 KiB + banda RGB565 7,5 KiB |
Margen ≥15 %. Si no cabe: menos voces FM, reverb a media tasa, o `cpu2`.

## Fases
- **F1** esqueleto: repo, docs, splash flyer, vista de prueba de I/O, emulador web, simulador host + tests, CI.
- **F2** secuenciador + drums + acid; audio estable en el emulador.
- **F3** sound system completo (criterio: kick → sub; hat → top; mute → caja apagada; triggers a 140 BPM sin lag).
- **F4** resto de motores y presets, FX globales, sidechain.
- **F5** patrones/song, MIDI, guardado en flash, exportación.
- **F6** optimización en hardware, tests completos, release `.fwsc`, README ES/EN, demo pública.

## Riesgos
| # | Riesgo | Mitigación |
|---|---|---|
| R1 | Coste real de cada motor desconocido | medir con `plat_cpu_pct` / `plat_cycles*` por fase en hardware |
| R2 | SPI lento | dirty bands; solo cajas que cambian |
| R3 | fm1-base es beta y cambia rápido | submódulo fijado; actualizar a propósito |
| R4 | El Hub exige prueba en un FM-1 real | flashear con confirmación del usuario antes de publicar |
| R5 | Sin velocidad de teclas por defecto | velocidad por paso; `APP_TOUCH` como opción |
| R6 | "OCT− al encender" no existe en fm1-base (su rescate es REC al encender + modo seguro) | documentar REC; valorar pedir OCT− upstream |
| R7 | BLE + WiFi + RAM | sin BLE en F1–F4; activarlo en F5 si la RAM lo permite |

## Estado por fase

### F2 — secuenciador + drums + acid (hecho)
- Reloj a 96 PPQN con duración de tick en Q16 (sin deriva; precisión ±1 muestra), render troceado en fronteras de tick.
- Por pista: 1–64 pasos, escala 1/32–1/4 y tresillos, dirección FWD/REV/PING/RAND, polimetría. Por paso: on, nota,
  velocidad, gate (1/16 de paso), probabilidad (dados con semilla: misma secuencia en cada PLAY), ratchet ×1–4,
  nudge ±11/24 de paso (también hacia atrás, gracias al look-ahead de medio paso), condiciones (FILL, 1ST, A:B),
  accent y slide. Global: BPM 20–300 con décimas, swing 50–75 %, mute/solo, FILL, grabación en vivo cuantizada,
  copiar/pegar/limpiar pista y un nivel de deshacer.
- Motores en punto fijo con productos de 32 bits (ver felucca-bsp docs/CPU.md): percusión modelada (808, 909, gabber,
  caja, palmas, hats con choke, rim, tom, cowbell) y bajo acid (saw/square polyBLEP o seno, filtro de 4 polos 24/18 dB
  con resonancia y tanh, envolvente de filtro, accent, slide). Constantes precalculadas (`tools/gen_tables.py`): sin
  libm, mismo audio bit a bit en host, web y FM-1.
- Pendiente de fases siguientes: p-locks (F4/F5), piano-roll (F3/F5), MIDI y reloj externo (F5), patrones y canción (F5).

Medidas (host, Apple Silicon; el FM-1 sin medir todavía):
| Caso | Host | Estimación FM-1 (360 MHz, 20–40× más lento) |
|---|---|---|
| Demo (6 pistas) | 0,42 % de tiempo real | ~8–17 % de un core |
| Peor caso (8 pistas, 1/32, ratchet ×4, accent) | 1,1 % | ~22–45 % |
| RAM propia | proyecto 4,4 KiB + voces/colas ~3 KiB + framebuffer 28 KiB + 2 bandas 15 KiB | |

### F3 — el sound system (hecho)
- Vista principal tras el flyer y con HOME. Cada pista con al menos un paso activo trae su caja; patrón vacío =
  escenario vacío (suelo, pancarta "STACK HI-FI" original, torre de truss, flightcase con pegatinas).
- Layout determinista: filas de abajo arriba SUB (50×52, 4 por fila), MID (44×40, 4), TOP (36×28, 5); una categoría
  que desborda apila otra fila suya; filas centradas en un eje a la izquierda del flightcase; pistas en orden.
  La categoría viene del preset y se cambia en la página de EDIT (CAT).
- Animación: caída con rebote (520 ms), recolocación con ease-out (260 ms), salida hacia arriba al vaciarse la pista.
- Reactividad: el cono sale 1–3 niveles según velocidad/accent y vuelve en pasos de 45 ms; mute = caja gris;
  solo = las demás en sombra tramada; pista seleccionada = marco lima parpadeante (300 ms); cincha amarilla en la fila
  de subs. Cabecera: patrón, BPM, play/rec. Pie: nombre y 16 pasos de la pista seleccionada con playhead.
- Scanlines opcionales (GLOBAL, página 2).
- Rendimiento: fondo cacheado (28 KiB en el pool) + cajas; un frame de UI cuesta ~39 µs en el host. Envío a pantalla
  por baldosas de 16×16: a 140 BPM 256 KB/s frente a 977 KB/s por franjas completas (~17 % de un SPI de 12 MHz,
  velocidad aún sin medir en el FM-1). Latencia disparo → cono: ≤ 1 frame (16 ms), comprobada en test.
- Criterios de aceptación cubiertos por tests (`test_stack`, `test_stack_latency`): kick → SUB; hat → TOP; mute →
  caja apagada; 8 de 8 kicks visibles en el frame siguiente a 140 BPM.
- No hecho (opcional en el encargo): auto-clasificar la categoría por centroide espectral. El número de patrón es fijo
  (P01) hasta la Fase 5.

### F4 — motores, biblioteca y FX globales (hecho)
- **Pool de voces** (16, hasta 4 por pista) para los motores polifónicos, con robo determinista: libre → la más
  antigua liberada de la pista (si está en su límite) → su más antigua → la más antigua liberada → la más antigua.
- **Sinte VA**: SAW, SQUARE, SUPER (5 sierras desafinadas), PWM, SINE; DETUNE (supersaw / segundo oscilador que deriva
  con un LFO lento: ensemble tipo chorus en pads); filtro de estado variable TPT (LP/BP/HP, estable con resonancia
  alta); ADSR; modelos POLY, MONO (glide), HOOVER (supersaw + sub, entra una quinta por debajo), CHORD.
- **Acordes**: tabla (MAJ, MIN, MIN7, MAJ7, DOM7, SUS4, quintas, MIN9, octava) y **memoria por pista**: en modo KEYS
  mantener notas y pulsar LFO guarda el acorde.
- **FM propio** (no el binario cerrado): dos pares modulador→portadora, realimentación; E.PIANO, BELL, MALLET, FM BASS.
- **Motor FX**: RISER, DOWNLIFTER, DUB SIREN, LASER, SWEEP, IMPACT, VINYL.
- **Percusión nueva**: SHAKER, RIDE, CRASH.
- **Biblioteca**: 36 presets en 6 grupos (DRUMS 13, BASS 5, LEAD 5, KEYS 3, PAD 3, FX 7).
- **Bus master**: envíos post-fader por pista a un delay dub ping-pong sincronizado (1/16–1/2, 0,74 s máx.) y a una
  reverb de diseño Freeverb; pump desde los kicks (incluso silenciados en nivel); compresor (umbral −18 dB, 4:1
  mezclado por AMOUNT, con make-up); filtro DJ de un knob. Ajustes neutros = audio idéntico bit a bit (test).
- **UI**: vista FX (3 páginas), páginas de knobs por motor (hasta 6), grupo del preset en EDIT, K3/K4 del sound system
  = filtro DJ y pump.
- **Medidas**: en Apple M1 con -O2, demo 0,33 % del tiempo real; peor caso pesado (dos stabs supersaw de 4 voces, FM,
  hoover, todos los envíos, pump, compresor, DJ, ratchets ×4 a 1/32) 1,2 %. Estimación FM-1 (25–50× más lento):
  demo ~8–17 %, peor caso ~30–60 % de un core. **Sin medir en hardware**: si no cabe, en F6 se reduce la supersaw
  a 3 sierras, la reverb a media tasa o se usa el segundo core.
- **RAM (estimada)**: pool de fm1-base: framebuffer 28,1 + fondo 28,1 + bandas 15 + delay 128 + reverb 24 ≈ 223 KiB
  de 336; .bss propio ≈ 20 KiB.
- Pendiente: p-locks (F5, con patrones y guardado).
