# Verificación de Android sobre development

La rama `feature/android-build-verification` parte de `e3abea4` de `zam-arch/NOVAHD_sc`. La [ejecución original de GitHub Actions](https://github.com/zam-arch/NOVAHD_sc/actions/runs/45710700125) falló en el paso de Gradle. La compilación local usa JDK 17, Gradle 8.5, SDK Platform 34 y Android Build Tools 34.0.0.

Gradle completó localmente `:app:assembleDebug :app:assembleRelease`, incluida la verificación lintVital de release. Se verificaron las firmas de ambos APK y el alineado y contenido del APK debug. Ruta de salida de debug: `app/build/outputs/apk/debug/app-debug.apk`. La copia de entrega está en `/workspace/shared/downloads/NOVAHD-gestor-recursos-debug.apk`.

El workflow usa la distribución preparada por `gradle/actions/setup-gradle@v4`. Ensambla ambas variantes, verifica la firma de debug y publica `novahd-resource-manager-debug` desde la ejecución correspondiente. El artefacto contiene el gestor de recursos. El script `tools/recomp/build_apk.py` queda disponible para compilar con un Mach-O legítimo en un entorno autorizado.

Un APK de campaña requiere `lib/arm64-v8a/libnovahd.so` y `assets/image.bin`, derivados del ejecutable original. El gestor actual usa un manifiesto de ejemplo con hashes provisionales y una URL de ejemplo. Para validar una instalación completa todavía faltan un manifiesto real, una copia legítima, el recorrido SAF del selector y la validación de destinos al extraer ZIP. La opción de importar avisa de su estado sin borrar el bundle instalado. Java usa el directorio interno `files/NovaHD.app`, mientras que el runtime nativo busca primero el directorio externo específico de la aplicación. La integración de ambos debe apuntar al mismo bundle y conservar la ruta de las partidas existentes.

## Adaptación a pantallas panorámicas

1. Conservar 1024×768 como espacio lógico del juego. `window_geometry` ya mide la ventana y calcula un búfer acorde a su aspecto. Aproximadamente 1366×768 a 16:9 y 1706×768 a 20:9. En `android_main.c`, conviene validar la geometría efectiva de EGL y publicar un estado coherente para render y entrada al crear o redimensionar la superficie.
2. En `gles1.c`, extender la proyección en perspectiva a todo el encuadre y conservar las proporciones de menús y HUD. Usar los insets del recorte y los gestos del sistema para colocar controles de borde. Revisar el minimapa y su scissor como una unidad, junto con subtítulos y panorámicas. Aplicar el cambio de framebuffer en un límite entre cuadros y comprobar sus dimensiones contra los límites de GLES.
3. Convertir los toques con la transformación inversa de la presentación y la colocación actual del HUD. Mantener el mapeo por dedo durante cada gesto y cancelar contactos activos cuando cambia la geometría de la superficie. Probar 4:3, 16:9 y 20:9, ambos sentidos de landscape, recortes, bloqueo y reapertura, además de disparar mientras se mueve el stick.

La revisión de pantalla se hizo sobre el código. Quedan por comprobar en dispositivo el primer arranque, las descargas interrumpidas, la corrupción, el espacio insuficiente, el reinicio, la campaña sin red y el comportamiento del runtime ARM64.
