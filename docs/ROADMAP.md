# Hoja de ruta acumulativa

| Hito | Estado y condición para avanzar |
|---|---|
| Recuperación 0.5 | Respaldo ARM64 de 4 KiB. Instalación y arranque de menú y campaña probados por el usuario en POCO F8 Pro, Android 17. Se conserva el APK y la ruta de partidas. |
| Diagnóstico 0.6 | Sonda GLES separada y panel opcional de pantalla/PSS/páginas. Obtener informe y una reproducción en el teléfono. Mantener la 0.5 como referencia A/B. |
| Recovery 0.7 | Música personalizada local y sonda D24S8 → D16. Validar en POCO antes de intervenir en el nativo. |
| Runtime 0.8 | Corrección fuente de FBO y memoria, telemetría opt-in y relink probado con entradas sintéticas. Conseguir objetos/metadatos legítimos o Mach-O y validar en el POCO. |
| Entradas de enlace 0.8.1 | Auditoría upstream sin bundle recuperable. Importador privado por IPA y rechazo de ELF enlazado. Conseguir objetos/metadatos autorizados o Mach-O compatible antes de empaquetar. |
| Estabilidad gráfica | Reproducir por escena y aislar texturas, geometría, transparencia o profundidad. Probar el fallback D16 y la restauración de estados. Corregir solo lo demostrado y validar campaña y arranque. |
| Panorámica y resolución | Validar 4:3, 16:9, 20:9 y superiores con FOV, HUD y toque coherentes. Evaluar Original, HD y Nativa tras relink del motor y pruebas de memoria. |
| Música integrada | Con acceso al runtime, estudiar pestañas iOS, canales, atenuación con diálogo y estados de música dinámica. |
| Telemetría del motor y 16 KiB | Obtener fuentes/objetos traducidos compatibles o IPA legítimo. Instrumentar presentación y errores sin alterar estados. Corregir la memoria host preservando la guardia nula. Probar 4 y 16 KiB con Android real. |
| Multijugador LAN | Eco UDP aislado verificado en loopback. Tras aprobación y validación entre dispositivos propios, reconstruir el contrato Darwin/Bonjour, lobby y sesión, con dos dispositivos de prueba y paquetes de referencia controlados. |
| Multijugador P2P | Tras estabilidad LAN, evaluar servidor de señalización, salas privadas, ICE/STUN/TURN, seguridad, relay, autoridad y sincronización. Mantener compatibilidad de protocolo bajo pruebas. |
| Mapas personalizados | Identificar esquemas de recursos y escenas con archivos de prueba propios. Exportar un mapa original mínimo con colisiones, luces y puntos de aparición. Cargar fuera del bundle de campaña y validar en dispositivos. |
| Mods y formatos | Carga con manifiestos separados y validación. Investigar conversor de mapas de Vanguard Zero e importación de formatos externos solo con permisos y esquemas demostrados. |
| Editor independiente | Una vez demostrado el exportador, añadir importación de recursos originales, edición y validación, contratos de archivos y round-trip de escenas. |
| Mandos, háptica y rendimiento | Evaluar controles personalizables, mandos Bluetooth, vibración háptica, optimización y altas tasas de refresco tras medir latencia y frame pacing nativo. |

El contrato nativo y sus pruebas están en [Recovery 0.8](RECOVERY-v0.8.md). La investigación gráfica, musical y panorámica está en [Recovery 0.7](RECOVERY-v0.7.md). La investigación técnica de red y mapas está en [Recovery 0.6](RECOVERY-v0.6.md). El trabajo de cada etapa posterior requiere aprobación.
