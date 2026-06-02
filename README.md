# Sistema de Entrenamiento Distribuido de Redes Neuronales sobre el Protocolo RDT-UDP
# Integrantes:

1. Gabriel Cornejo
2. Jorge Chavez
3. Marela Mendoza
4. Diego Vasquez
   
Este proyecto implementa una arquitectura distribuida de aprendizaje federado mediante el método de Paralelismo de Datos (Data Parallelism) configurada de forma fija para un entorno de **1 Nodo Maestro y 3 Nodos Esclavos**. El sistema permite el entrenamiento de una red neuronal artificial para la clasificación de diabetes utilizando un canal de transporte no confiable (UDP). Para garantizar la integridad, el orden y el control de pérdidas de los datos transferidos, se utiliza un protocolo personalizado de Transferencia Confiable de Datos (RDT) en C++, integrado nativamente en Python a través de Pybind11.

---

## 1. Estructura de Directorios del Proyecto

La organización del repositorio aísla los recursos y garantiza la modularidad del software:

📂 proyecto_ia_distribuida/
┃
┣ 📂 dataset/
┃ ┗ 📄 Diabetes.csv               
┃
┣ 📂 maestro/
┃ ┣ 📜 rdt_master.cpp             # Lógica RDT en C++ para segmentación, recepción y promedio de matrices
┃ ┣ 📜 rdt_master.hpp             # Definiciones de cabeceras del nodo maestro
┃ ┣ 📜 setup.py                   # Script de compilación Pybind11 para el módulo maestro
┃ ┗ 🐍 maestro.py                 # IA, división de datos y actualización de pesos
┃
┗ 📂 esclavo/
  ┣ 📜 rdt_slave.cpp              # Lógica RDT en C++ para recepción de datos y envío de gradientes
  ┣ 📜 rdt_slave.hpp              # Definiciones de cabeceras del nodo esclavo
  ┣ 📜 setup.py                   # Script de compilación Pybind11 para el módulo esclavo
  ┗ 🐍 esclavo.py                 # IA (Forward y Backward Pass)

---

## 2. Especificación Estricta del Protocolo RDT (500 Bytes)

El protocolo opera en la capa de aplicación y delimita el tamaño máximo de cada datagrama a un límite estricto de **500 bytes**. La elección de 500 bytes en lugar de la clásica potencia de dos (512 bytes) responde a un criterio de diseño controlado, donde el software gestiona directamente la fragmentación exacta del búfer independientemente de las alineaciones por defecto de la memoria física.

### 2.1. Anatomía del Datagrama (Encapsulación)

Cada paquete transmitido se divide de manera fija en dos secciones:

$$\text{Datagrama Total (500 bytes)} = \text{Cabecera (7 bytes)} + \text{Carga Útil (493 bytes)}$$

* **Cabecera (Bytes 0 al 6):**
    * `Byte 0 [1 Byte] - Checksum (Hash):` Valor numérico obtenido mediante la suma matemática de los caracteres del payload bajo la operación aritmética `Suma % 7`. Su propósito es la detección de errores por inversión de bits en el canal. Si el hash calculado por el receptor no coincide con este byte, el paquete se descarta de inmediato.
    * `Bytes 1-2 [2 Bytes] - Banderas de Fragmentación (Flags):` Controlan el reensamblado de las estructuras de datos extensas en el destino.
        * `01`: Datagrama inicial de una ráfaga.
        * `00`: Datagrama intermedio.
        * `11`: Datagrama final de la secuencia.
    * `Bytes 3-6 [4 Bytes] - Número de Secuencia (SEQ):` Valor numérico incremental codificado en texto con relleno de ceros a la izquierda (ej. `0000`, `0001`). Permite al receptor ordenar los fragmentos en el búfer de manera correcta, mitigando el problema del desorden de paquetes en UDP.
* **Carga Útil (Bytes 7 al 499):**
    * `Payload [493 Bytes]:` Segmento de datos puros serializados en texto. Los espacios remanentes de la carga útil que no alcancen los 493 bytes se rellenan de forma uniforme con el carácter `#` para forzar el tamaño estricto de 500 bytes en el socket.

### 2.2. Control de Pérdidas por Temporización (Algoritmo de Karn)

Para gestionar la pérdida de paquetes en el laboratorio sin saturar el canal ni generar estimaciones falsas de retraso, el nodo maestro implementa un mecanismo de Temporizador de Retransmisión (RTO) basado en el Algoritmo de Karn:
1.  Se mide el tiempo de ida y vuelta (RTT) únicamente de aquellos datagramas que son confirmados con éxito en su primera transmisión.
2.  Si un temporizador expira y ocurre una retransmisión, se aplica un retroceso exponencial (Exponential Backoff), duplicando el valor del temporizador para el siguiente intento.
3.  Se prohíbe calcular el RTT a partir de paquetes retransmitidos, eliminando la ambigüedad de no saber si la confirmación pertenece al paquete original o al retransmitido.

---

## 3. Flujo Operacional de la Red Neuronal Distribuida (1 Sola Época)

El entrenamiento distribuido se ejecuta en un único ciclo síncrono estructurado en tres fases:

### Fase 1: Segmentación del Dataset (Inicio del Programa)
1.  El script `maestro.py` lee el archivo `Diabetes.csv` con las 1000 muestras.
2.  El dataset se divide en 4 porciones proporcionales: el Maestro se reserva 250 muestras para su cómputo local, y asigna 250 muestras independientes a cada uno de los 3 esclavos.
3.  El script invoca al módulo nativo `rdt_master` en C++, el cual fragmenta cada bloque en cadenas de texto de 493 bytes, les añade la cabecera de control y las envía vía UDP a las direcciones IP de los 3 esclavos. Esta transferencia ocurre una sola vez.

### Fase 2: Sincronización de Pesos y Cómputo Local
1.  El `maestro.py` instancia la red neuronal, generando los coeficientes matemáticos iniciales (pesos y sesgos) de forma aleatoria. Estos parámetros globales se transmiten inmediatamente a los 3 esclavos mediante una ráfaga RDT.
2.  Los scripts `esclavo.py` reciben los parámetros a través de sus módulos `rdt_slave.cpp` y clonan la red neuronal de forma exacta.
3.  Cada nodo (Maestro y los 3 Esclavos) procesa sus 250 muestras asignadas en paralelo:
    * **Forward Pass:** Se evalúan las características clínicas de entrada para generar las predicciones de la IA y calcular el error (Loss).
    * **Backward Pass:** Mediante el algoritmo de retropropagación, cada nodo calcula las derivadas del error respecto a los pesos, generando una matriz local de **Gradientes** (la dirección en la que debe aprender la red).

### Fase 3: Reducción y Agregación Matemática en C++
1.  Cada uno de los 3 esclavos toma su matriz de gradientes local, la serializa en texto y la transmite de vuelta al maestro en tramas RDT de 500 bytes.
2.  El módulo `rdt_master.cpp` del maestro recibe los paquetes, valida los hashes, reordena los fragmentos y reconstruye las 3 matrices de los esclavos, sumando además la matriz generada por el propio maestro.
3.  **El Promedio en C++:** El código C++ realiza la agregación matemática en memoria de alto rendimiento: suma los valores posición por posición de las 4 matrices y los divide entre 4 para obtener la **Matriz de Gradiente Promedio Global**.
4.  Esta matriz unificada se entrega al espacio de nombres de Python en el maestro, el cual ejecuta la instrucción `optimizer.step()`, actualizando formalmente los pesos de la red neuronal con el aprendizaje combinado de todo el sistema.
5.  El maestro evalúa el modelo final frente a un conjunto de datos de prueba retenidos y despliega las métricas cuantitativas en la pantalla.

---

## 4. Instrucciones de Instalación y Despliegue

### 4.1. Requisitos del Entorno
Instalar las dependencias de cómputo científico e interfaces en todas las estaciones de trabajo:

```bash
pip install torch pandas scikit-learn matplotlib pybind11 setuptools

4.2. Compilación de los Módulos Puente (C++ a Python)
En la Máquina Maestra:
Compilar la lógica de red y agregación para generar la librería dinámica nativa de Python:

Bash
cd maestro
python setup.py build_ext --inplace
En las 3 Máquinas Esclavas:
Ejecutar la compilación local en cada uno de los terminales destinados como esclavos:

Bash
cd esclavo
python setup.py build_ext --inplace
4.3. Protocolo de Ejecución en Red
Para evitar la pérdida de datagramas iniciales por falta de escucha, el orden de arranque debe ser el siguiente:

Paso 1 (Esclavos): En las 3 computadoras esclavas, ejecutar el script para inicializar los sockets de recepción:

Bash
python esclavo.py
Paso 2 (Maestro): En la computadora maestra, iniciar el proceso central para comenzar la distribución de datos y el entrenamiento:

Bash
python maestro.py
4.4. Resultados del Entrenamiento
Al finalizar la única época de entrenamiento, la terminal del maestro desplegará:

Métrica de exactitud global del modelo (Accuracy score).

Reporte detallado de rendimiento (Precision, Recall y F1-Score).

La interfaz gráfica de la Matriz de Confusión y la curva de la función de pérdida.
