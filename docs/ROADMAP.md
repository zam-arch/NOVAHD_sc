# Hoja de ruta acumulativa

| Hito | Estado y condición para avanzar |
|---|---|
| Recuperación 0.5 | Respaldo ARM64 de 4 KiB. Instalación y arranque de menú y campaña probados por el usuario en POCO F8 Pro, Android 17. Se conserva el APK y la ruta de partidas. |
| Diagnóstico 0.6 | Sonda GLES separada y panel opcional de pantalla/PSS/páginas. Obtener informe y una reproducción en el teléfono. Mantener la 0.5 como referencia A/B. |
| Estabilidad gráfica | Reproducir por escena y aislar texturas, geometría, transparencia o profundidad. Probar el fallback D16 y la restauración de estados. Corregir solo lo demostrado y validar campaña y arranque. |
| Telemetría del motor y 16 KiB | Obtener fuentes/objetos traducidos compatibles o IPA legítimo. Instrumentar presentación y errores sin alterar estados. Corregir la memoria host preservando la guardia nula. Probar 4 y 16 KiB con Android real. |
| Multijugador LAN | Tras aprobación, reconstruir el contrato Darwin/Bonjour, lobby y sesión, con dos dispositivos de prueba y paquetes de referencia controlados. |
| Multijugador P2P | Tras estabilidad LAN, evaluar señalización, ICE/STUN/TURN, seguridad, relay, autoridad y sincronización. Mantener compatibilidad de protocolo bajo pruebas. |
| Mapas personalizados | Identificar esquemas de recursos y escenas con archivos de prueba propios. Exportar un mapa original mínimo con colisiones, luces y puntos de aparición. Cargar fuera del bundle de campaña y validar en dispositivos. |
| Editor independiente | Una vez demostrado el exportador, añadir importación de recursos originales, edición y validación, contratos de archivos y round-trip de escenas. |

La investigación técnica de red y mapas está en [Recovery 0.6](RECOVERY-v0.6.md). El trabajo de cada etapa posterior requiere aprobación.
