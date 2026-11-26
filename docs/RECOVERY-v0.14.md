# Recovery 0.14 · Inicio directo y configuración en juego

Base `a5877de` en `recovery/v0.13`. Fuente del APK `d0fb94b`. En el POCO F8 Pro, el usuario probó 0.13 en 4:3 y comunicó que no vio glitches en las escenas probadas, que el área de juego y los botones eran pequeños y que la música personalizada seguía al salir de un nivel.

## Inicio y recuperación

`LauncherActivity` reutiliza la verificación en segundo plano de `CampaignManager`. Primero comprueba el runtime y el tamaño de página. Al terminar la verificación, abre la actividad nativa si los recursos están listos. La instalación embebida y la migración se mantienen en el mismo gestor. Cuando faltan archivos o una comprobación falla, se abre el gestor de recursos con los controles existentes.

En el motor recompilado se registra un intento únicamente al crear la actividad, antes de iniciar el motor. El runtime confirma el arranque después de completar un ciclo de juego y observar al menos un intercambio EGL exitoso. Si el proceso se detiene antes de confirmar, un nuevo inicio desde el icono muestra el gestor de recuperación. El acceso **Recuperación** al mantener pulsado el icono permite abrirlo cuando el launcher del teléfono admite accesos directos. El gestor conserva la comprobación y el reintento manual mediante **Jugar**. Si Android conserva una actividad bloqueada, vuelve a abrir el icono después de detener la aplicación desde Android.

## Configuración integrada

El botón de engranaje se presenta en una ventana superpuesta sobre el juego con foco. Está disponible desde el menú principal y durante la campaña, incluido el menú de pausa. El diálogo se abre sobre NativeActivity y no modifica el FBO ni escribe matrices. Las herramientas de música, recuperación e información del dispositivo siguen siendo pantallas separadas y se abren sobre la actividad en curso. El acceso integrado a los widgets internos del juego requiere identificar rutas de creación, localización, activación y cierre en ambos menús. Se usó la alternativa superpuesta solicitada.

El culling y la telemetría se aplican al instante desde las preferencias. El registro se guarda al actualizar el panel y se puede copiar desde el diálogo. El encuadre se guarda al tocar el selector y se aplica la próxima vez que arranca el motor. El diálogo incluye el catálogo musical, la biblioteca existente, volumen, regreso a la banda sonora, recuperación y restablecimiento. La escala independiente y la resolución configurable siguen pendientes de sus rutas de dibujo y toque.

## Comparación gráfica

Para la superficie 1668 × 768 registrada en el POCO, la referencia muestra 1024 columnas de imagen, alrededor del 61 % del ancho. La panorámica usa 1668. En 4:3, el FBO interno permanece en 768 × 1024 con el viewport, el scissor y la proyección del juego, y el toque invierte el rectángulo de presentación. En panorámica, el FBO crece por su eje largo. `wide_apply` cambia la proyección en draws con perspectiva sobre el drawable y aplica transformaciones al viewport y al scissor. Los draws 2D pueden entrar en `wide_place` o repartirse en grupos mediante `wide_split`. La inversa del toque depende de la región publicada para cada grupo.

Esta comparación delimita las rutas que pueden explicar la diferencia comunicada por el usuario. Aún faltan dos capturas comparables de la misma escena, posición, cámara y culling. Tampoco se ha podido seguir los controles de campaña desde sus funciones de origen hasta el dibujante común y la zona táctil. El 4:3 sigue siendo el valor de fábrica. Se conserva la panorámica experimental y el culling original como valor de fábrica.

## Música

El reproductor personalizado utiliza la misma instancia Java. Al quedar todas las actividades de la aplicación en segundo plano, la reproducción activa o pendiente se pausa después de 250 ms. El retorno a GameActivity puede restaurar esa sesión. Una pausa manual, una orden explícita de parar o liberar la pista cancelan la reanudación. Al cerrar la actividad nativa se detiene el audio personalizado y al abrir un proceso nuevo solo se restaura el identificador guardado, sin iniciar audio. En el diálogo integrado se puede volver a la banda sonora original.

Salir de un nivel y volver al menú original sucede dentro de la misma NativeActivity. Las referencias del Mach-O a funciones de niveles distinguen el subsistema, pero todavía falta una llamada verificada que identifique esa transición frente a pausa, cinemáticas, reintentos o carga del siguiente nivel. El apagado automático al abandonar un nivel sigue pendiente. Los ensayos del puente pasan. La visualización de filas, selección, rótulo e indicador en Android sigue pendiente de prueba física.

## Pruebas en el POCO

1. Instala como actualización y toca el icono. Comprueba que ves la verificación y luego el juego. Mantén pulsado el icono para comprobar el acceso **Recuperación**.
2. Abre el engranaje en el menú principal y durante la pausa de un nivel. Activa la telemetría, vuelve al juego, abre el engranaje y copia la traza. Confirma que la pausa, los botones y el guardado responden.
3. En el capítulo 2, guarda una captura del diálogo de Yelena y del minimapa en 4:3. Desmarca el encuadre original, cierra y vuelve a iniciar. Repite desde el mismo guardado, con la misma cámara y culling. Registra cualquier geometría ausente en el mismo lugar.
4. Reproduce una pista personalizada, minimiza y vuelve al juego. Repite con una pausa manual antes de minimizar. En el reproductor original revisa canción, artista, lista y rótulo. Sal del nivel, registra si sigue sonando y prueba **Volver a la banda sonora original**. Cierra la actividad y comprueba el silencio.

Los scripts locales verificaron gráficos y transformaciones, puente musical, JNI de la sonda, guardias de memoria y rechazo de entradas de enlace alteradas. Se compararon los 21.230 símbolos traducidos con la referencia. La validación en Android de inicio, superposición, audio, partidas y compatibilidad física de 16 KiB sigue pendiente.

## APK experimental

| Campo | Valor |
|---|---|
| Versión | 0.14-recovery-arm64-4k, versionCode 15 |
| Rama y compilación | recovery/v0.14, `d0fb94b` |
| Tamaño | 206.180.212 bytes |
| SHA-256 APK | `21a0e41a62986dcbc1280ad742b65e3bf07e1d40d0245a110ed6337b48b9efd0` |
| SHA-256 motor | `18d428b89b3d36eb61351dd1c4068927bffc14764fbd779bbf849e83f07e59c4` |
| Paquete | com.novahd.recomp |
| SHA-256 certificado experimental | `ce235f8d30539f6d344c692983caf309dde2bdb915120c23d0ba23797076d035` |

Gradle ensambló el APK en el entorno de trabajo y se verificaron los 44 recursos, la imagen empacada, la firma, el launcher y el acceso directo de recuperación en el manifiesto y recursos compilados. El APK usa los 48 shards y el dispatcher existentes. El criterio para actualizar es instalar sobre el mismo paquete y certificado. La conservación física de la partida se comprueba cargando un guardado. [APK privado persistente](https://github.com/zam-arch/NOVAHD-private-test/tree/delivery/v0.14/inputs/recovery-v0.14) · [Validación del código](https://github.com/zam-arch/NOVAHD_sc/actions/runs/45797207159)

El flujo privado valida la huella de firma, los hashes de las entradas privadas y el checksum del APK antes de subirlo. La rama pública contiene código y documentación. [Ejecución de entrega privada](https://github.com/zam-arch/NOVAHD-private-test/actions/runs/45797343778)
