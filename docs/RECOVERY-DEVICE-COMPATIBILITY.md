# Fase 0.2. Entrega privada y compatibilidad de memoria

## APK congelado y entrega

El APK del juego generado en la fase 0.1 se conservó sin reconstruirlo. La verificación local dio **206.306.036 bytes**, SHA-256 `5b5efdeb540a481a7ffbf93ec64583aa638ce5905611866eb9aff342ef1729d0` y firma Android Debug válida con huella SHA-256 `ce235f8d30539f6d344c692983caf309dde2bdb915120c23d0ba23797076d035`. El verificador también comprobó la identidad de la biblioteca oficial, `image.bin` y los 44 recursos. El archivo original de entrega permanece en `/workspace/shared/downloads/NOVA-HD-v0.5-Recovery-ARM64-4KiB.apk`.

El enlace al archivo del workspace falló en Android. El entorno de Codex tiene una entrada HTTP para el puerto 3000, pero no entregó una URL autenticada en las consultas realizadas. GitHub Actions admite un ZIP descargable desde el navegador del teléfono con una sesión autorizada. Se preparó `.github/workflows/deliver-private-apk.yml` para republicar exactamente el APK conservado como artefacto, con siete días de retención. El workflow rechaza repositorios públicos, comprueba tamaño, SHA-256 y firma, y no compila nada.

Para habilitar la entrega privada desde el teléfono:

1. Abre [New repository](https://github.com/new), selecciona tu cuenta, escribe `NOVAHD-private-test`, elige **Private** y crea el repositorio vacío.
2. Abre [GitHub installations](https://github.com/settings/installations), entra en **Configure** en el conector de GitHub de Codex y añade ese repositorio si el acceso está limitado a una selección. Conserva la visibilidad privada.
3. Comparte el nombre completo del repositorio y autoriza que se use para el APK del juego. Desde el entorno se podrá enviar el workflow como código, subir el APK conservado a una Release privada `delivery-0.2` y ejecutar **Deliver verified 4 KiB APK (private)**. La Release ofrece el archivo directo y Actions el ZIP con el APK y `SHA256SUMS`. No necesitas subir el APK desde el teléfono.

La firma de prueba difiere de la oficial. Si Android indica conflicto con la 0.4 ya instalada, conserva esa instalación para proteger tus partidas. El APK de diagnóstico usa otro identificador y puede coexistir.

## Diagnóstico desde Android

El equipo principal de referencia es el **POCO F8 Pro**, con Snapdragon 8 Elite, Adreno 830 y Android 17 según la ficha aportada. El tamaño de página permanece **sin medir en ese dispositivo**. La app de diagnóstico mide `Os.sysconf(OsConstants._SC_PAGESIZE)` y muestra bytes, KiB, versión Android, API, modelo y ABI, con opción de copiar el informe. No requiere ADB, permisos ni acceso de red y se puede instalar con el juego presente. El futuro APK del gestor ofrece la misma lectura en **Configuración → Diagnóstico**. El APK del juego congelado sigue usando su protección previa de 4096 bytes.

Se compilaron localmente `:diagnostic:assembleDebug` y `:app:compileDebugJavaWithJavac`. El APK de diagnóstico empaca DEX y ningún juego, biblioteca nativa ni recurso de campaña. La compilación y la firma se verificaron. La instalación y el resultado de la medición en el POCO quedan pendientes.

## Auditoría del tamaño de página y carga moderna

- `runtime/mem.c:16–24` es la única llamada a `mprotect` del runtime. Usa una máscara de 4 KiB en el inicio y el fin. El binario original confirma esa máscara en el desensamblado de `rt_commit`. La imagen comienza en `0x1000`. Con páginas de 16 KiB, `mprotect(reserva + 0x1000, …)` recibe una dirección desalineada y se espera `EINVAL` antes de copiar la imagen. El camino de error termina en `rt_fatal`.
- `runtime/mem.c:27–42` reserva el mapa de 4 GiB con `mmap(PROT_NONE, MAP_ANONYMOUS | MAP_NORESERVE)`. `mmap` sin dirección fija da una base alineada por el SO. Los otros consumidores de `rt_commit` son el stub de importación de `0x1000` bytes, la zona de importación de datos y el crecimiento del heap en tramos de 1 MiB. No se encontró `munmap` en el runtime existente. La vía Windows usa `VirtualAlloc` y debe conservar su comportamiento.
- `host_page_size` en `runtime/libc.c` responde a una importación Mach del juego, por lo que 4096 es parte de la vista Darwin invitada. El `st_blksize` del mismo archivo es tamaño de bloque de E/S. El redondeo de 4096 de `libstdcxx.c` replica el crecimiento de cadenas de GCC. Los 4096 del allocator pequeño, TLS, búferes y límites de tablas no son alineación física. Cambiarlos no corrige el fallo.
- La biblioteca original ya tiene segmentos `PT_LOAD` con `p_align=0x4000` y offsets congruentes. Se instala con extracción de bibliotecas, por lo que su entrada comprimida no necesita alineado ZIP de 16 KiB para carga directa. Una futura variante con bibliotecas sin extraer debe verificar tanto ELF como el alineado ZIP. El requisito moderno de formato de carga es independiente de la llamada a `mprotect`. El arranque en Android 17 y los controladores de ese teléfono aún no se probaron.

## Viabilidad de corregir el runtime

Reempaquetar, firmar o usar un cargador Java no cambia la dirección de la protección nativa. Redondear simplemente el primer tramo hacia abajo a 16 KiB haría accesible `__PAGEZERO`, que protege frente a accesos nulos del invitado. La variante congelada conserva el bloqueo para 16 KiB.

A nivel de fuente se puede probar una corrección confinada a la capa de memoria. Con página física `P`, reservar 4 GiB más una página y situar la base virtual del invitado a `P - 0x1000` bytes del comienzo de la reserva. Así el primer byte de la imagen coincide con el inicio de la segunda página física y el intervalo invitado `[0,0x1000)` permanece protegido. Cada operación debe redondear **direcciones físicas** con `P` usando aritmética de punteros de 64 bits, validar overflow y límites, y mantener la base física para liberar el mapa. En 4 KiB el sesgo es cero. En 16 KiB el sesgo es `0x3000`. El sesgo por sí solo no sirve para los commits posteriores de importación o heap.

Si se recuperan los archivos C traducidos u objetos ARM64 originales, junto con `image_meta.c` compatible, puede recompilarse el runtime y volver a enlazar sin volver a traducir el juego. El ZIP y el APK revisados no contienen esos objetos independientes ni los fuentes generados. La biblioteca final mezcla el runtime y el juego en un ELF enlazado. Sus símbolos exportados no equivalen a objetos reubicables. Interponer `mprotect` o editar el ELF sin esos insumos podría alterar la protección de memoria, el enlace o el arranque. El flujo por IPA sigue siendo la vía disponible si no se recuperan los objetos. Una solución de fuente requerirá pruebas de guardia nula, imagen, stubs, heap y arranque real en 4 y 16 KiB antes de entregarse.

No se modificó el runtime, el binario oficial, los gráficos ni los controles. La rama se creó a partir de `10a8c72` y se mantiene separada de `development`.
