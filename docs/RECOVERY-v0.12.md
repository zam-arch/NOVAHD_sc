# Recovery 0.12 · HUD, minimapa y rótulo musical

Se partió de `4325173` (APK 0.11 compilado en `04cc649`). En el POCO el usuario confirmó campaña y selección desde el reproductor original. Reportó falta de confirmación visual, textos y controles pequeños y recortes del minimapa.

## A/B del runtime y corrección

| Versión | Comportamiento |
|---|---|
| 0.9 | `wide_place` ampliaba hasta 1,6× lotes compactos de borde cuando la heurística del HUD estaba activa. El mismo mecanismo admitía geometría de controles, instrumentos y lotes con scissor. |
| 0.10 | Conservó 1,6× e incorporó restauración de matrices y color de borrado. El viewport de campaña `−461,30,1229,1638` observado en el POCO corresponde a esa escala. |
| 0.11 | Quitó la ampliación incluso en lotes que sí representaban controles e instrumentos de borde. Esta es la regresión de tamaño que puede reproducirse a nivel de código. Siguió habiendo dos tratamientos para el mismo scissor pequeño según proyección y tipo de llamada. |
| 0.12 | Recupera ampliación uniforme y acotada solo para lotes de esquina que cumplen la clasificación geométrica y para la región de mapa observada. Se calcula desde los límites lógicos en cada pasada y no depende de detectar cuatro cuadros seguidos de HUD. |

En panorámica el ajuste **HUD y minimapa** parte de 135 % y **Controles de esquina** de 140 %. Admiten 75–150 %, se guardan con **Aceptar** y tienen un restablecimiento. Valores iniciales elegidos para recuperar legibilidad sin volver a la ampliación general de 1,6×. El límite de ancho disponible puede reducir el máximo efectivo. La composición original 4:3 conserva su camino de proporciones. La inversa táctil usa la misma transformación afín que el dibujo y el mismo anclaje.

El scissor lógico registrado de `656,643,105,200` cae en el cuadrante de mapa. Antes, un borrado o dibujo ortográfico entraba en la rama de recorte y un draw con perspectiva podía entrar en la rama 3D de ancho completo aun con el mismo scissor. Ahora se comparte la transformación del mapa y las dimensiones se calculan redondeando sus bordes. Al retirar el recorte vuelve a aplicarse la proyección 3D de panorámica. El fixture comprueba la transformación idéntica de borrado y dibujo con perspectiva, y que un borde con los mismos límites conserva ese anclaje. La correspondencia exacta de los marcos y máscaras de cada escena todavía requiere validar el POCO.

Las bandas de texto delgadas y centrales conservan escala unitaria. Los lotes 2D grandes conservan el tratamiento de fondo. Los ajustes quedan limitados a dos grupos de geometría. Salud y munición pueden estar en un lote separado, y el motor aún no expone marcas fiables para separar cada fuente de texto, objetivo, diálogo o tutorial de sus menús y cinemáticas. Una esquina con geometría parecida puede entrar en la clasificación. El evento de pase incluye el retorno invitado para identificar orígenes concretos en una captura. El tamaño independiente de textos y subtítulos queda pendiente de identificar esas llamadas. Se mantiene el ritmo del juego.

## Música original

En el ejecutable, la rutina de etiquetas de fila `0x128840` consulta `count`, `objectAtIndex:` y `valueForProperty:`, construye la etiqueta y copia caracteres. La ruta de cambio de pista parte de `MusicObserver changePlayingItem:` en `0x129520`, pasa por `0x127f20` y obtiene `nowPlayingItem` en `0x128648`. Allí usa las propiedades de artista y título con el formato existente `%@ - %@`. Hay un consumidor de presentación indirecto en la ruta de actualización de menú desde `0x126588`. La rutina exacta que rasteriza las filas queda pendiente. Estas llamadas son independientes de UITableView.

0.11 devolvía la pista actual solo cuando Android publicaba un identificador y el objeto seguía en la cola vigente o coincidía con la selección. 0.12 conserva objetos de pista estables por ID entre catálogos dentro de un caché acotado y devuelve el elemento seleccionado antes de recibir el estado de audio. Se separa la notificación de cambio de elemento del estado de reproducción. `NSString.length` y `characterAtIndex:` exponen unidades UTF-16 para que el juego copie caracteres en lugar de bytes de UTF-8. El test incluye acentos y un par sustituto. La representación final depende de los glifos de la fuente del juego.

La preferencia conserva el último ID elegido sin iniciar reproducción al restaurar la app. Los comandos personalizados siguen usando el mismo reproductor. La orden de parada del juego vuelve a su audio y limpia esa selección. La música original sigue siendo el comportamiento predeterminado. La prueba local valida el contrato de la selección inmediata y las notificaciones con backend simulado. Quedan por comprobar el rótulo real, los tres grupos del menú, la pausa, pistas siguiente y anterior y la actualización tras cerrar y abrir.

## Compatibilidad y pruebas

El culling original, la comparación sin face culling, el formato D24S8, su fallback D16 y el blit corregido permanecen activos. La traza identifica intervalos entre swaps de al menos 100 ms dentro de una misma superficie. No hay evidencia para atribuirlos a cargas o al controlador. FPS presentados, estabilidad real de 16 KiB y regresión entre escenas y dispositivos quedan pendientes.

Reenlace ARM64 con las 49 entradas verificadas del bundle privado de 0.9. Pruebas locales de restauración GLES simulada, escalado, toque, scissor, metadatos y reproducción simulada, memoria y verificación de entradas. No se volvió a traducir el juego. Para comparar en el POCO, usar el mismo guardado y ángulo con los valores predeterminados. Revisar minimapa al entrar en combate y en el diálogo de Yelena, subtítulos de cinemática, textos de misión, toque en las esquinas, y transición de nivel. Para música, importar dos pistas, activar el catálogo y seleccionar una pista en el menú original. Revisar título y estado, cambiar, pausar y volver al audio del juego.
