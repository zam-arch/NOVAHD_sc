# Recovery 0.13 · Encuadre de referencia y reproductor original

Base de código `145ede0`. Fuente del APK `d646fd8`. El usuario probó 0.12 en el POCO F8 Pro y comunicó que los ajustes no producían una mejora visual clara y que el reproductor había mejorado parcialmente. La validación física de 0.13 sigue pendiente.

## Encuadre y controles

| Versión | Transformación que podía recibir el mismo elemento de interfaz |
|---|---|
| 0.10 | Ampliación de hasta 1,6× en lotes compactos de borde condicionada por una señal temporal del HUD. |
| 0.11 | Retirada de esa ampliación. El usuario observó botones y textos pequeños e inconsistencias del minimapa. |
| 0.12 | Clasificación por geometría, esquinas y región de scissor. Los porcentajes se aplicaban solo a los lotes clasificados. |
| 0.13 | El modo original, seleccionado por defecto, presenta la capa lógica de iPad girada y centrada como una imagen de 1024 × 768. El juego establece sus propias matrices, viewports y recortes. Se conserva el modo panorámico de comparación con los porcentajes predeterminados de 0.12. |

El ajuste de encuadre se guarda antes de entrar al juego. La configuración JNI se aplica antes de que NativeActivity arranque el hilo. En referencia, `rt_gl_wide_long` vale cero incluso en un teléfono panorámico. El FBO conserva los ejes originales de 768 × 1024 y la presentación respeta su aspecto. En una superficie de 1668 × 768 el rectángulo de salida ocupa 1024 × 768 desde x=322. El dibujo final y la transformación inversa del toque usan el mismo cálculo. Un toque iniciado en la banda se ignora y un dedo que sale de la imagen puede terminar su gesto. Se han retirado ambos selectores de escala. La opción panorámica usa el comportamiento de comparación con sus valores iniciales aunque existan preferencias personalizadas de 0.12.

Las pruebas de geometría comprueban el rectángulo en 1668 × 768, la alternativa panorámica y la composición en una superficie cuadrada. El fixture anterior también comprueba anclajes, scissor y la inversa panorámica hasta 21:9. Estas pruebas no reproducen el diálogo, los recursos gráficos visibles ni el digitalizador físico. La referencia conserva toda la escena 4:3, incluidas las proporciones entre HUD y mundo. No se ha modificado la proyección 3D panorámica ni separado cada elemento de la interfaz en ese modo.

## Evidencia estructural

Se contrastaron el Mach-O, las direcciones del `image.bin` con base 0x1000 y funciones de los shards C privados. Direcciones en hexadecimal del ejecutable invitado.

| Subsistema | Evidencia | Alcance |
|---|---|---|
| Menú | La carga en `0xa2c44` dentro de `0xa2c1c` referencia MenuButtons.cpp, junto a comprobaciones de índice. | Identifica manejo de botones de menú. |
| Pestañas y tablas | `0x13a8b0` referencia TabControl.cpp y `0x142860` referencia Table.cpp. | Identifican subsistemas de la interfaz. Por sí solas las referencias no demuestran qué tabla aparece en pantalla. |
| Cinemáticas | Referencias a Cinematic.cpp en `0x82f50` y CinematicManager.cpp en `0x88dc8`. `0x162c4` carga el nombre CINEMATIC_BAND_HEIGHT. | Identifican gestión cinematográfica y un parámetro de bandas. |
| Mira | `0x14c50` y `0x14c94` referencian PLAYER_FIRE_CROSSHAIR_COOL_TIME. | Identifican un parámetro temporal de la mira. |
| Música | `0x1252d8` y `0x125954` llaman al selector de filas `0x127f3c`. En la ruta de selección, `0x12550c` llama a `0x127ec4` para copiar el rótulo y `0x12551c` guarda el índice. En el bucle de filas, `0x125bc8` solicita el texto y `0x125bec` llama a `0x1bfd60`. | Se identificaron consumidores en el menú, la composición de texto y el guardado de la selección. El último draw de glifos y el icono de pausa requieren correlación en pantalla. |

Para salud, munición, minimapa, botones de campaña, objetivos y subtítulos de Yelena falta vincular el productor de cada elemento con el lote remitido al dibujante común. La dirección de retorno del draw y el rectángulo de una textura no identifican por sí solos al productor. El modo de referencia permite aislar la transformación panorámica aun sin ese vínculo. No se añadieron nuevas reglas por tamaño o posición ni cuatro controles que dependieran de ellas.

## Música

La selección nativa permanece visible mientras Android confirma el ID. Después de la confirmación, una transición automática puede volver a actualizar la pista actual que devuelve `nowPlayingItem`. En 0.12, seleccionar mientras se reproducía música podía dejar activa la marca de selección y mantener el rótulo anterior tras avanzar. Al seleccionar una pista diferente con la anterior en pausa, Android libera el decodificador anterior y conserva el nuevo ID sin iniciar otra reproducción. Se mantienen la misma instancia de backend, las preferencias musicales y el acceso al audio original.

Se ejecutaron en Linux las funciones C traducidas originales `0x128648`, `0x128840` y `0x128f68` con la imagen privada, el despacho de métodos Objective-C del runtime y sus clases de música. El ensayo inicializó los globales de menú que normalmente configura la aplicación, usó una pista de muestra y sustituyó la salida de audio. Los búferes de texto del invitado devolvieron exactamente los siguientes valores.

| Entrada del menú | Resultado leído |
|---|---|
| Canciones, primera fila | 1. Luna - Sol |
| Artistas, primera fila | Luna |
| Listas, primera fila | Favoritas |
| Pista actual | Luna - Sol |

El ensayo comprueba la salida del código que prepara el menú. El dibujo final, la selección táctil, el sonido, el icono de pausa y el regreso a la banda sonora deben comprobarse en Android. El test del puente comprueba, además, seleccionar A mientras reproduce B, recibir confirmación y avanzar otra vez a B.

## Compilación y entrega

Se reutilizaron los 48 shards y el dispatcher privados. El conjunto de 21.230 símbolos traducidos coincide con la referencia. El APK contiene el motor ARM64, la sonda, la imagen esperada y los 44 archivos de campaña verificados. La huella de firma experimental y el paquete permiten intentar la actualización sobre 0.12. La carga de partidas debe validarse en el teléfono.

| Campo | Valor |
|---|---|
| Versión / versionCode | 0.13-recovery-arm64-4k / 14 |
| Paquete | com.novahd.recomp |
| Tamaño del APK | 206.174.820 bytes |
| SHA-256 APK | `0a29f3b1674b38d6ba8272d7fa6859118d55469ed7d34f3f3fec11815c2e3fae` |
| SHA-256 motor empaquetado | `9fd9c840e599946d26676a431be04bfb771c10b8e4160d886c58db2a59dd0f44` |
| SHA-256 certificado | `ce235f8d30539f6d344c692983caf309dde2bdb915120c23d0ba23797076d035` |

Gradle completó assembleRelease localmente tras liberar espacio temporal. Se verificó la firma con apksigner. La validación pública del código pasó y la entrega privada verificó las entradas y firma antes de subir el artefacto. [Código y pruebas](https://github.com/zam-arch/NOVAHD_sc/actions/runs/45792219942) · [Entrega privada](https://github.com/zam-arch/NOVAHD-private-test/actions/runs/45791965510) · [APK y SHA persistentes](https://github.com/zam-arch/NOVAHD-private-test/tree/delivery/v0.13/inputs/recovery-v0.13)

## Prueba en el POCO

1. Instalar como actualización. En Ajustes dejar seleccionado **Encuadre original de iPad**, guardar y entrar al capítulo 2 desde la misma partida. Comparar el texto y el marco del minimapa antes, durante y después de que hable Yelena. Probar los extremos de los botones y las bandas negras.
2. Salir del juego, desmarcar el encuadre original y repetir desde el mismo punto con culling original. Capturar la misma escena en ambos modos. En un segundo recorrido opcional cambiar solo el culling si hay geometría ausente.
3. Con el catálogo activado, importar dos pistas con artistas distintos y crear una lista con ambas. Abrir cada pestaña del reproductor del juego, seleccionar la segunda canción, pausar, elegir otra y continuar. Comparar la fila, el rótulo, el indicador y la pista audible, también tras un avance automático. Volver al audio original y comprobar las voces.
4. Cargar el guardado, minimizar y volver, reproducir una cinemática y pasar de nivel. Las mediciones existentes son de cadencia e intervalos de swaps. La compatibilidad física con páginas de 16 KiB y los FPS efectivamente presentados siguen pendientes.
