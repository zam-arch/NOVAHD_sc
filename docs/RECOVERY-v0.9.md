# Recovery 0.9 · Recompilación ARM64

La base de esta rama es `7330885`. En el POCO F8 Pro con Android 17, Adreno 830 y páginas de 4096 bytes, el usuario comprobó arranque, carga de partidas, combate, cinemáticas y transición de niveles en 0.8.2. Observó una ventana nativa de 2510 × 1156, acceso a las exportaciones de swap y registro musical, e importación de M4A. Las pestañas musicales estaban vacías. Las dimensiones corresponden a la ventana, mientras que la resolución interna requiere consultar el FBO del motor.

## Entrada verificada

| Elemento | Resultado |
|---|---|
| IPA de la Release privada `private-input-v1` | 170.155.866 bytes, CRC de ZIP correcto, SHA-256 `8fa64d2169ba9e4f264c708872ddf30151ed14455be48f7937907a582a956772` |
| Info.plist | `com.gameloft.NovaiPad`, `CFBundleVersion=1.0.6`, `MinimumOSVersion=3.2`, `UIDeviceFamily=[2]`, ejecutable `NovaHD` |
| Mach-O | `Payload/NovaHD.app/NovaHD`, 4.445.984 bytes, SHA-256 `a50e03be4b305e965cb4b52a2c7194bcffa0e232418431158dfaffefa0627b18`, ejecutable thin little-endian ARMv6 (12/6/2), 28 comandos, `cryptid=0` |
| Segmentos (dirección y tamaño virtual) | `__PAGEZERO 0x0+0x1000`, `__TEXT 0x1000+0x3c2000`, `__DATA 0x3c3000+0x3f000`, `__LINKEDIT 0x402000+0x42000` |
| Imagen en memoria | `0x1000+0x44f000`, SHA-256 `8800ee114204bca4313c80b3da3fb9e2c6a39575eda2eedc2324dd27b3c65709`, igual al APK 0.4 de referencia |
| Metadatos | SHA-256 de `image_meta.c` `87a042f65b876d99b24e0c534f01a6911df72f16b8f72f94e987f31ca1afcb13`. 484 imports, 5.578 palabras enlazadas, 46 constructores, `main=0x63f0`, 21.230 entradas de despacho |
| Campaña | 44 archivos con tamaños y SHA-256 idénticos a `campaign.tsv` |

`macho.py`, `lift.py` y `pack_image.py` coinciden con `930cd60`. La generación de C de `difftest.py` mantiene el traductor y sus flags. Se separaron la emisión y la compilación después de que el límite de 8 GiB terminara el primer proceso por falta de memoria. La comparación con la 0.4 verifica bytes de la imagen invitada y no presupone un ELF idéntico. El procedimiento usa NDK 28.2.13676358 y destino aarch64-linux-android26.

## Reproducción privada

Con SDK 34, NDK 28.2.13676358, Gradle 8.5 y JDK 17 en Linux, use un workspace autorizado, un directorio privado nuevo y el almacén de firma experimental existente. Descargue el IPA mediante el CLI autenticado y valide el SHA antes de iniciar. El flujo deja el caché de fuentes C en `translation/` y el manifiesto de objetos verificados en `reference/relink-inputs/`.

```bash
umask 077
scratch=$(mktemp -d)
gh release download private-input-v1 -R zam-arch/NOVAHD-private-test \
    --pattern '*.ipa' --output "$scratch/original.ipa"
mkdir "$scratch/reference-src"
git archive 930cd60 runtime | tar -x -C "$scratch/reference-src"
python3 -m pip install -r tools/recomp/recovery-requirements.txt
python3 tools/recomp/import_recovery_ipa.py "$scratch/original.ipa" \
    --sha256 8fa64d2169ba9e4f264c708872ddf30151ed14455be48f7937907a582a956772 \
    --runtime-dir "$scratch/reference-src/runtime" --translation-dir "$scratch/translation" \
    --out "$scratch/reference"
bash tools/recomp/relink_managed_android.sh "$scratch/reference" "$scratch/candidate"
gradle :app:assembleRelease -PnovaNativeRecovery=true -PnovaRebuiltEngine=true \
    -PnovaWindowProbe=true -PnovaNativeDir="$scratch/candidate/android"
```

Para futuros enlaces, conservar el conjunto de objetos, `relink.json`, `image_meta.c` e `image.bin` juntos. El manifiesto comprueba los hashes, la arquitectura, el NDK y el ABI del traductor. La firma requiere el certificado experimental SHA-256 `ce235f8d30539f6d344c692983caf309dde2bdb915120c23d0ba23797076d035`.

## Alcance

La compilación de referencia usa los fuentes del runtime en `930cd60`. La variante gestionada habilita las correcciones 0.8 de profundidad y memoria y activa la telemetría de swaps de 0.8 por log. El APK planificado usa la sonda opcional de 0.8.2 y muestra el nombre de versión real. La campaña se valida o migra al directorio interno. El runtime conserva `home` bajo la ruta de archivos anterior. La variante de pruebas mantiene el límite de páginas de 4 KiB y deja las pruebas de 16 KiB para una validación separada.

La cadencia de `eglSwapBuffers` y el intervalo entre retornos exitosos se registran en el log, junto con el tamaño EGL y el FBO intermedio cuando está activo. Los FPS presentados requieren timestamps del compositor. El bucle del runtime está programado a 60 Hz y llama a `rt_uikit_frame` en cada tick. Cambiar el periodo sin estudiar temporizadores, simulación y animaciones podría cambiar el comportamiento.

La aplicación musical cuenta actividades iniciadas y pausa tras 250 ms sin actividad visible. Para un fallo al entrar en campaña hay que separar un hueco entre actividades de un evento de pérdida de foco de audio. El resultado M4A confirma importación. Restaurar las pestañas requiere implementar consultas, objetos de medios, colas y notificaciones en el runtime antes de conectar el backend Android al controlador original. El audio del juego tiene su flujo existente.

Respaldos verificados por SHA-256.

- 0.5 `5b5efdeb540a481a7ffbf93ec64583aa638ce5905611866eb9aff342ef1729d0`
- 0.6 `4f24fc1eb9115f4c3ea5b93e398bc737723f93d5294fd6203c157fb685b0325e`
- 0.7 `ed472ffe2d742d78e32d04cadadaf563a0909519a81811c2d3a1fa03c11f4e6d`
- 0.8.2 `f38549ea2c32bb3a58541acf617611d37c295a61419598d8fdd97cf2184d3529`
