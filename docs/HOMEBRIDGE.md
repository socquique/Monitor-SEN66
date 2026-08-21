# Homebridge (HomeKit) sin Home Assistant

El aparato publica por MQTT igual que para Home Assistant; lo que cambia es
quien escucha. Con [`homebridge-mqttthing`](https://github.com/arachnetech/homebridge-mqttthing)
acaba en la app **Casa** de Apple y en Siri.

Configuracion lista para pegar: [`homebridge.json`](homebridge.json).
Las funciones `apply` estan probadas contra el JSON real del aparato.

## Montaje

1. **Broker en la DietPi.** `dietpi-software` NO lo trae en su catalogo
   (comprobado en DietPi 10.6): va por apt.

   ```bash
   apt-get install -y mosquitto mosquitto-clients
   ```

   Mosquitto 2.x de fabrica **solo escucha en localhost y rechaza conexiones
   anonimas**: recien instalado el monitor no conecta y no dice por que.

   ```bash
   cat > /etc/mosquitto/conf.d/local.conf <<'EOF'
   listener 1883 0.0.0.0
   allow_anonymous false
   password_file /etc/mosquitto/passwd
   EOF
   mosquitto_passwd -c /etc/mosquitto/passwd monitor
   chown mosquitto:mosquitto /etc/mosquitto/passwd
   chmod 0600 /etc/mosquitto/passwd
   systemctl restart mosquitto
   ```

   Dos piedras con las que se tropieza seguro:

   - **No repetir `persistence` ni `persistence_location`** en `conf.d`: ya
     vienen en el `mosquitto.conf` del paquete de Debian y el servicio se
     niega a arrancar con `Duplicate persistence_location value`.
   - **El fichero de contrasenas debe ser `mosquitto:mosquitto` y 0600.** Con
     `root:root` el broker sale con estado 13 (permiso denegado). Confunde que
     `mosquitto_passwd`, corriendo como root, avise justo de lo contrario: el
     que manda es el broker, que corre como `mosquitto`.

2. **Plugin**: desde la interfaz de Homebridge, o por consola si es la
   instalacion oficial con Node propio en `/opt/homebridge`:

   ```bash
   hb-service add homebridge-mqttthing
   ```

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
  "topic": "sen66-XXXXXX/state",
  "apply": "return JSON.parse(message).voc;"
},
```

y `"history": true`. Sabiendo que ese numero es un indice disfrazado de ug/m3.

## El aviso de Node

Homebridge avisa de que el plugin pide Node 18/20/22 y tu tienes 24:

```
The plugin "homebridge-mqttthing" requires a Node.js version of
^18.12.0 || ^20.10.0 || ^22.11.0 which does not satisfy the current
Node.js version of v24.19.0
```

**Es un aviso, no un fallo, y se puede ignorar.** No hay version que lo
arregle: la 1.1.49 es de enero de 2026 y sigue declarando como maximo el 22.
Comprobado con `logMqtt` activado sobre Node 24.19.0 y Homebridge 2.4.0:
recibe los mensajes cada 10 s, las funciones `apply` los decodifican y las
caracteristicas se actualizan.

```
[CO2] Received MQTT: sen66-XXXXXX/state = {"co2":470,...}
[CO2] apply() function decoded message to [NORMAL]
[CO2] apply() function decoded message to [470]
```

Bajar Node a 22 para callar el aviso arrastraria a todos los demas plugins,
que ahora funcionan en 24: no compensa. Eso si, el autor no ha probado en 24,
asi que si algun dia el plugin hace algo raro, esto es lo primero que mirar.

## Notas

- El umbral de 1200 ppm del aviso de CO2 esta en el `apply`, no en el
  firmware: se cambia en el `config.json` y basta reiniciar Homebridge.
- El firmware publica igualmente los mensajes de autodescubrimiento de Home
  Assistant bajo `homeassistant/...`. Sin HA se quedan ahi retenidos sin
  molestar, y si algun dia anades HA el aparato aparece solo. Para no
  publicarlos, **vaciar el prefijo** en el panel web.
- El tema de estado se publica **retenido**, asi que al reiniciar Homebridge
  los accesorios recuperan el ultimo valor al instante. Sin eso, mqttthing
  arranca avisando de `characteristic value ... received "undefined"` hasta
  que llega el siguiente envio.
- El aparato **no depende de nada de esto**: la pantalla y su panel web
  funcionan aunque la DietPi este apagada.
