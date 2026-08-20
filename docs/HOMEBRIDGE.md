# Homebridge (HomeKit) sin Home Assistant

El aparato publica por MQTT igual que para Home Assistant; lo que cambia es
quien escucha. Con [`homebridge-mqttthing`](https://github.com/arachnetech/homebridge-mqttthing)
acaba en la app **Casa** de Apple y en Siri.

Configuracion lista para pegar: [`homebridge.json`](homebridge.json).
Las funciones `apply` estan probadas contra el JSON real del aparato.

## Montaje

1. **Broker en la DietPi** (Mosquitto ocupa 5-10 MB de RAM):

   ```bash
   dietpi-software list | grep -i mosquitto   # saca el ID
   dietpi-software install <ID>
   ```

   Mosquitto 2.x de fabrica **solo escucha en localhost y rechaza conexiones
   anonimas**: recien instalado el monitor no conecta y no dice por que.

   ```bash
   sudo tee /etc/mosquitto/conf.d/local.conf <<'EOF'
   listener 1883 0.0.0.0
   allow_anonymous false
   password_file /etc/mosquitto/passwd
   EOF
   sudo mosquitto_passwd -c /etc/mosquitto/passwd monitor
   sudo systemctl restart mosquitto
   ```

2. **Plugin**: instalar `homebridge-mqttthing` desde la interfaz de Homebridge.

3. **Aparato**: en su panel web, broker `mqtt://IP-DE-LA-DIETPI:1883` con ese
   usuario y contrasena. Al guardar se reinicia.

4. **Homebridge**: pegar los dos accesorios de `homebridge.json` dentro del
   array `accessories` de tu `config.json` y reiniciar.

## Que se ve y que no

| Metrica | En HomeKit |
|---|---|
| Nivel global | Calidad del aire, escala 1-5 (mapeo 1:1 con los cinco del firmware) |
| PM2.5, PM10 | Densidades, en el detalle del accesorio |
| CO2 | Nivel en ppm + accesorio de CO2 que avisa por encima de 1200 ppm |
| Temperatura, humedad | Servicios dentro del mismo accesorio |
| **VOC y NOx** | **No aparecen** |

VOC y NOx se quedan fuera a proposito: HomeKit espera densidades en ug/m3 y
el SEN66 da *indices* adimensionales (1-500). Publicarlos como si fueran una
densidad seria inventarse la unidad. Se siguen viendo en la pantalla del
aparato y en su panel web.

**Excepcion, si quieres graficas**: la app Casa no guarda historico, pero la
app **Eve** si, y para eso mqttthing exige `getVOCDensity`. Si te compensa,
anade al accesorio de calidad del aire:

```json
"getVOCDensity": {
  "topic": "sen66-8625d4/state",
  "apply": "return JSON.parse(message).voc;"
},
```

y `"history": true`. Sabiendo que ese numero es un indice disfrazado de ug/m3.

## Notas

- El umbral de 1200 ppm del aviso de CO2 esta en el `apply`, no en el
  firmware: se cambia en el `config.json` y basta reiniciar Homebridge.
- El firmware publica igualmente los mensajes de autodescubrimiento de Home
  Assistant bajo `homeassistant/...`. Sin HA se quedan ahi retenidos sin
  molestar; para limpiarlos, vaciar el prefijo en el panel web.
- El aparato **no depende de nada de esto**: la pantalla y su panel web
  funcionan aunque la DietPi este apagada.
