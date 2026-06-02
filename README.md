# Sistema de Entrenamiento Distribuido de Redes Neuronales sobre el Protocolo RDT-UDP

Este proyecto implementa una arquitectura distribuida de aprendizaje federado mediante el método de Paralelismo de Datos (Data Parallelism). El sistema permite el entrenamiento de una red neuronal artificial para la clasificación de diabetes utilizando un canal de transporte no confiable (UDP). Para garantizar la integridad, orden y control de pérdidas de los datos transferidos, se ha diseñado un protocolo personalizado de Transferencia Confiable de Datos (RDT) en C++, integrado nativamente en Python a través de la biblioteca Pybind11.

**Course / Curso:** Redes y Comunicaciones  
**Reference Textbook / Libro de Referencia:** Computer Networking: A Top-Down Approach (Kurose & Ross)  

### TEAM MEMBERS / INTEGRANTES:
* Chávez, Jorge
* Cornejo, Gabriel
* Mendoza, Mariela
* Vásquez, Diego

A continuación se presenta la documentación técnica y el manual de despliegue estructurado en formato `README.md`.

---

```

---

## 1. Estructura del Proyecto

El repositorio está organizado bajo el siguiente árbol de directorios para garantizar el aislamiento de los recursos y la modularidad del software:

```text
proyecto_ia_distribuida/
│
├── dataset/
│   └── Diabetes.csv               # Conjunto de datos original (1000 registros, 14 características)
│
├── maestro/
│   ├── rdt_master.cpp             # Lógica RDT en C++ para segmentación, recepción y agregación
│   ├── rdt_master.hpp             # Definiciones de cabeceras del nodo maestro
│   ├── setup.py                   # Script de compilación Pybind11 para el módulo maestro
│   └── maestro.py                 # Orquestador de IA, carga de datos y actualización de parámetros
│
└── esclavo/
    ├── rdt_slave.cpp              # Lógica RDT en C++ para recepción de datos y envío de gradientes
    ├── rdt_slave.hpp              # Definiciones de cabeceras del nodo esclavo
    ├── setup.py                   # Script de compilación Pybind11 para el módulo esclavo
    └── esclavo.py                 # Procesamiento local de IA (Forward y Backward Pass)

```

---

## 2. Especificación del Protocolo de Aplicación RDT

Debido a que el protocolo base UDP opera sin estado y no garantiza la entrega de datagramas, la capa de transporte del proyecto implementa un protocolo RDT acotado a un tamaño estricto de **500 bytes por datagrama**.

### 2.1. Anatomía del Datagrama (Encapsulación)

Cada paquete transmitido por la red se divide de manera fija en dos secciones fundamentales: Cabecera (Header) y Carga Útil (Payload).

$$\text{Datagrama Total (500 bytes)} = \text{Cabecera (7 bytes)} + \text{Carga Útil (493 bytes)}$$

* **Cabecera (Bytes 0 al 6):**
* `Byte 0 [1 Byte] - Checksum (Hash):` Valor numérico obtenido mediante la suma modular de la carga útil (`Suma % 7`). Se utiliza para la detección de errores por inversión de bits o corrupción en el canal físico. Si el receptor calcula un Hash distinto, el paquete es descartado.
* `Bytes 1-2 [2 Bytes] - Flags de Control (Banderas):` Controlan la fragmentación y el reensamblado de estructuras extensas.
* `01`: Indica el datagrama inicial de una ráfaga de datos.
* `00`: Indica un datagrama intermedio.
* `11`: Indica el datagrama final de la secuencia.


* `Bytes 3-6 [4 Bytes] - Número de Secuencia (SEQ):` Valor numérico incremental codificado en texto con relleno de ceros (ej. `0000`, `0001`). Permite al receptor ordenar los fragmentos en el búfer de la aplicación, mitigando el problema del desorden inherente a UDP.


* **Carga Útil (Bytes 7 al 499):**
* `Payload [493 Bytes]:` Contiene los datos crudos serializados (subconjuntos del dataset, vectores de pesos sintácticos o matrices de gradientes). Los espacios remanentes no utilizados se rellenan de manera uniforme con el carácter `#` para forzar el tamaño estricto del datagrama.



### 2.2. Control de Pérdidas y Temporización (Algoritmo de Karn)

Para gestionar la pérdida de paquetes sin saturar la red ni generar estimaciones falsas de retraso, el nodo maestro implementa un mecanismo de Temporizador de Retransmisión (RTO) fundamentado en el Algoritmo de Karn:

1. Se mide el tiempo de ida y vuelta (RTT) únicamente de aquellos datagramas que son confirmados con éxito en su primera transmisión.
2. Si un temporizador expira y ocurre una retransmisión, se aplica un retroceso exponencial (Exponential Backoff), duplicando el valor del temporizador para el siguiente intento.
3. Se prohíbe explícitamente calcular el RTT a partir de paquetes retransmitidos, evitando la ambigüedad en el reconocimiento de las confirmaciones.

---

## 3. Flujo Macroscópico del Sistema (Una Sola Época)

El proceso de entrenamiento distribuido se ejecuta en un único ciclo síncrono estructurado en tres fases secuenciales:

### Fase 1: Segmentación y Distribución del Dataset

1. El archivo `Diabetes.csv` es cargado en la memoria del script `maestro.py`.
2. El dataset se fragmenta en partes proporcionales según el número de nodos esclavos concurrentes en el laboratorio.
3. El script invoca al módulo nativo `rdt_master` en C++, el cual encapsula los registros en tramas de 500 bytes y los distribuye vía UDP hacia las direcciones IP correspondientes de los esclavos. Cada esclavo almacena de manera local su porción exclusiva del dataset.

### Fase 2: Sincronización y Cómputo Local Paralelo

1. El nodo maestro inicializa la arquitectura de la red neuronal, generando los coeficientes matemáticos (pesos y sesgos) iniciales. Estos parámetros globales se transmiten a toda la red mediante un mecanismo de difusión (Broadcast) RDT.
2. Cada nodo esclavo recibe la estructura de la red mediante `rdt_slave` y la carga en su entorno local de PyTorch (`esclavo.py`).
3. Todos los nodos procesan sus datos de forma aislada y simultánea realizando dos operaciones:
* **Forward Pass:** Evaluación de las 14 características clínicas del paciente para generar la predicción de diabetes y calcular la función de pérdida (error).
* **Backward Pass (Backpropagation):** Computación de las derivadas parciales del error respecto a los parámetros del modelo, generando una matriz local de **Gradientes**.



### Fase 3: Reducción y Agregación de Gradientes en C++

1. Los esclavos serializan sus matrices de gradientes locales y las envían de regreso al nodo maestro utilizando canales RDT confiables de 500 bytes.
2. El módulo `rdt_master.cpp` del maestro actúa como un receptor centralizado. Una vez validados los hashes y reordenadas las secuencias, el código C++ ejecuta el algoritmo de agregación matemática: realiza la suma posicional de todas las matrices recibidas y calcula el promedio aritmético exacto.
3. La matriz unificada de gradientes promedio es devuelta al espacio de nombres de Python en el maestro, donde se ejecuta la instrucción `optimizer.step()`, actualizando formalmente los pesos globales del modelo predictivo.
4. El nodo maestro evalúa el modelo final frente a un conjunto de datos de prueba no vistos previamente, imprimiendo las métricas cuantitativas de precisión en la terminal.

---

## 4. Instrucciones de Instalación y Despliegue

### 4.1. Requisitos del Entorno

Garantizar la instalación previa de los siguientes paquetes en todas las estaciones de trabajo involucradas:

```bash
pip install torch pandas scikit-learn matplotlib pybind11 setuptools

```

### 4.2. Compilación de los Módulos Puente (C++ a Python)

**En la Máquina Maestra:**
Navegar al directorio del maestro y ejecutar la compilación del módulo dinámico a través de `setuptools` y `pybind11`:

```bash
cd maestro
python setup.py build_ext --inplace

```

Este comando generará una biblioteca compartida ejecutable directamente por el intérprete de Python (archivo con extensión `.so` o `.pyd`).

**En las Máquinas Esclavas:**
Repetir el proceso de compilación local en cada uno de los terminales destinados como esclavos dentro de la red:

```bash
cd esclavo
python setup.py build_ext --inplace

```

### 4.3. Protocolo de Ejecución en Red

Para evitar condiciones de carrera o desbordamientos en los búferes de recepción, se debe seguir estrictamente el siguiente orden de arranque:

1. **Activación de los Nodos Esclavos:**
En cada una de las computadoras asignadas como esclavos, iniciar el script de escucha para quedar a la espera del dataset segmentado:
```bash
python esclavo.py

```


2. **Activación del Nodo Maestro:**
Una vez cerciorado que los sockets de los esclavos están abiertos y vinculados al puerto correspondiente, iniciar el proceso central en la máquina maestra:
```bash
python maestro.py

```



### 4.4. Resultados Esperados

Al culminar la transferencia y el procesamiento de la única época de entrenamiento, la consola del maestro imprimirá de forma automática los siguientes indicadores de rendimiento de la red neuronal:

* Métrica de precisión global (*Accuracy score*).
* Reporte detallado de clasificación (*Precision*, *Recall* y *F1-Score* por clase).
* Matriz de confusión impresa en terminal y despliegue gráfico del comportamiento de la pérdida.

```

```
