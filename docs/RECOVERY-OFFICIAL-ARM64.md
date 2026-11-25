# Fase 0.1. Reutilización privada del motor ARM64 oficial

## Conclusión

El motor 0.4 puede integrarse sin recompilación ni cambios en sus bytes para una prueba ARM64 en un dispositivo con páginas de 4 KiB. El paquete experimental fue construido y verificado localmente. Falta instalarlo y arrancarlo en Android. El motor presenta un bloqueo de arranque en sistemas de 16 KiB, explicado abajo. El código de recompilación por IPA sigue disponible para futuros cambios al runtime.

Se usó el APK oficial 0.4 publicado, SHA‑256 `75622eb6eb70edaeee21b520b64abe746d5e7df439fbbbf4b0fe7906aa471e00`, como referencia para el ensayo privado pedido. La publicación del original permite verificar la procedencia del archivo, mientras que el uso y distribución siguen sujetos a los derechos sobre el juego. El ensayo se entrega de forma privada. Git y Actions públicos solo reciben código, pruebas y metadatos de verificación. [Release oficial](https://github.com/Noamcelermajer/NOVAHD_sc/releases/tag/v0.4)

## ABI, arranque y datos

- ELF64 AArch64 little-endian. Sus dependencias dinámicas son `liblog.so`, `libandroid.so`, `libEGL.so`, `libGLESv1_CM.so`, `libaaudio.so`, `libdl.so`, `libm.so` y `libc.so`. Contrasté los 217 símbolos fuertes indefinidos con los stubs públicos AArch64 del NDK API 26. Todos tienen un nombre presente. La tabla de versiones declara `LIBC` para los símbolos versionados. El APK original declara min SDK 26, target 34 y GLES 1.1. En Android real hay que comprobar el driver GLES y AAudio.
- Los símbolos exportados incluyen `ANativeActivity_onCreate`, `android_main`, `rt_mem_init_from`, `rt_commit` y 21.230 símbolos de funciones traducidas `f_…`. La actividad Java `GameActivity` extiende `NativeActivity`. Android lee `android.app.lib_name=novahd`, carga `libnovahd.so` y usa la entrada predeterminada `ANativeActivity_onCreate`. El glue del NDK llama a `android_main`. Se mantiene el nombre de paquete `com.novahd.recomp`, que el runtime también usa al pedir el gestor de sensores.
- El runtime abre `image.bin` a través de `AAssetManager`. La imagen se almacena sin compresión y conserva su SHA‑256 `8800ee114204bca4313c80b3da3fb9e2c6a39575eda2eedc2324dd27b3c65709`.
- El original elige `ANativeActivity.externalDataPath` cuando está disponible y `internalDataPath` como respaldo. El bundle es `<files>/NovaHD.app`. El directorio de partidas y preferencias es `<files>/home`, con `Documents`, `Library`, `Library/Caches` y `tmp`. En el modo con motor oficial, Java usa `getExternalFilesDir(null)` o `getFilesDir()` con el mismo criterio. La instalación y reparación se limitan al bundle. Las descargas parciales y los archivos de importación se guardan en el área interna. El empaquetado omite `bundle.lst`, por lo que el extractor anterior deja que el gestor instale y verifique la campaña antes de abrir el juego.

## Límite de 16 KiB

`readelf -lW` muestra tres segmentos `PT_LOAD` con `p_align=0x4000` y desplazamientos congruentes con sus direcciones virtuales módulo `0x4000`. Esto satisface el alineado ELF. La biblioteca se extrae al instalarse, igual que en 0.4, por lo que el alineado ZIP para carga directa de una biblioteca sin extraer no interviene.

La ejecución tiene un requisito distinto. En el binario oficial, `rt_image_base` vale `0x1000`. El desensamblado de `rt_commit` en `0x5a7302c` muestra `and w19, w0, #0xfffff000` y una llamada a `mprotect` con `rt_mem + w19`. Al preparar la imagen, la primera llamada usa por tanto la base de `mmap` más `0x1000`. En un kernel con páginas de 16 KiB esa dirección no está alineada y `mprotect` la rechaza con `EINVAL`. La rama de error llama a `rt_fatal`. El gestor comprueba el tamaño de página con `Os.sysconf` y bloquea el arranque del motor oficial cuando el valor difiere de 4096. Habilitar 16 KiB requiere cambiar y volver a validar el manejo de memoria del runtime.

## Construcción privada

Con una copia autorizada del APK original y el entorno Gradle de Recovery, desde la raíz del repositorio:

```sh
python3 tools/recomp/stage_official_apk.py /ruta/original.apk app/src/main/assets/campaign.tsv --stage build/prebuilt/android
gradle :app:assembleRelease -PnovaOfficialEngine=true -PnovaNativeDir=../build/prebuilt/android
python3 tools/recomp/stage_official_apk.py app/build/outputs/apk/release/app-release.apk app/src/main/assets/campaign.tsv
```

El directorio de salida de la preparación debe ser nuevo. El script comprueba el hash del APK de origen y de cada entrada, y copia la biblioteca sin pasar por el recompilador. El workflow `prebuilt-private.yml` puede ejecutarse en un repositorio privado autorizado con el entorno `authorized-recovery` y el secreto `NOVA_OFFICIAL_APK_HTTPS_URL`. La URL tiene que entregar directamente el APK oficial. El workflow rechaza repositorios públicos y deshabilita la caché Gradle para ese trabajo. El workflow por IPA permanece en `build-apk.yml`.

## Resultados observados

| Etapa | Resultado |
|---|---|
| Compilación y empaquetado | Éxito local con Gradle 8.5, JDK 17 y SDK 34. Pasaron `lintVitalRelease`, la verificación de firma y `zipalign -c 4`. Paquete `com.novahd.recomp`, versión 5, ABI `arm64-v8a`, min SDK 26 y target 34. |
| Integridad del motor y recursos | `libnovahd.so` coincidió byte por byte con 0.4, SHA‑256 `ab6a0489e69bce594bd9eb32abb42eaf30b73ea577adbc7406b1c7ee9dcfc24c`. Pasaron la imagen y los 44 recursos. En la JVM el instalador extrajo la campaña desde el APK nuevo, detectó un paquete de nivel eliminado, lo reparó y volvió a verificar el conjunto. |
| Instalación en Android | Pendiente. ADB arrancó y no listó ningún dispositivo conectado. La firma local de prueba es distinta a la de 0.4. Android solo permite actualizar sobre la instalación original si se firma con la misma clave autorizada. Para conservar datos de la 0.4, prueba en un dispositivo separado si hay conflicto de firma. |
| Ejecución real | Pendiente en un teléfono ARM64 de 4 KiB. Conviene comprobar arranque en modo avión, primer nivel, sonido, controles y guardado tras cerrar y volver a abrir. La evidencia del fallo de 16 KiB es estática. |

APK experimental privado: `/workspace/shared/downloads/NOVA-HD-v0.5-Recovery-ARM64-4KiB.apk`. Tamaño 206.306.036 bytes. SHA‑256 `5b5efdeb540a481a7ffbf93ec64583aa638ce5905611866eb9aff342ef1729d0`.

Cambios de código en `app/build.gradle`, `AndroidManifest.xml`, `CampaignManager.java`, `ResourceManagerActivity.java`, `strings.xml`, el verificador de entradas `stage_official_apk.py` y los workflows. Las ramas de respaldo, `recovery/v0.5` y `development` se conservan.
