# Red Neuronal Distribuida sobre el Protocolo RDT-UDP

* **Departamento:** Ciencia de la Computación
* **Curso:** Redes y Comunicaciones
* **Docente:** Dr. Julio Santisteban Pablo
* **Integrantes:**
  * Jorge Chávez
  * José Cornejo
  * Mariela Mendoza
  * Diego Vásquez

---

## 1. Descripción del Proyecto

Este software implementa el entrenamiento síncrono de una red neuronal artificial para la clasificación de diabetes bajo la arquitectura de paralelismo de datos (Data Parallelism). El sistema está configurado de forma fija para un entorno de **1 Nodo Maestro y 3 Nodos Esclavos**. 

La transferencia de datos del dataset, coeficientes de pesos y matrices de gradientes se realiza sobre el protocolo de transporte UDP. Para dotar al canal de confiabilidad, control de orden, temporización y detección de errores, se programó un protocolo RDT personalizado de 500 bytes en C++, el cual se enlaza al entorno de ejecución de PyTorch en Python mediante módulos compilados con Pybind11.

---

## 2. Estructura de Directorios

```
proyecto_ia_distribuida/
┃
┣ 📂 dataset/
┃ ┗  Diabetes.csv               # Conjunto de datos (1000 registros / 14 características)
┃
┣ 📂 maestro/
┃ ┣  rdt_master.cpp             # Lógica RDT en C++ y promedio matemático de matrices
┃ ┣  rdt_master.hpp             # Cabeceras del nodo maestro
┃ ┣  setup.py                   # Script de compilación Pybind11 Maestro
┃ ┗  maestro.py                 # Orquestador del entrenamiento e interfaz de usuario PyTorch
┃
┗ 📂 esclavo/
  ┣  rdt_slave.cpp              # Lógica RDT en C++ de transmisión y recepción
  ┣  rdt_slave.hpp              # Cabeceras del nodo esclavo
  ┣  setup.py                   # Script de compilación Pybind11 Esclavo
  ┗  esclavo.py                 # IA Esclavo (Cómputo local de Forward y Backward Pass)
```
Las especificaciones detalladas del formato del datagrama de 500 bytes, campos ACK/NACK, la lógica del Checksum al final de la trama y la implementación del algoritmo de Karn para el control de pérdidas por Timeout se encuentran documentadas en el archivo independiente Protocolo.txt.

3. Instalación y Compilación
3.1. Instalación de Dependencias
Ejecutar el gestor de paquetes en las 4 estaciones de trabajo destinadas a la prueba:

```Bash
pip install torch pandas scikit-learn matplotlib pybind11 setuptools
```
3.2. Compilación del Módulo Puente C++ (Pybind11)
En la Computadora Maestra:

```Bash
cd maestro
python setup.py build_ext --inplace
```
En las 3 Computadoras Esclavas:

```Bash
cd esclavo
python setup.py build_ext --inplace
```
4. Protocolo de Ejecución en Red
Para garantizar la correcta sincronización de los sockets y evitar pérdidas por falta de escucha, el orden de arranque debe ser el siguiente:

Paso 1: Inicialización de los 3 Esclavos
Ejecutar el script principal en las 3 computadoras esclavas para levantar los sockets de escucha e inicializar los entornos locales de cómputo:

```Bash
python esclavo.py
```
Paso 2: Inicialización del Maestro
Una vez constatado el estado de escucha de los 3 esclavos en el laboratorio, iniciar el proceso central en la máquina maestra:

```Bash
python maestro.py
```
5. Resultados del Entrenamiento
Al concluir la única época de entrenamiento distribuido y completarse la agregación de gradientes en el puente C++, la consola del Maestro desplegará:

Métrica de exactitud global del modelo unificado (Accuracy score).

Reporte técnico estructurado de rendimiento (Precision, Recall y F1-Score por clase).

Interfaz visual con la curva descendente de la función de pérdida (Loss Graph) y la distribución de aciertos mediante la Matriz de Confusión
