# Homebridge (HomeKit) sin Home Assistant

El aparato publica por MQTT igual que para Home Assistant; lo que cambia es
quien escucha. Con [`homebridge-mqttthing`](https://github.com/arachnetech/homebridge-mqttthing)
acaba en la app **Casa** de Apple y en Siri.

Configuracion lista para pegar: [`homebridge.json`](homebridge.json).
Las funciones `apply` estan probadas contra el JSON real del aparato.

## Montaje

1. **Broker**: montarlo como explica [MQTT.md](MQTT.md). Con Homebridge deja
   el **prefijo de descubrimiento vacio** en el panel del aparato: el
   autodescubrimiento solo lo entiende Home Assistant, y si no, quedan once
   mensajes retenidos en el broker que no consume nadie.

2. **Plugin**: desde la interfaz de Homebridge, o por consola si es la
   instalacion oficial con Node propio en `/opt/homebridge`:

   ```bash
   hb-service add homebridge-mqttthing
   ```

3. **Aparato**: en su panel web, broker `mqtt://IP-DE-LA-DIETPI:1883` con ese
   usuario y contrasena. Al guardar se reinicia.

4. **Homebridge**: pegar los dos accesorios de `homebridge.json` dentro del
   array `accessories` de tu `config.json` y reiniciar.

> **Editar estos accesorios SOLO como JSON.** El formulario de la interfaz de
> Homebridge no sabe representar los temas con funcion `apply`, y al guardar
> desde ahi —aunque solo se cambie el nombre— se lleva por delante el bloque
> `topics` entero. El accesorio sigue apareciendo en HomeKit, pero ya no lee
> nada, y en el registro salen errores del tipo `Cannot read properties of
> undefined (reading 'getAirQuality')`. Usar el editor de JSON de la interfaz
> (Config > JSON) o el fichero a mano.

## Que se ve y que no

| Metrica | En HomeKit |
|---|---|
| Nivel global | Calidad del aire, escala 1-5 (mapeo 1:1 con los cinco del firmware) |
| PM2.5, PM10 | Densidades, en el detalle del accesorio |
| CO2 | Nivel en ppm + accesorio de CO2 que avisa por encima de 1200 ppm |
| Temperatura, humedad | Servicios dentro del mismo accesorio |
| **VOC y NOx** | **No aparecen** |
| Bateria | Nivel, estado de carga y aviso de bateria baja, si hay celda |

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
  "apply": "const v=JSON.parse(message).voc; return v==null?state:v;"
},
```

y `"history": true`. Sabiendo que ese numero es un indice disfrazado de ug/m3.

## La bateria

Homebridge **no usa el autodescubrimiento**, asi que la bateria no aparece
sola como en Home Assistant: hay que darle los temas, y ya vienen en
`homebridge.json`. mqttthing anade un **servicio de bateria** a cualquier
accesorio en cuanto ve `getBatteryLevel`, `getChargingState` o
`getStatusLowBattery`.

Van solo en el accesorio de calidad del aire, no en el de CO2: en los dos, la
app Casa enseñaria dos baterias para el mismo aparato.

Si el aparato **no lleva celda**, esos campos no salen en el JSON y las
funciones `apply` devuelven el valor anterior, asi que no molestan; pero si
nunca vas a ponerle bateria, lo limpio es quitar los tres temas.

En la app Casa la bateria no es un icono aparte: sale **dentro del accesorio**,
en sus ajustes. Y si acabas de anadirla, la app puede tardar en enterarse de
que el accesorio tiene un servicio nuevo; forzar el cierre de la app suele
bastar.

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
