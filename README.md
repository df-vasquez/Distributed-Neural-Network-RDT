# Distributed Neural Network RDT - UDP (C++ y Pybind11)

**Universidad Católica San Pablo**
**Curso:** Redes y Comunicaciones
**Docente:** Dr. Julio Santisteban Pablo

**Integrantes:**

* Jorge Chávez
* José Cornejo
* Marela Mendoza
* Diego Vásquez

---

## 1. Descripción del Proyecto

Este proyecto implementa un sistema coordinado de entrenamiento distribuido para una red neuronal artificial (*Data Parallelism*), utilizando sockets UDP como capa de transporte.

Para garantizar la fiabilidad sobre un canal inherentemente no confiable, se ha desarrollado una capa de transporte personalizada denominada **RDT-UDP**, implementada en C++ e inspirada en el protocolo clásico **RDT 3.0 (Stop-and-Wait)** descrito por *Kurose & Ross*.

El sistema permite:

* Fragmentación segura de datasets reales
* Distribución confiable a nodos de cómputo
* Recolección de métricas de entrenamiento distribuidas
* Consolidación de resultados estadísticos globales

Todo ello sin pérdida de datos ni desalineación de paquetes en la red.

---

## 2. Arquitectura del Clúster

El sistema opera bajo una arquitectura síncrona descentralizada compuesta por:

### Nodo Maestro (`maestro.py`)

* Actúa como orquestador del sistema.
* Segmenta el dataset `Diabetes.csv` de manera equitativa.
* Transmite fragmentos mediante el motor RDT-UDP en C++.
* Recopila métricas de los nodos esclavos.
* Genera visualizaciones globales.
* No realiza cómputo local para evitar saturación de red.

### Nodos Esclavos (`esclavo.py`)

* Ejecutan el entrenamiento local del modelo.
* Escuchan en puertos independientes.
* Reciben datos de forma confiable.
* Entrenan una red neuronal en PyTorch.
* Envían métricas de rendimiento al nodo maestro.

---

### Topología del Sistema

```text
                  ┌─────────────────────┐
                  │    Nodo Maestro     │
                  │     (Puerto 8000)   │
                  └──────────┬──────────┘
                             │
        ┌────────────────────┼────────────────────┐
        │                    │                    │
        ▼                    ▼                    ▼
┌─────────────┐       ┌─────────────┐      ┌─────────────┐
│  Esclavo 1  │       │  Esclavo 2  │      │  Esclavo 3  │
│(Puerto 8001)│       │(Puerto 8002)│      │(Puerto 8003)│
└─────────────┘       └─────────────┘      └─────────────┘
```

---

## 3. Modelo Matemático y Dataset

El sistema utiliza el dataset `Diabetes.csv`, compuesto por aproximadamente 1000 muestras.

### Variables de entrada

* **input_dim = 11**
* Corresponde a las primeras 11 columnas del dataset (MF a B0I)

### Variables de salida

* **num_classes = 3**
* Codificación *One-Hot*: (P, N, Y)

---

### Arquitectura de la Red Neuronal

* Capa oculta 1: `Linear(128)` + ReLU
* Capa oculta 2: `Linear(64)` + ReLU
* Salida:

  * Logits de clasificación
  * Varianza logarítmica (incertidumbre del modelo)

---

## 4. Estructura del Repositorio

```text
Distributed-Neural-Network-RDT/
├── dataset/
│   └── Diabetes.csv
├── maestro/
│   ├── maestro.py
│   ├── rdt_master.cpp
│   ├── rdt_master.hpp
│   ├── bindings.cpp
│   └── setup.py
├── esclavo/
│   ├── esclavo.py
│   ├── rdt_slave.cpp
│   ├── rdt_slave.hpp
│   ├── bindings.cpp
│   └── setup.py
└── Protocolo.txt
```

---

## 5. Instrucciones de Compilación e Instalación

### Requisitos

* Compilador C++ (GCC o Clang)
* Python 3.x
* `pybind11`
* `setuptools`

---

### Paso 1: Compilar el motor del Maestro

```bash
cd maestro
python3 setup.py build_ext --inplace
```

---

### Paso 2: Compilar el motor de los Esclavos

```bash
cd ../esclavo
python3 setup.py build_ext --inplace
```

> Esto generará librerías compartidas (`.so` o `.pyd`) para la integración directa con Python.

---

## 6. Guía de Ejecución

Para ejecutar el sistema distribuido localmente, abrir **cuatro terminales**:

---

### Terminal 1 — Esclavo 1

```bash
cd esclavo
python3 esclavo.py 1
```

### Terminal 2 — Esclavo 2

```bash
cd esclavo
python3 esclavo.py 2
```

### Terminal 3 — Esclavo 3

```bash
cd esclavo
python3 esclavo.py 3
```

### Terminal n — Esclavo n

```bash
cd esclavo
python3 esclavo.py n
```

### Terminal — Maestro (último en ejecutarse)

```bash
cd maestro
python3 maestro.py n
```

---

## 7. Executores

### Requisitos

* gnome-terminal

### Paso 1: Dar permisos a los .sh

```bash
chmod +x one_terminal.sh
```

```bash
chmod +x multiple_terminal.sh
```

### Paso 2: Ejecutar

Para ejecutar en una consola:

```bash
./one_terminal n
```

Para abrir multiples consolas:
```bash
./multiple_terminal n
```


## 8. Resultados y Visualizaciones

Al finalizar la ejecución del protocolo confiable, el nodo maestro genera:

### Métricas de Evaluación

* Classification Report (Scikit-Learn)
* Precision, Recall y F1-score por clase
* Evaluación para las 3 clases del sistema

---

### Visualizaciones

* Curvas de **Loss vs Accuracy** por batch (50 muestras)
* Matriz de confusión global del clúster
* Gráficas generadas automáticamente con Matplotlib

---


