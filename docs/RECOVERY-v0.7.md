# Recovery 0.7. Gráficos, panorámica y música

## Punto de partida

El usuario probó la 0.6 en un POCO F8 Pro con Android 17 ARM64. La campaña, movimiento, disparo y menús funcionaron. Persistieron glitches gráficos y el error de conexión multijugador. La 0.5 y la 0.6 se conservan como respaldo. La 0.7 mantiene paquete, firma de prueba, rutas de guardado, instalación automática, motor e imagen de la versión anterior. Las observaciones del POCO corresponden a la 0.6.

## Framebuffer y glitches

`present_setup` en `runtime/gles1.c` retira el adjunto de stencil al caer de D24S8 a D16 y consulta el estado del FBO. `present_resize` cambia el formato del renderbuffer a D16, deja el stencil adjunto y acepta el tamaño sin volver a comprobar el FBO. Está demostrado el defecto de la ruta de fallback en el código. Sigue sin demostrarse que la rama se ejecute en esa escena o en ese POCO.

La sonda separada de **Configuración → Diagnóstico** ahora transforma un FBO de prueba D24S8 completo a D16 y registra estado del framebuffer y error GLES antes y después de retirar el stencil. Sirve para comprobar la consecuencia en ese controlador. Sus resultados no demuestran la frecuencia del fallback en el juego. Para eso hay que capturar en el motor los tamaños de los adjuntos, formato, estado FBO después de cada cambio, contexto y causa del redimensionado. La biblioteca oficial incluida en el APK es idéntica a la de la 0.6. No se aplicó la corrección nativa al binario.

Las otras hipótesis siguen abiertas. En geometría, inspeccionar matrices, viewport/scissor, elementos del HUD y arrays al reproducir la escena. En texturas, distinguir software PVRTC, BGRA, alineación de filas y mipmaps. En transparencia, comprobar orden de dibujo, alpha y blend antes y después del blit. En profundidad, medir formato efectivo y planos near/far, y separar z-fighting de geometría coincidente. Una captura, el capítulo y el momento del glitch ayudan a elegir la primera traza. La prueba de un FBO GLES1 aislado no atribuye errores al driver ni a una etapa del juego por sí sola.

## Panorámica

`runtime/android_main.c` conserva una referencia lógica de 1024×768 y calcula un búfer de ventana de aspecto variable. `runtime/gles1.c` amplía el destino de presentación y aplica `wide_k` a la proyección de perspectiva según la orientación interna de la capa. El código traslada y escala elementos 2D aparte, y los toques usan la transformación inversa que corresponde al HUD. El blit rota la capa y calcula un viewport de presentación que conserva el aspecto. Son mecanismos existentes, no cambios de la 0.7.

| Aspecto | Búfer teórico con 768 px de alto |
|---|---:|
| 4:3 | 1024×768 |
| 16:9 | 1366×768 |
| 20:9 | 1706×768 |
| 21:9 | 1792×768 |

Son valores del código para esos aspectos ideales en píxeles, con el redondeo presente. La resolución realmente usada por el POCO no se midió. La referencia 4:3 debe seguir dando identidad. Para panorámica, conservar proporción de la imagen y FOV vertical al expandir el horizontal según la ventana útil. Verificar proyecciones no de perspectiva por separado. Aplicar área segura a los controles visibles, dejar la escala y el anclaje del HUD en su propio espacio, y calcular el toque desde la transformación inversa exacta del frame que se presenta. Probar 4:3, 16:9, 20:9 y más ancho, con recortes, ambos sentidos de landscape y cambios de superficie. Altas relaciones pueden exponer geometría fuera del encuadre previsto o implicar márgenes si se limita el FOV.

Los modos **Original**, **HD** y **Nativa** deben cambiar la densidad del render, conservando el espacio lógico de la campaña y las coordenadas de la interfaz. Hace falta consultar dimensiones EGL y FBO reales, memoria aproximada de adjuntos, límites de textura/renderbuffer y presupuesto de GPU. Comprobar también que el proyecto no sobredimensione varios buffers y que una asignación fallida mantenga un modo seguro. Forzar tamaños de Android o añadir un zoom Java no corrige la proyección interna. Estos cambios necesitan volver a enlazar con objetos ARM64/fuentes traducidos y `image_meta.c`, o recuperar el IPA legítimo para generarlos.

## Música Android

El menú original de iOS usa interfaces de MediaPlayer. `runtime/offline.c` registra `MPMusicPlayerController` (`iPodMusicPlayer`, `applicationMusicPlayer`, estado y notificaciones), `MPMediaQuery`, `MPMediaItemCollection`, `MPMediaPropertyPredicate` y `MPMediaPickerController`. La consulta devuelve una biblioteca vacía, el controlador nunca reproduce y el picker simula cancelación. Los objetos y selectores originales no están conectados a Android en la biblioteca precompilada. El reproductor 0.7 es independiente y se abre desde **Configuración → Música personalizada**.

- Selector de archivos Android para importar uno o varios MP3, OGG, WAV o AAC, incluida la extensión M4A. Se copia al espacio privado, se extrae título y artista y se valida que Android reconozca una pista de audio. Límites de 128 MiB por pista y 1 GiB de archivos en la biblioteca. Los codecs concretos dentro del contenedor dependen del soporte de Android. No se necesita Internet para reproducir la copia local.
- Biblioteca y listas persistentes. Mantener pulsada una canción permite añadirla a una lista, retirarla o eliminar la copia local con confirmación. Pulsar una canción reproduce la lista visible. Controles anterior, siguiente, pausa/continuar, aleatorio, repetir lista y volumen de música.
- Si inicias una canción y regresas al gestor para pulsar **Jugar**, la música continúa en primer plano dentro de la app. **Solo audio del juego** detiene la música extra. Se conservan efectos, voces y música del motor existente. La salida de primer plano y la pérdida de foco de audio pausarán la pista personalizada. La reanudación es manual. Estas transiciones aún requieren pruebas en el teléfono.
- El volumen personalizado es una ganancia de su propio `MediaPlayer`; no ajusta efectos, voces ni música nativa. El juego precompilado no expone canales ni eventos de diálogo. El ducking de diálogos del juego, la selección desde las pestañas originales y la música dinámica de exploración, combate, jefes, victoria o multijugador quedan pendientes. Para ellas se necesitan puntos de evento comprobados y un puente de estados o un mezclador en el runtime; las reglas de prioridad, atenuación y transición se deben probar con pistas propias.

## Diagnóstico y 16 KiB

El panel mantiene memoria PSS, modo físico de pantalla y tamaño de página reales. La sonda mide GLES/EGL fuera del contexto del juego. Para FPS reales y tiempo entre fotogramas se requiere instrumentar swaps y presentaciones, y para resolución activa leer EGL/FBO desde el hilo del motor. Se conserva el requisito de 4 KiB porque el runtime oficial usa una dirección desalineada al proteger la primera página del invitado en kernels de 16 KiB. Ajustarlo exige preservar la guardia nula y verificar con ambas configuraciones. No se añadieron valores estimados a la interfaz.

## Pruebas y límites

La validación de entrega verifica firma, paquete e integridad respecto al motor original, la imagen y los 44 recursos. Las pruebas de instalador usan la JVM y no equivalen a un arranque del juego. Pendiente en POCO: instalación sobre 0.6, carga de partida, sonda D16, reproducción sin red de cada formato con archivos propios, persistencia de listas tras cerrar la app, música durante la campaña, pausa al minimizar y conservación del audio del motor. No se implementaron mapas ni multijugador. La siguiente fase requiere aprobación y resultados de dispositivo.
