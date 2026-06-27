# Protocolo RDT-UDP para Entrenamiento Distribuido de Redes Neuronales

## 1. Objetivo Técnico

Este documento especifica una capa de transporte confiable personalizada (**RDT-UDP**) implementada sobre datagramas UDP en el espacio de aplicación.

El protocolo incorpora mecanismos de confiabilidad para mitigar las limitaciones inherentes de UDP, tales como:

* Pérdida de paquetes en canales saturados dueños de alta latencia.
* Retrasos por congestión de tráfico en redes locales.
* Corrupción de datos por ruido electromagnético, atenuación o colisiones.

La implementación sigue de forma estricta el protocolo **Go-Back-N (GBN)** mediante ventanas deslizantes y transmisión en *pipeline*, permitiendo una comunicación concurrente y asíncrona entre un Nodo Maestro y un número dinámico de Nodos Esclavos.

---

## 2. Especificación de la Unidad de Transmisión

Para evitar desalineamientos en memoria entre arquitecturas de hardware heterogéneas y reducir la fragmentación en la capa IP, el protocolo utiliza una estructura binaria empaquetada mediante la directiva `__attribute__((packed))` en C++.

Cada datagrama posee un tamaño fijo y constante de **523 bytes**, garantizando que permanezca dentro de la MTU estándar de Ethernet de 1500 bytes para evitar la sobrecarga por reensamblado IP.

### Formato del datagrama

| Campo | Tipo | Tamaño | Descripción |
| --- | --- | --- | --- |
| FLAGS | `uint8_t` | 1 B | Tipo de trama y control de estado |
| SEQ_NUM | `uint32_t` | 4 B | Número de secuencia en el pipeline |
| DATA_LEN | `uint32_t` | 4 B | Tamaño del payload válido |
| PAYLOAD | `char[512]` | 512 B | Datos del dataset o estructuras JSON serializadas |
| CHECKSUM | `uint16_t` | 2 B | Verificación de integridad basada en RFC 1071 |

---

## 3. Descripción de Campos

### 3.1 FLAGS (`uint8_t`)

Indica la semántica operacional del datagrama dentro de la máquina de estados finitos.

| Valor | Tipo | Descripción |
| --- | --- | --- |
| 1 | START | Sincronización inicial, reinicio de secuencias a 0 e inicialización de buffers |
| 2 | DATA | Fragmento de datos de la carga útil principal |
| 3 | END | Finalización de transmisión del segmento o flujo actual |
| 4 | ACK | Confirmación de recepción secuencial acumulativa |

---

### 3.2 SEQ_NUM (`uint32_t`)

Número de secuencia incremental continuo `0, 1, 2, ...`

Se utiliza para gobernar los límites espaciales de la ventana Go-Back-N, reordenar los paquetes en el receptor y permitir la detección y descarte inmediato de tramas duplicadas. Las secuencias se inicializan estrictamente en 0 con cada bandera START al inicio de una nueva fase o subfase de transporte.

---

### 3.3 DATA_LEN (`uint32_t`)

Indica la cantidad exacta de bytes útiles contenidos en el campo PAYLOAD de 0 a 512 bytes.

Este campo es indispensable para procesar correctamente el último bloque de una transmisión o cadenas JSON variables, evitando que el receptor interprete memoria residual del buffer del sistema operativo. Para paquetes de control puro como START, END y ACK, este campo debe configurarse explícitamente en 0.

---

### 3.4 PAYLOAD (`char[512]`)

Buffer principal de almacenamiento de datos de tamaño fijo.

Dependiendo de la fase operativa del clúster, transporta de forma empaquetada:

* Fragmentos secuenciales del archivo estructurado `Diabetes.csv`.
* Fragmentos secuenciales de cadenas serializadas en formato JSON contentivas de las métricas de rendimiento local del modelo.

---

### 3.5 CHECKSUM (`uint16_t`)

Mecanismo de detección de errores basado en el algoritmo aritmético **Internet Checksum (RFC 1071)**.

```
Checksum = Complemento a 1 de la suma en complemento a 1 de todas las palabras de 16 bits del datagrama.

```

Argumento técnico: Se descarta el uso de sumas modulares de 8 bits debido a su alta tasa de colisiones ante errores simétricos. El formato de 16 bits asegura la detección de inversiones complejas de bits. El procesamiento en C++ realiza la suma tratando el buffer de memoria exclusivamente como un arreglo de elementos sin signo `uint16_t` o `uint8_t` para prevenir distorsiones por signo.

---

## 4. Máquina de Estados y Confiabilidad

### 4.1 Detección de Corrupción

Al arribo de un datagrama al socket:

1. El backend en C++ recalcula el Internet Checksum de 16 bits sobre la estructura completa.
2. Si la verificación binaria falla, es decir, el resultado de la suma checksum es diferente de cero:

* El paquete se descarta de forma silenciosa e inmediata.
* No se altera ningún puntero de estado del receptor.
* No se despacha ninguna trama de confirmación.

La ausencia de un ACK válido provoca la expiración del temporizador del emisor y la consecuente retransmisión de la ventana.

---

### 4.2 Go-Back-N (Ventana Deslizante)

El flujo de transporte de datos se gestiona en modo pipeline para maximizar el factor de utilización del canal:

**Emisor**

* Mantiene una ventana de transmisión de tamaño fijo parametrizado de forma global, por ejemplo, `N = 8`.
* Está facultado para transmitir hasta `N` paquetes consecutivos sin detenerse a esperar confirmaciones.
* Gestiona las variables de control de secuencia: `base`, que representa el paquete enviado no confirmado más antiguo, y `next_seq_num`, que es la siguiente secuencia disponible para transmisión.

**Receptor**

Mantiene una lógica estrictamente secuencial gobernada por la variable:

* `expected_seq`

Si un paquete íntegro es recibido y su número de secuencia satisface la igualdad `SEQ_NUM == expected_seq`, los datos se aceptan, se transfieren al buffer de aplicación y `expected_seq` se incrementa en una unidad.

---

### 4.3 Control de Tiempo y Demultiplexación Concurrente

#### Temporizador mediante `select()`

El emisor gestiona un único temporizador lógico activo asociado exclusivamente al paquete ubicado en el límite inferior de la ventana `base`.

Para implementar este temporizador de forma no bloqueante, el código en C++ utiliza la función de bajo nivel `select()`. Se configura una estructura de tiempo `timeval` con un límite crítico de **200 ms** acoplada al descriptor del socket UDP.

* Si `select()` determina que hay datos listos en el socket antes de que expire el tiempo, se procesa el ACK entrante de manera inmediata.
* Si `select()` retorna 0, se declara un evento de Timeout. Se asume la pérdida o degradación de la trama en el canal y el emisor ejecuta una retransmisión en ráfaga de todos los paquetes comprendidos en el intervalo activo:

```
base ... next_seq_num - 1

```

#### Demultiplexación por Software

El Nodo Maestro centraliza la recepción de datos concurrentes provenientes de múltiples terminales independientes a través de un único puerto socket UDP de escucha.

Para evitar la pérdida de datagramas por desbordamiento del búfer del sistema operativo y colisiones de hilos en memoria, el backend implementa un **hilo receptor asíncrono no bloqueante**. Este hilo ejecuta de manera continua lecturas del socket, identifica el origen de la trama mediante el par ordenado `(IP_origen, Puerto_origen)` y desvía los paquetes válidos hacia colas de memoria independientes `std::queue` asociadas dinámicamente a cada nodo. El acceso mutuo a estas colas entre el hilo receptor y el hilo de la aplicación se encuentra estrictamente sincronizado mediante mecanismos de exclusión mutua `std::mutex`.

#### Umbral de aborto

Se establece un límite crítico de **15 retransmisiones consecutivas** para un paquete antes de declarar al nodo en estado de desconexión forzada por razones de seguridad y estabilidad del clúster.

---

### 4.4 NACK Implícito y Reglas de Excepción

El protocolo optimiza el ancho de banda omitiendo mensajes NACK explícitos, implementando una lógica de control de errores implícita:

#### ACK acumulativos

Cada ACK transmitido confirma la recepción exitosa y ordenada de todos los paquetes consecutivos hasta el número de secuencia indicado en su campo `SEQ_NUM`. Al recibirlo, el emisor desplaza el límite inferior de su ventana: `base = SEQ_NUM + 1`.

#### Paquetes corruptos o fuera de orden

Si un paquete presenta un checksum inválido o un número de secuencia desalineado donde `SEQ_NUM > expected_seq`, el receptor ejecuta la siguiente lógica defensiva:

* Descarta el paquete de forma inmediata.
* **Regla de Excepción por Underflow:** Si `expected_seq == 0`, el receptor no envía ninguna respuesta y se mantiene a la espera. Si `expected_seq > 0`, el receptor retransmite obligatoriamente un ACK de control con el valor:

```
expected_seq - 1

```

La llegada de ACKs duplicados al emisor, acoplada a la expiración del temporizador de 200 ms gestionado por `select()`, constituye un NACK implícito que fuerza la retransmisión de la ventana activa.

---

## 5. Flujo de Trabajo del Clúster Dinámico

### 5.1 Fase de Distribución

El módulo centralizado `maestro.py` lee dinámicamente el número de esclavos activos `K` desde la configuración global del sistema. El maestro opera estrictamente como coordinador y delegador de carga:

1. Envía de manera asíncrona un paquete con la bandera `START` (Flag 1) a cada esclavo para inicializar los estados de red locales a cero.
2. Lee el archivo fuente `Diabetes.csv`.
3. Divide el conjunto total de muestras en partes matemáticamente homogéneas `M/K`.
4. Asigna el residuo de la división entera al último nodo registrado para balancear la carga.
5. Distribuye de forma asíncrona cada partición utilizando el pipeline confiable Go-Back-N.

---

### 5.2 Cómputo Local Síncrono y Sincronización de Cierre

Cada nodo esclavo se ejecuta de forma aislada en su propia terminal o host independiente:

1. Recupera y reconstruye el segmento de datos binarios en su espacio de memoria local.
2. **Barrera de Sincronización del Receptor:** Tras enviar el ACK del paquete `END` (Flag 3) de la fase de distribución, el esclavo permanece en un estado de escucha pasiva durante 500 ms antes de liberar la lógica de red. Esto garantiza que si el ACK de cierre se perdió, el esclavo pueda responder a las retransmisiones del paquete `END` del maestro, evitando abortos falsos por timeout.
3. Inicializa el modelo matemático de la red neuronal utilizando el motor de PyTorch.
4. Recupera la variable de configuración global de Épocas y ejecuta el entrenamiento local durante el ciclo establecido.

Cada lote ejecuta de forma síncrona las etapas de Forward Pass, CrossEntropyLoss, Backpropagation y optimización de pesos mediante el algoritmo Adam.

---

### 5.3 Recolección Fragmentada y Sincronización de Retorno

Al concluir la última época de entrenamiento configurada, cada nodo esclavo serializa sus matrices y vectores numéricos en una cadena en formato JSON. Dado que el tamaño de esta estructura supera la capacidad de una única trama, el JSON se trata como un stream continuo de datos:

1. El esclavo envía al maestro un paquete `START` para inicializar la subfase de recolección y resetear secuencias a cero en ambos extremos de esa conexión particular.
2. La cadena JSON se fragmenta en bloques secuenciales de hasta 512 bytes, transmitidos bajo la bandera `DATA` (Flag 2) gobernados estrictamente por la ventana de pipeline GBN.
3. Al despachar el último bloque, se adjunta la bandera `END` (Flag 3).
4. **Barrera de Sincronización del Emisor:** Tras la transmisión del paquete `END` de métricas, el esclavo mantiene activo su módulo de retransmisión por temporizador durante un tiempo de guarda pasivo de 500 ms a la espera del ACK final del maestro, asegurando la recepción íntegra en el nodo central antes de terminar la ejecución de la terminal.

Las métricas transmitidas en el JSON unificado incluyen el historial de Loss acumulado por lote, el historial de Accuracy acumulado por lote, los vectores de prueba reales `y_true` y los vectores de predicción generados por el modelo `y_pred`.

---

## 5.4 Consolidación

El Nodo Maestro:

1. Extrae de forma ordenada las métricas desde las colas de recepción demultiplexadas por software en C++.
2. Ensambla de forma estrictamente indexada los fragmentos de payload de cada nodo guiado por el orden del campo `SEQ_NUM` de los paquetes DATA validados, deteniéndose al procesar la bandera `END`, procediendo a decodificar el string JSON resultante.
3. Consolida y unifica linealmente los resultados provenientes de los `K` esclavos concurrentes.
4. Genera las visualizaciones analíticas globales del entrenamiento manteniendo el formato de salida requerido: curvas globales de convergencia de Loss, curvas globales de convergencia de Accuracy y la matriz de confusión unificada del clúster distribuido.
