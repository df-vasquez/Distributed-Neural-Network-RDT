# Distributed-Neural-Network-RDT
Distributed neural network training using C++ workers and custom RDT over UDP
## PROJECT SPECIFICATION & PROTOCOL DESIGN
**Course / Curso:** Redes y Comunicaciones  
**Reference Textbook / Libro de Referencia:** Computer Networking: A Top-Down Approach (Kurose & Ross)  

### TEAM MEMBERS / INTEGRANTES:
* Chávez, Jorge
* Cornejo, Gabriel
* Mendoza, Mariela
* Vásquez, Diego

---

# [EN] ENGLISH VERSION

## 1. ARCHITECTURAL OVERVIEW & NODE ROLES
The system implements a multi-tier, Master-Worker (Master-Slave) distributed architecture designed to offload heavy matrix algebra operations from a Deep Learning training loop to multiple remote terminals over a Local Area Network (LAN).

* **Python Master:** Coordinates the neural network training loop (360 epochs). It loads the Diabetes CSV dataset, manages the network graph (`MulticlassClassifier`), and intercepts linear layer transformations to offload them via `pybind11`.
* **C++ Master Bridge:** Receives matrix memory pointers from Python. It sections large multi-dimensional tensors into discrete chunks ($D_n$) of fixed size, encapsulates them into RDT packets, and transmits them via non-blocking UDP sockets. It handles logical timers, ACKs, NACKs, and retransmissions.
* **C++ Slaves / Workers:** 3 to 4 standalone terminals listening on UDP ports. They act as stateless matrix engines that parse 500-byte streams, execute floating-point matrix multiplications, and return results back using RDT encapsulation.

---

## 2. STRICT DATAGRAM SPECIFICATION (500 BYTES FIXED FLUSH)
To comply with the fixed buffer size requirement and ensure seamless C++ memory structure mapping, every network packet has a strict limit of 500 bytes.

### 2.1. Datagram Byte Layout Table
| Field Name | Byte Range | Data Type | Description |
| :--- | :--- | :--- | :--- |
| `PACKET_TYPE` | Byte 0 | `uint8_t` | Control Flag: `0x01` = DATA, `0x02` = ACK, `0x03` = NACK |
| `SEQUENCE_NUMBER` | Bytes 1 - 4 | `uint32_t` | Monotonically increasing ID for packet sequencing and ordering |
| `PAYLOAD_SIZE` | Bytes 5 - 6 | `uint16_t` | Indicates the actual length of valid matrix data (Max 461 Bytes) |
| `HASH_CHECKSUM` | Bytes 7 - 38 | `char[32]` | SHA-256 cryptographic signature computed over the unpadded payload |
| `PAYLOAD_DATA` | Bytes 39 - 499| `uint8_t[461]`| Serialized matrix elements + zero-byte padding (`\0`) |

### 2.2. Padding and Parsing Logic
* **Padding Rule:** If a remaining matrix slice only requires 70 bytes of float representations, `PAYLOAD_SIZE` is explicitly set to `70`. The remaining `391` bytes within `PAYLOAD_DATA` are forced to zero (`\0`) to preserve the absolute 500-byte envelope.
* **Parsing Rule:** Upon receipt, the network buffer reads `PAYLOAD_SIZE`, extracts exactly that amount of bytes from the data offset (Byte 39), and discards the remaining padding bytes before reconstructing the tensor.

---

## 3. RDT MECHANISMS & KARN'S ALGORITHM
To convert raw UDP into a reliable transport system, we implement custom RDT mechanisms following Kurose's specifications:

* **Sequence Numbers:** Used by receivers to safely reassemble multi-dimensional tensors in correct mathematical order and detect/discard duplicate packets.
* **Hash Checksum (Corruption Detection):** SHA-256 string compared at destination. If the calculated signature diverges from the header's `HASH_CHECKSUM`, the packet is instantly dropped.
* **ACK & NACK Flow:** Safe packets trigger an immediate ACK (`0x02`) with their sequence ID. Corrupted packets trigger a fast-retransmit NACK (`0x03`) to bypass the timeout window.
* **Karn's Algorithm for Timers:** 1. *Retransmission Ambiguity:* SampleRTT measurements are strictly forbidden for any packet that has undergone a timeout and retransmission. 
  2. *Exponential Backoff:* If a timeout occurs, the Retransmission Timeout (RTO) is immediately doubled to avoid saturating congested switches. The timer resets to its adaptive baseline only when a packet succeeds on its very first try.

---

## 4. MATHEMATICAL INTERACTION (FORWARD & BACKPROPAGATION)
Network offloading occurs during two vital stages inside the computational graph of each epoch:
* **Forward Pass Distribution (Inference):** Input matrices ($X$) are multiplied by layer weights ($W$). The Master chunks $X$ into sub-matrices ($D_1, D_2, \dots, D_n$). Workers solve partial dot products and return the raw prediction scores.
* **Backpropagation Distribution (Weight Upgrades):** To minimize loss, gradients are updated via:
  $$\frac{\partial L}{\partial W} = X^T \cdot \delta$$
  The Python layer calculates the error vector ($\delta$). The C++ Master distributes both $\delta$ and the transposed structural activations ($X^T$). Workers return the "newly calculated data" (partial matrix derivatives) so Python can securely execute `optimizer.step()`.

---
---

# [ES] VERSIÓN EN ESPAÑOL

## 1. ARQUITECTURA GENERAL Y ROLES DE LOS NODOS
El proyecto implementa una arquitectura distribuida Maestro-Trabajador (Maestro-Esclavo) de múltiples niveles diseñada para descargar operaciones pesadas de álgebra matricial desde el bucle de entrenamiento de Deep Learning hacia terminales remotas en una Red de Área Local (LAN).

* **Maestro Python:** Coordina el ciclo de entrenamiento de la red neuronal (360 épocas). Carga el conjunto de datos CSV de Diabetes, maneja el grafo de la red (`MulticlassClassifier`) e intercepta las transformaciones de las capas lineales para delegarlas mediante `pybind11`.
* **Puente Maestro C++ :** Recibe punteros de memoria de las matrices desde Python. Segmenta los grandes tensores multidimensionales en bloques discretos ($D_n$) de tamaño fijo, los encapsula en paquetes RDT y los transmite mediante sockets UDP no bloqueantes. Administra temporizadores lógicos, ACKs, NACKs y retransmisiones.
* **Esclavos / Trabajadores C++:** De 3 a 4 terminales independientes escuchando en puertos UDP. Actúan estrictamente como motores de cálculo matricial sin estado que parsean flujos de 500 bytes, ejecutan multiplicaciones de matrices de punto flotante y devuelven los resultados empaquetados bajo RDT.

---

## 2. ESPECIFICACIÓN ESTRICTA DEL DATAGRAMA (500 BYTES FIJOS)
Para cumplir con la restricción de un buffer de tamaño fijo y asegurar el mapeo directo de estructuras de memoria en C++, cada paquete de red tiene un límite estricto de 500 bytes.

### 2.1. Tabla de Disposición de Bytes del Datagrama
| Nombre del Campo | Rango de Bytes | Tipo de Dato | Descripción |
| :--- | :--- | :--- | :--- |
| `PACKET_TYPE` | Byte 0 | `uint8_t` | Bandera de Control: `0x01` = DATA, `0x02` = ACK, `0x03` = NACK |
| `SEQUENCE_NUMBER` | Bytes 1 - 4 | `uint32_t` | ID incremental para el ordenamiento y secuenciación de los paquetes |
| `PAYLOAD_SIZE` | Bytes 5 - 6 | `uint16_t` | Indica la longitud real de los datos matemáticos válidos (Máx 461 Bytes) |
| `HASH_CHECKSUM` | Bytes 7 - 38 | `char[32]` | Firma criptográfica SHA-256 calculada sobre el contenido sin padding |
| `PAYLOAD_DATA` | Bytes 39 - 499| `uint8_t[461]`| Elementos de la matriz serializados + relleno de bytes en cero (`\0`) |

### 2.2. Lógica de Relleno (Padding) y Parseo
* **Regla de Padding:** Si una porción restante de una matriz solo requiere 70 bytes, `PAYLOAD_SIZE` se establece explícitamente en `70`. Los restantes `391` bytes dentro de `PAYLOAD_DATA` se fuerzan a cero (`\0`) para mantener el paquete en exactamente 500 bytes.
* **Regla de Parseo:** Al recibir el paquete, el buffer lee `PAYLOAD_SIZE`, extrae exactamente esa cantidad de bytes desde el desplazamiento de datos (Byte 39) y descarta los bytes de relleno restantes antes de reconstruir el tensor.

---

## 3. MECANISMOS RDT Y ALGORITMO DE KARN
Para transformar UDP puro en un sistema de transporte confiable, implementamos mecanismos RDT personalizados basados en las especificaciones de Kurose:

* **Números de Secuencia:** Utilizados por los receptores para reensamblar de forma segura los tensores en el orden matemático correcto y detectar/descartar paquetes duplicados.
* **Hash Checksum (Detección de Corrupción):** Cadena SHA-256 comparada en el destino. Si la firma calculada diverge del `HASH_CHECKSUM` de la cabecera, el paquete es descartado inmediatamente.
* **Flujo de ACK y NACK:** Los paquetes íntegros disparan un ACK inmediato (`0x02`) con su número de secuencia. Los paquetes corruptos disparan un NACK de retransmisión rápida (`0x03`) para mitigar el tiempo de espera del temporizador.
* **Algoritmo de Karn para Temporizadores:** 1. *Ambigüedad de Retransmisión:* Se prohíbe estrictamente tomar mediciones de SampleRTT para cualquier paquete que haya sufrido una pérdida y posterior retransmisión.
  2. *Respaldo Exponencial (Exponential Backoff):* Si ocurre un timeout, el tiempo de espera (RTO) se duplica inmediatamente para evitar saturar los switches de red congestionados. El temporizador vuelve a su línea base adaptativa solo cuando un paquete se transmite con éxito en su primer intento.

---

## 4. INTERACCIÓN MATEMÁTICA DISTRIBUIDA (FORWARD Y BACKPROPAGATION)
La descarga de procesamiento hacia la red ocurre en dos etapas vitales dentro del grafo computacional de cada época:
* **Distribución en Forward Pass (Inferencia):** Las matrices de entrada ($X$) se multiplican por los pesos de la capa ($W$). El Maestro segmenta $X$ en submatrices ($D_1, D_2, \dots, D_n$). Los trabajadores resuelven los productos punto parciales y devuelven las puntuaciones crudas de predicción.
* **Distribución en Backpropagation (Actualización de Pesos):** Para minimizar la pérdida, los gradientes se actualizan mediante la ecuación:
  $$\frac{\partial L}{\partial W} = X^T \cdot \delta$$
  La capa de Python calcula el vector de error ($\delta$). El Maestro C++ distribuye tanto $\delta como las activaciones estructurales transpuestas ($X^T$). Los esclavos retornan los "nuevos datos calculados" (derivadas matriciales parciales) para que Python pueda ejecutar de manera segura el método `optimizer.step()`.
