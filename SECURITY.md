# Seguridad

- La API HTTP no tiene autenticación: está pensada para la red local. No expongas la placa a
  Internet (ni con port forwarding ni con túneles).
- La API nunca devuelve credenciales WiFi. El AP de configuración (`LedMatrix-XXXX`) es abierto,
  pero solo sirve el portal cautivo y la API local.
- Todo lo que llega por la red se valida contra rangos reales y se escapa al insertarlo en HTML
  (`textContent` en el cliente).
- El instalador web solo escribe binarios publicados por el CI en GitHub Pages.

Para reportar una vulnerabilidad, abre un issue sin detalles explotables y pide un canal privado,
o usa *Report a vulnerability* en la pestaña *Security* del repositorio.
