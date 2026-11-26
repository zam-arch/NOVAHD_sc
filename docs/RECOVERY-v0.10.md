# Recovery 0.10 · Diagnóstico gráfico y catálogo musical

Punto de partida `b45cae2`. El usuario confirmó que la campaña funcionó en el POCO F8 Pro con el motor recompilado de 0.9. Informó un cambio de tamaño de texto en el diálogo de Yelena del capítulo 2 y confirmó que puede entrar al juego con una canción personalizada ya en reproducción.

## A. HUD y restauración de estados

El blit de presentación guardaba las matrices GLES efectivas pero restauraba valores reconstruidos desde las estructuras auxiliares. También cambiaba el color de borrado a negro y lo dejaba activo. Se restaura ahora el estado capturado. La prueba GLES simulada introduce matrices distintas de las auxiliares, ejecuta el blit, verifica que durante el swap se usaron las matrices de presentación y comprueba que después se recuperaron las matrices y el color originales.

El escalado de controles depende de `hud_seen` y `hud_on`. Un patrón geométrico identifica el botón de pausa. Tras cuatro cuadros sin detectarlo, el runtime deja de ampliar los elementos candidatos y cambia las zonas táctiles correspondientes. Es una causa candidata del salto que el usuario observó. Hace falta una captura temporal del capítulo 2 para vincularla al diálogo. Se mantuvieron los umbrales geométricos y se acotó el contador de ausencia.

La traza recoge detección y estado efectivo del HUD, cantidad de zonas táctiles, viewport lógico y efectivo, scissor y coeficientes de matrices. Lee el estado GLES en el último pase antes del blit, con muestras nativas cada quince cuadros y en las transiciones detectadas. El panel registra el último estado aproximadamente una vez por segundo y conserva las 90 muestras recientes en preferencias privadas. Las muestras de un segundo pueden omitir transiciones breves. La traza describe el estado al final de un cuadro y no identifica semánticamente los subtítulos ni captura cada draw.

## B. Geometría, profundidad y stencil

Los tres modos son culling original, desactivación de face culling y orientación CW/CCW invertida. La prueba modifica el estado físico solo alrededor de las llamadas de dibujo del invitado cuando el juego pidió culling. El estado original se restaura inmediatamente y la información devuelta al juego conserva el estado que pidió. La prueba automatizada confirma esa reversibilidad. El experimento actúa en rasterización. No cuenta polígonos descartados por frustum u oclusión antes de llegar a GLES.

El FBO conserva la corrección de la 0.9. La traza registra los estados de completitud de D24S8 y del resultado final, los bits de profundidad y stencil del FBO ligado, depth test, máscara, función y rango. Para atribuir geometría desaparecida a culling o a otra causa hay que comparar la misma vista con los tres modos. El resultado de D16, las transparencias, las texturas y el z-fighting siguen pendientes en el Adreno 830.

## C. Resolución y panorámica

El panel distingue pantalla física, destino interno de presentación y superficie EGL. La prueba sintética recorre 4:3, 16:9, 18:9, 19.5:9, 20:9 y 21:9 para un control de ejemplo. Comprueba escala igual en ambos ejes y la inversión de la transformación táctil sobre ese control. Requieren pruebas en Android la composición completa, el encuadre 3D y los casos de diálogo. No se añadieron escalas de usuario ni cambios de FOV mientras se aísla el defecto.

## D. MediaPlayer del juego

`songsQuery`, `artistsQuery`, `playlistsQuery`, `items`, `collections` y `valueForProperty:` figuran entre los nombres encontrados en el ejecutable. El puente experimental entrega al runtime las canciones del catálogo de 0.7, agrupaciones de artistas y álbumes y listas con pistas. Los objetos invitados ofrecen título, artista, álbum y tipo musical. Los álbumes se guardan al importar nuevas pistas. Para archivos importados antes de 0.10 se conserva el registro existente y el álbum puede quedar vacío.

La prueba de Java y C verifica el formato, metadatos UTF-8 y rechazo de una fila truncada. La opción **Probar catálogo musical del motor** está desactivada por defecto. El juego contiene delegados de `UITableView`. En el runtime, `reloadData` es inerte y los métodos de recuento devuelven cero. Las pestañas pueden seguir vacías incluso cuando las consultas entregan un catálogo. Faltan validar el recorrido de la fuente de datos y la selección, implementar predicados, notificaciones y conectar las órdenes de reproducción al backend compartido. Se conserva el reproductor provisional y el flujo de audio original. La prueba de importación y reproducción de 0.9 proviene del usuario.

## E. Telemetría, prueba y entrega

Se muestran swaps por segundo e intervalo entre swaps exitosos. El dato solo refleja llamadas completadas a EGL. Quedan pendientes los timestamps de presentación del compositor. El PSS corresponde al proceso y la resolución del modo de pantalla es la física. El bucle del juego conserva su periodo de 60 Hz. Antes de cambiar esa frecuencia se deben aislar los ticks que gobiernan física, animación y lógica.

Para el capítulo 2, activa **Mostrar diagnóstico y guardar traza gráfica** en Configuración. Empieza unos segundos antes del diálogo, avanza hasta que el texto reduzca su tamaño y deja pasar unos segundos más. Vuelve al gestor y pulsa **Copiar última traza de campaña** en Configuración. Para comparar culling, cambia una opción a la vez, reinicia la campaña desde el mismo guardado y repite con la misma posición y vista. Para el catálogo, con una canción y una lista ya importadas, activa el experimento y revisa las pestañas. Comprueba también minimizar y volver, sonido, carga de partida y transición de nivel.

Los puntos de restauración de la rama conservan por separado el arreglo de presentación, el diagnóstico y el puente de catálogo. Los 49 objetos se extrajeron del archivo privado de 0.9. El builder validó hashes, el ABI y la imagen al enlazar. El APK mantiene paquete, firma experimental y ruta de partidas. La ejecución de 0.10 en Android queda pendiente.
