# Entrenamiento Distribuido de una Red Neuronal sobre un Protocolo RDT Implementado en UDP

**Universidad Católica San Pablo**
**Departamento de Ciencia de la Computación**
**Curso:** Redes y Comunicaciones
**Docente:** Dr. Julio Santisteban Pablo

## Integrantes

* Jorge Chávez
* José Cornejo
* Marela Mendoza
* Diego Vásquez

---

# 1. Descripción General

Este proyecto implementa un sistema de entrenamiento distribuido para una red neuronal artificial destinada a la clasificación de diabetes utilizando la estrategia de **Data Parallelism**.

La arquitectura está compuesta por un **Nodo Maestro** y **tres Nodos Esclavos**, los cuales cooperan para ejecutar una única época de entrenamiento de manera síncrona.

La comunicación entre nodos se realiza mediante el protocolo **UDP**. Debido a que UDP no garantiza entrega confiable, ordenamiento ni control de errores, se desarrolló una capa de transporte confiable inspirada en los protocolos **Reliable Data Transfer (RDT)** estudiados en Kurose y Ross.

Esta capa incorpora:

* Numeración de secuencias.
* ACK y NACK.
* Checksum.
* Retransmisión por timeout.
* Algoritmo de Karn.
* Exponential Backoff.

---

# 2. Arquitectura del Sistema

```text
                  ┌─────────────────────┐
                  │    Nodo Maestro     │
                  │     maestro.py      │
                  └──────────┬──────────┘
                             │
       ┌─────────────────────┼─────────────────────┐
       │                     │                     │
       ▼                     ▼                     ▼

 ┌─────────────┐      ┌─────────────┐      ┌─────────────┐
 │ Esclavo 1   │      │ Esclavo 2   │      │ Esclavo 3   │
 │ esclavo.py  │      │ esclavo.py  │      │ esclavo.py  │
 └─────────────┘      └─────────────┘      └─────────────┘

              Comunicación UDP + RDT
```

La arquitectura sigue el modelo **Parameter Server**, donde el nodo maestro coordina la ejecución, distribuye los datos, recibe los gradientes calculados por los esclavos y realiza la actualización global del modelo.

---

# 3. Integración Python–C++

El sistema se divide en dos capas complementarias:

## Capa de Inteligencia Artificial (Python)

Implementada con PyTorch.

Responsabilidades:

* Lectura del dataset.
* Construcción de la red neuronal.
* Forward Pass.
* Backward Pass.
* Optimización del modelo.
* Métricas y visualización.

## Capa de Comunicación (C++)

Implementada mediante sockets UDP.

Responsabilidades:

* Fragmentación de datos.
* Encapsulación RDT.
* Checksum.
* ACK/NACK.
* Timeout.
* Retransmisiones.
* Reensamblado.
* Agregación de gradientes.

## Puente Python ↔ C++

La comunicación entre ambas capas se realiza mediante **Pybind11**.

Los módulos:

* `rdt_master.cpp`
* `rdt_slave.cpp`

son compilados como extensiones nativas e importados directamente desde Python.

De esta manera, PyTorch puede utilizar las funciones de comunicación implementadas en C++ sin abandonar el entorno Python.

---

# 4. Estructura del Proyecto

```text
proyecto_ia_distribuida/
│
├── dataset/
│   └── Diabetes.csv
│
├── maestro/
│   ├── rdt_master.cpp
│   ├── rdt_master.hpp
│   ├── setup.py
│   └── maestro.py
│
├── esclavo/
│   ├── rdt_slave.cpp
│   ├── rdt_slave.hpp
│   ├── setup.py
│   └── esclavo.py
│
└── Protocolo.txt
```

---

# 5. Flujo de Ejecución

## Fase 1: Distribución del Dataset

El maestro divide el dataset en cuatro particiones homogéneas.

* Una partición permanece en el maestro.
* Tres particiones son enviadas a los esclavos.

## Fase 2: Sincronización de Pesos

El maestro inicializa la red neuronal y transmite los pesos iniciales a todos los esclavos.

## Fase 3: Cómputo Distribuido

Cada esclavo ejecuta:

* Forward Pass
* Backward Pass
* Cálculo de gradientes locales

## Fase 4: Reducción de Gradientes

Los gradientes son enviados al maestro mediante RDT-UDP.

El módulo `rdt_master.cpp`:

* valida checksums,
* reordena secuencias,
* recupera pérdidas,
* promedia gradientes.

## Fase 5: Actualización Global

El gradiente promedio es entregado a PyTorch para ejecutar:

```python
optimizer.step()
```

completando la única época de entrenamiento.

---

# 6. Instalación

```bash
pip install torch pandas scikit-learn matplotlib pybind11 setuptools
```

---

# 7. Compilación

## Maestro

```bash
cd maestro
python setup.py build_ext --inplace
```

## Esclavos

```bash
cd esclavo
python setup.py build_ext --inplace
```

---

# 8. Ejecución

## Paso 1

Iniciar los tres esclavos:

```bash
python esclavo.py
```

## Paso 2

Iniciar el maestro:

```bash
python maestro.py
```

---

# 9. Resultados

Al finalizar el entrenamiento se muestran:

* Accuracy.
* Precision.
* Recall.
* F1-Score.
* Curva de pérdida.
* Matriz de confusión.

---

# 10. Relación con Kurose y Ross

La implementación toma como referencia los mecanismos de confiabilidad estudiados en el capítulo de la capa de transporte de Kurose y Ross.

Particularmente se emplean conceptos equivalentes a:

* Detección de errores mediante checksum.
* Confirmaciones ACK/NACK.
* Retransmisión por temporizador.
* Estimación adaptativa de RTT.
* Algoritmo de Karn.
* Exponential Backoff.

Con ello se construye una capa de transporte confiable sobre UDP para soportar el intercambio de datasets, pesos y gradientes durante el entrenamiento distribuido.
