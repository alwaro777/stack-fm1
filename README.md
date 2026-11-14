# STACK

![The RAVE demo building up on the FM-1's screen](docs/img/stack-build.gif)

> ⚠️ **Beta — instálalo bajo tu responsabilidad · install at your own risk.** Probado en simulador y emulador; la
> prueba en un FM-1 real está pendiente ([docs/HARDWARE-TEST.md](docs/HARDWARE-TEST.md)). Tested in the simulator
> and the emulator; testing on a real FM-1 is pending.

## Español

**STACK** es un firmware open-source para el **M-VAVE FM-1** que lo convierte en una groovebox de 8 pistas para rave,
dub y acid. Cada pista con pasos trae una caja de altavoz al *sound system* de la pantalla: subs abajo, medios en
medio, tops arriba; los conos laten con cada golpe.

- **Sonido**: 36 presets sintetizados (sin samples): percusión analógica modelada (808, 909, gabber, caja, palmas,
  hats, rim, tom, cowbell, shaker, ride, crash), bajos (acid 303, sub, reese, FM), leads (hoover, rave stab, square,
  pluck, acid), FM (e-piano, campana, mallet), pads con acordes y memoria de acordes, y efectos (riser, downlifter,
  sirena dub, láser, sweep, impacto, vinilo).
- **Secuenciador**: 1–64 pasos por pista, polimetría, escalas de tiempo y direcciones; por paso nota, velocidad,
  gate, probabilidad, ratchet, micro-timing, condiciones, accent, slide y **p-locks**; swing, fill, mute/solo,
  grabación en vivo cuantizada; **16 patrones**, cadenas y canción; escalas.
- **Bus master**: delay dub ping-pong sincronizado, reverb, *pump* desde el kick, compresor y filtro DJ.
- **MIDI por USB**: notas por canal de pista, reloj de entrada y salida, start/stop.
- **Guardado** en la flash del FM-1 y **exportar/importar** el proyecto como fichero.

### Probarlo sin FM-1

El emulador web ejecuta el mismo código que el FM-1. Tras la primera release estará en
<https://alwaro777.github.io/stack-fm1/emu/>; mientras, en local:

```bash
make web && make serve      # http://localhost:8017/
```

**Switch on**, una tecla, **PLAY**. **HOME + PLAY** construye una demo pista a pista (RAVE 138, DUB 140, GABBER 180).
Con el ratón: arrastra o usa la rueda en los knobs; mayúsculas-clic mantiene pulsada una tecla. Los botones de
*backup* del emulador exportan e importan el proyecto; las demos están en `build/demos/` (`make demos`).

### Instalar en el FM-1

1. Desde la web del proyecto (Chrome o Edge, FM-1 por USB): **Install**. O por línea de comandos:
   `python3 base/tools/fm1_install.py stack-X.Y.fwsc`.
2. **Si algo falla: mantén REC al encender** — modo de actualización del chip; reinstala STACK, otro firmware o el
   oficial de M-VAVE. Dos arranques fallidos seguidos activan el modo seguro (USB activo, sin sonido).
3. STACK nunca toca el bootloader ni el update loader (vienen intactos de felucca-bsp a través de fm1-base).

Nota: el rescate "OCT− al encender" de otros firmwares no existe en fm1-base; usa REC.

### Controles

El [manual](app/docs/MANUAL.md) lo explica todo; en resumen:

| Control | Hace |
|---|---|
| Teclas blancas | los 16 pasos de la página; mantener + knobs edita el paso; LFO + paso + knobs = p-locks |
| Negras 1–8 | elegir pista (en el sound system: mute; HOME + tecla: solo) |
| Negras 9 / 10 / 11 | página / FILL / modo teclado |
| PLAY · REC · SAVE | play-stop · grabar en vivo · guardar |
| HOME · SEQ · EDIT · ENV · FX · ARP · SEL · GLO | sound system · secuenciador · sonido · envolvente · efectos · patrones y canción · escala · global |
| HOME mantenido | SHIFT: copiar, pegar, borrar, deshacer, demo, ayuda |
| SELECT · ALGO · PRESET · K1–K4 | pista · página de knobs · sonido · parámetros |

## English

**STACK** is open-source firmware for the **M-VAVE FM-1** that turns it into an 8-track groovebox for rave, dub and
acid. Every track with steps brings a speaker cabinet to the on-screen sound system — subs at the bottom, mids in the
middle, tops on top — and the cones push with every hit.

It has 36 synthesized sounds (no samples), a sequencer with per-step locks, probability, ratchets, conditions and
nudge, 16 patterns with chains and a song mode, scales, a dub delay, reverb, kick-driven pump, compressor and DJ
filter, USB MIDI (notes, clock, start/stop), saving to the FM-1's flash and project export/import.

**Try it**: `make web && make serve`, then http://localhost:8017/ — switch on, press a key, PLAY; HOME + PLAY builds a
demo up track by track. **Install**: the project site's installer (Chrome or Edge) or `base/tools/fm1_install.py`.
**If anything goes wrong, hold REC while switching on** for the chip's update mode. The [manual](app/docs/MANUAL.md)
covers every control.

## Development · Desarrollo

| Command | What |
|---|---|
| `make test` | unit tests (sequencer timing, DSP, serialization, UI snapshots, audio goldens) + the app in fm1-base's host simulator (save/reboot, backup/restore, MIDI) |
| `make web` · `make serve` | the browser emulator (Emscripten 6.0.10) |
| `make demos` · `make gif` | the demo projects as backup files · the README's GIF, both made by the firmware in the simulator |
| `./build.sh` | the FM-1 package (JieLi toolchain + AC79 SDK: CI does it, `.github/workflows/release.yml` on `zvenson/fm1-build@v1`, attested for The Firmware Hub) |

Layout: `app/core` (sequencer, engines, mixer, effects, project format — portable C, fixed point, no malloc, no libm),
`app/ui` (4-bit framebuffer, views, the sound system), `app/app.c` (the glue to fm1-base's `plat.h`), `base/`
(fm1-base, with felucca-bsp), `tests/`, `tools/`. Design and hardware notes: [docs/PLAN.md](docs/PLAN.md),
[docs/HARDWARE.md](docs/HARDWARE.md).

## Licence · Licencia

GPL-3.0-or-later ([LICENSE](LICENSE)). Built on fm1-base and felucca-bsp (MPL-2.0), from Felucca by Leo Kuroshita,
Hügelton Instruments; see [THIRD_PARTY.md](THIRD_PARTY.md). Not affiliated with M-VAVE; no real sound system's name or
logo is used.
