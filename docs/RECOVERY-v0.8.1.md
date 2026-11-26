# Recovery 0.8.1. Búsqueda de entradas de enlace

## Resultado

Todavía faltan las entradas del juego. En las referencias accesibles encontré **image.bin** y **los scripts originales**. Los 48 shards ARM64, el dispatcher reubicable, `image_meta.c` y los fuentes C generados no aparecieron. Se mantienen intactas las entregas 0.5, 0.6 y 0.7. No se generó un APK 0.8.1 ni se cambió el runtime aprobado en 0.8.

## Repositorios y procedencia

- Clon espejo de `Noamcelermajer/NOVAHD_sc` con las referencias ofrecidas por GitHub. Dos ramas (`development` y `feature/objc-runtime`), cuatro etiquetas (`v0.1`–`v0.4`) y 19 commits alcanzables. El inventario de rutas de esos commits contiene `tools/recomp/build_apk.py`, `build_runner.py`, `pack_image.py`, el traductor y fuentes del runtime, pero no `image_meta.c`, `image.bin`, shards/dispatcher generados ni objetos ARM64. Tampoco aparece un workflow en el historial revisado. La API de upstream devolvió cero workflows, ejecuciones y artefactos visibles. Las cuatro Releases ofrecen únicamente sus APK. Esto acota la búsqueda al material accesible; un checkout o una caché privados del autor todavía podrían contener los archivos. [Repositorio original](https://github.com/Noamcelermajer/NOVAHD_sc) · [Releases originales](https://github.com/Noamcelermajer/NOVAHD_sc/releases)
- `v0.4` apunta a `930cd60`. El script de esa revisión prepara `image_meta.c` e `image.bin`, compila los shards y dispatcher y enlaza esos resultados con el runtime ARM64. El APK oficial incluye la imagen y el ELF enlazado con las funciones traducidas. El flujo de fuente explica que esos objetos y metadatos son insumos de la construcción, aun si se reutilizaron desde caché. No hay log publicado que permita confirmar el comando exacto o dónde se conservaron. [Script de v0.4](https://github.com/Noamcelermajer/NOVAHD_sc/blob/v0.4/tools/recomp/build_apk.py)
- Los artefactos visibles de los dos repositorios de trabajo son el gestor, el diagnóstico y los paquetes de entrega 0.5, 0.6 y 0.7. No hay bundle de relink en esos listados. El SHA‑256 de la imagen extraíble y compatible de 0.4 es `8800ee114204bca4313c80b3da3fb9e2c6a39575eda2eedc2324dd27b3c65709`.

## Alternativas y pruebas

| Alternativa | Resultado |
|---|---|
| Reutilizar el ELF entero | Sirve como motor anterior en el POCO, según las pruebas de 0.5–0.7. No permite sustituir la memoria, el renderizador y el dispatcher como objetos independientes. |
| Extraer secciones y reconstruir objetos | El ELF es AArch64 ET_DYN; tiene `.dynsym`, carece de `.symtab` y está marcado BIND_NOW. Los datos observados son 22.830 RELATIVE, 26 GLOB_DAT y 21.564 JUMP_SLOT. Se extrajo `.text` localmente (90.824.320 bytes); son bytes sin cabecera ELF, límites originales de objetos o relocaciones de enlace estático. `readelf -h` rechazó esa extracción. El relinker también rechaza un ET_DYN aunque se llame `.o` y coincida su hash. Los exportados permiten localizar funciones; no restauran las referencias resueltas ni los símbolos locales. |
| Sustitución controlada | Es viable en fuente con objetos reubicables: el enlace sintético ARM64 de 0.8 verificó el contrato. Un hook de `mprotect` no traslada la base invitada ni preserva por sí mismo su guardia nula. `rt_commit` alinea internamente a 0x1000 antes de llamar a la PLT. En el archivo oficial no hay JUMP_SLOT de `rt_commit` para sustituir esa función. `rc_import` y `rc_dispatch` tienen JUMP_SLOT, pero sustituirlos requeriría reproducir estado, tablas y ABI del invitado. |
| Interposición gráfica | `eglSwapBuffers` y `eglGetProcAddress` tienen JUMP_SLOT; el desensamblado confirma que `rt_platform_swap` llama a `eglSwapBuffers@plt`. Parte de GLES pasa por `dlsym` con un handle explícito y luego por punteros. Una prueba sintética Linux interceptó una importación dinámica normal y conservó la implementación original al resolver con un handle explícito. Es evidencia del límite del método, no validación de preload, capas GLES ni namespaces del linker en el POCO. |
| Interposición de red | No hay relocaciones JUMP_SLOT a `socket`, `sendto` ni `recvfrom` de Bionic en el ELF. La red del invitado llega a `rc_import` y a los stubs en `offline.c`. Interponer la red del sistema no habilita esa ruta. Un adaptador fuente requerirá verificar ABI Darwin, descriptor/errno y los protocolos originales. |
| Instrumentación | En una compilación legítima, `--frame-log` mide el tiempo monotónico entre swaps exitosos y dimensiones EGL/FBO sin modificar el traductor. Para FPS realmente mostrados se requieren tiempos de presentación del compositor; investigar Perfetto FrameTimeline o `EGL_ANDROID_get_frame_timestamps` si el dispositivo y contexto los soportan. Una captura externa de presentación es diagnóstica y no obtiene automáticamente la resolución interna ni corrige el motor. No se han tomado mediciones del juego en esta fase. |

Para reproducir la inspección estática con una copia autorizada se extrae **solo localmente** `lib/arm64-v8a/libnovahd.so` mediante `unzip -p` y se ejecutan `llvm-readelf -hSW`, `llvm-readelf -dWr`, `llvm-readelf --dyn-syms -W` y `llvm-objdump -d --disassemble-symbols=rt_platform_swap,rt_commit,rc_import`. `llvm-objcopy --dump-section=.text=extracto.bin` permite verificar que la sección cruda no es un objeto; se trabaja sobre una copia temporal. Las pruebas Linux y de preflight se repiten con `bash tests/check-recovery-v081.sh`. El relink sintético con NDK se prueba con `bash tests/check-native-v08.sh`. Ninguna prueba ejecuta el ELF original en Android ni modifica un APK de respaldo.

## Qué conseguir

**Primera opción.** Pedir al autor del port una copia autorizada del build ARM64 correspondiente a **v0.4 / `930cd60`**. Debe contener los **48 objetos reubicables de shards, el objeto dispatcher, `image_meta.c` e `image.bin` del mismo build**, con checksums, modo checked o normal, NDK y revisión del header `recomp.h`. Alternativamente sirven los 48 C generados, dispatcher C, `protos.h`, header traducido y metadatos/imagen compatibles. El contexto de procedencia evita mezclar cachés de compilaciones distintas. La imagen ya está disponible; por sí sola no reconstruye los objetos ni su ABI.

**Segunda opción.** Una copia legítima del IPA de **N.O.V.A. HD para iPad 1.0.6** (`CFBundleIdentifier=com.gameloft.NovaiPad`, `CFBundleVersion=1.0.6`). El archivo dentro del bundle debe ser `Payload/NovaHD.app/NovaHD`, Mach‑O ejecutable thin de 32 bits little-endian, `CPU_TYPE_ARM=12`, subtipo ARMv6 `6`, `MH_EXECUTE=2`, con comandos de carga completos. Si incluye `LC_ENCRYPTION_INFO`, su `cryptid` debe ser cero. El procedimiento acepta una copia no cifrada que el propietario esté autorizado a utilizar. Requiere además los 44 archivos de campaña cuyos hashes ya constan en `campaign.tsv`. Una vez empaquetada, la imagen deberá coincidir con 0.4; el procedimiento estricto rechaza builds distintos antes de traducir. No se proporcionan fuentes de descarga del IPA.

## Importación automática y firma

Con Python 3.12, dependencias de `tools/recomp/recovery-requirements.txt`, SDK 34 y NDK `28.2.13676358`, desde la raíz del proyecto y usando rutas privadas:

```sh
python3 -m pip install -r tools/recomp/recovery-requirements.txt
python3 tools/recomp/import_recovery_ipa.py /ruta/privada/NovaHD.ipa \
  --sha256 "$SHA256_IPA_AUTORIZADO" --out build/recovery-import
```

El comando comprueba el hash autorizado, encabezado Mach‑O y cifrado, los 44 recursos y el hash de la imagen de referencia antes de compilar los shards. Publica localmente `build/recovery-import/relink-inputs` con manifest, objetos e imagen, y `build/recovery-import/android` con la biblioteca y assets para Gradle. Los resultados se preparan en un directorio temporal, y el destino final se publica tras éxito. Los cachés del traductor se guardan en directorios `build/` ignorados por Git. Las pruebas usaron IPA y Mach‑O sintéticos para verificar rechazos de digest, cifrado y truncamiento; la ruta completa con datos legítimos queda pendiente.

El job manual `complete-apk` debe ejecutarse solo en un repositorio privado autorizado con el código de esta rama, un URL HTTPS proporcionado por el usuario y su SHA‑256. Los secretos incluyen `NOVA_IPA_HTTPS_URL`, `NOVA_IPA_SHA256`, `NOVA_SIGNING_KEYSTORE_B64`, `NOVA_SIGNING_STORE_PASSWORD`, `NOVA_SIGNING_ALIAS` y `NOVA_SIGNING_KEY_PASSWORD`. Utiliza versión 8 con el mismo paquete y verifica el certificado experimental `ce235f8d30539f6d344c692983caf309dde2bdb915120c23d0ba23797076d035` antes de publicar. El APK y el conjunto de relink serían artefactos separados del repo privado. No se ejecutó ese job sin los archivos legítimos. El motor recompilado usa el bundle privado gestionado y mantiene la ruta existente de partidas; el gestor contempla la migración de recursos heredada. Todavía se debe validar la actualización y las partidas en el POCO antes de declarar desbloqueo funcional.

La hoja de ruta mantiene todos los objetivos de motor para después del desbloqueo. La siguiente fase requiere aprobación.
