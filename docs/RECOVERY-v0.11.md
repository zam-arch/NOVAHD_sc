# Recovery 0.11 · HUD, MediaPlayer y traza gráfica

Base de rama `8cb710b`, con resultados de dispositivo aportados por el usuario para el APK de `07d23c7`. El POCO informó pantalla de 1156 × 2510, EGL de 1668 × 768, FBO de 768 × 1668, lógica de 768 × 1024, PSS alrededor de 165 MiB y páginas de 4096 bytes. En esa muestra se usó el modo experimental CW/CCW invertido. El formato packed y el resultado final fueron completos (`0x8CD5`), con 24 bits de profundidad y 8 de stencil.

## HUD

| Evidencia | Lectura del runtime |
|---|---|
| `HUD 0/0` y `1/1` | El primer valor era el permiso para ampliar lotes, calculado con los cuadros anteriores. El segundo indicaba que la geometría de este cuadro coincidió con la heurística del botón de pausa. Tras cuatro cuadros sin esa señal, el permiso pasaba a cero. |
| `hit 4`, `hit 60`, `hit 0` | Contador de regiones geométricas creadas para invertir la transformación táctil. Una llamada triangular podía generar varias entradas al dividir un lote. El contador no representa botones ni pulsaciones. |
| Viewport `-461,30,1229,1638` | `768 × 1,6 ≈ 1229`, `1024 × 1,6 ≈ 1638`, `768 × (1 − 1,6) ≈ −461`, `1668 − 1024 × 1,6 ≈ 30`. Los números reproducen la rama de ampliación anclada. El origen negativo era intencional. |
| Viewports con Y igual a 0, 322 o 644 | Para un lote 2D, `wide_place` distribuye los 644 píxeles extra a un lado, mitad y mitad o al otro lado. El viewport de 1668 de largo requiere además distinguir 3D, borrado y fondo. |
| Coeficientes pequeños | El resumen anterior omitía 12 de 16 coeficientes de proyección. La traza nueva incluye la matriz completa en orden de columnas. Una escala ortográfica pequeña o una componente diagonal cercana a cero con rotación puede ser válida. |

`glViewport` y `glScissor` guardan las coordenadas lógicas. Antes del draw, `wide_apply` clasifica el destino, proyección y scissor. `wide_set` calcula desde las coordenadas guardadas. Los lotes divididos usan `wide_split` y la presentación conserva la restauración de estado de 0.10. La fórmula no acumula los cambios de viewport en las coordenadas lógicas.

Quitamos el factor 1,6 de los lotes 2D genéricos. Las figuras de borde mantienen su anclaje panorámico y la transformación inversa de toque a escala unitaria. Un lote delgado que abarque todo el ancho también conserva su escala. El fondo ancho y alto sigue teniendo su ruta de expansión, y la proyección 3D conserva su ruta propia. Esto elimina una causa reproducible de cambio de tamaño. Los controles que antes recibían la ampliación pueden verse más pequeños. Se comprobó en el fixture que el mismo lote y su toque conservan su escala cuando cambia la detección de HUD. La asociación con el diálogo de Yelena requiere repetirlo en el POCO.

La traza registra transiciones de detección del HUD y combinaciones de viewport/scissor por pase 3D, 2D, lote agrupado, recorte, borrado y FBO auxiliar. Un conjunto acotado filtra combinaciones repetidas y el informe avisa cuando se agota la cola de eventos entre lecturas. El resumen por cuadro incluye la matriz de proyección completa. El motor desconoce si un lote contiene un subtítulo.

## MediaPlayer y tabla

El Mach-O atribuye las implementaciones de `tableView:cellForRowAtIndexPath:` y `tableView:didSelectRowAtIndexPath:` a `CatalogTableViewController` y `GameTableViewController`. Son controladores de catálogo promocional. Su presencia no vincula el `UITableView` inerte al menú musical. El ejecutable también declara consultas `songsQuery`, `artistsQuery`, `playlistsQuery`, comandos de cola y selección, y `MusicObserver` con `changePlaybackState:` y `changePlayingItem:`. La ruta implementada conecta esos contratos con el backend musical.

Los elementos de catálogo conservan el ID de la pista. Los selectores de cola y selección se conectan a reproducción, pausa, siguiente, anterior, parada y consulta del elemento actual. El backend usa un solo reproductor y el hilo principal de Android ejecuta los comandos. Si la pista solicitada ya suena en ese reproductor, se conserva la instancia. Las notificaciones se envían desde el ciclo del invitado al detectar un cambio. La función experimental sigue siendo opt-in. La prueba C ejercita los métodos del invitado contra un backend simulado. El APK requiere validar en Android si el menú original consume estas consultas, dibuja las filas y envía las selecciones. El `UITableView` genérico y su compositor siguen pendientes si una captura demuestra que intervienen. Volumen musical, búsqueda y predicados siguen pendientes en el menú original. El reproductor de Android sigue disponible y el audio de campaña conserva su flujo.

## Culling, profundidad, cadencia y resolución

El modo por defecto es original. La comparación sin face culling se conserva y el modo invertido se retiró. Las configuraciones antiguas con modo 2 vuelven a original. La traza registra el nombre. Los rectángulos negros observados al invertir las caras prueban un efecto del experimento sobre el rasterizado. Todavía faltan comparaciones en modo original y sin culling sobre el mismo glitch. La muestra del POCO usó D24S8 completo, por lo que se conserva D24S8 y la ruta D16 sigue siendo fallback.

Los swaps/s, último intervalo, promedio, p50, p95 y máximo pertenecen a las llamadas exitosas de EGL. Se quitó la línea duplicada del informe. Las pausas largas al cambiar de superficie o por huecos de más de un segundo reinician la ventana. Un intervalo de 544,21 ms cabe en una ventana y aparecerá en el máximo. Esa pausa aislada no identifica cargas, creación de recursos ni sincronización como causa. Aún faltan timestamps de presentación, identificación de fotogramas repetidos y correlación con eventos de carga. Se mantienen los tiempos del juego.

En la orientación de campaña, 1668/2510 y 768/1156 son aproximadamente 66,4 %. Indican escala lineal de la superficie y FBO en esa prueba. La futura configuración de resolución debe cambiar la densidad de EGL/FBO con regeneración segura y mantener las coordenadas lógicas y su inversa táctil. La 0.11 conserva la densidad, proyección y encuadre 3D de la 0.10.

## Prueba en el POCO

1. Instala sobre 0.10. Confirma la versión, carga la partida y revisa controles, sonido y transición de nivel.
2. Activa el diagnóstico en Configuración con culling original. Repite la línea de Yelena y recoge la traza inmediatamente al volver al gestor. Anota si el salto desaparece, si ocurre en el mismo instante o si cambió el alineado del texto.
3. Repite una escena con geometría ausente desde el mismo guardado y posición. Prueba original y sin face culling por separado y conserva captura y traza de cada caso.
4. Importa dos canciones y crea una lista. Activa **Probar catálogo musical del motor** y abre el menú original. Indica qué pestañas muestran filas. Si permite seleccionar, prueba selección, pausa, cambio de canción y vuelta al audio del juego. Minimiza y vuelve.

La rama conserva los objetos ARM64 de 0.9, la firma experimental, el paquete, recursos y partidas. Queda pendiente la prueba de la actualización y ejecución de 0.11 en Android.
