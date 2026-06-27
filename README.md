## Distributed Neural Network RDT - UDP (C++ y Pybind11)

**Universidad Católica San Pablo** **Curso:** Redes y Comunicaciones  
**Docente:** Dr. Julio Santisteban Pablo  

**Integrantes:** 
* Jorge Chávez  
* José Cornejo  
* Marela Mendoza  
* Diego Vásquez  

---

## 1. Descripción del Proyecto

Este proyecto implementa un sistema coordinado de entrenamiento distribuido para una red neuronal artificial (*Data Parallelism*), utilizando sockets UDP como capa de transporte subyacente.

Para garantizar una fiabilidad absoluta sobre un canal inherentemente no confiable, se ha desarrollado una capa de transporte personalizada denominada **RDT-UDP**. Esta arquitectura está implementada a nivel nativo en C++ e inspirada en el protocolo **Go-Back-N (GBN) con Ventana Deslizante (Sliding Window de tamaño N=8)**, incorporando control de flujo, gestión de timeouts por software y validación de integridad mediante sumas de verificación (*Internet Checksum RFC 1071*).

El sistema permite:
* Fragmentación masiva y distribución simultánea de datasets hacia múltiples nodos de cómputo en paralelo.
* Transmisión tolerante a pérdidas mediante ráfagas acotadas por ventana y temporizadores asíncronos administrados por hilos independientes.
* Entrenamiento local concurrente de modelos predictivos directamente sobre frameworks de aceleración científica (PyTorch).
* Recolección y unificación asíncrona de streams métricos JSON mediante multiplexación por software basada en el puerto de origen.

---

## 2. Arquitectura del Clúster y Flujo Concurrente

El sistema opera bajo una arquitectura distribuida síncrona coordinada por roles estrictos y ejecución paralela real:

### Nodo Maestro (`maestro.py`)
* **Orquestador del Clúster:** Carga en memoria y segmenta el dataset de manera equitativa entre los nodos registrados en la red.
* **Distribución Simultánea (Fase 1):** Dispara ráfagas de datos de forma secuencial-inmediata hacia todos los esclavos. Al delegar la transmisión al pipeline nativo C++, todos los esclavos "despiertan" y comienzan su entrenamiento al mismo tiempo, evitando bloqueos secuenciales.
* **Recolección Asíncrona (Fase 2):** Monitorea el socket mediante un hilo C++ en segundo plano no bloqueante (`select`), demultiplexando las métricas JSON entrantes hacia colas de memoria independientes por cada nodo.

### Nodos Esclavos (`esclavo.py`)
* **Unidades de Cómputo Concurrente:** Escuchan de forma independiente en puertos dedicados (`8001 + id`) y deserializan los datos validados por el motor nativo RDT-UDP.
* **Entrenamiento y Retropropagación:** Ejecutan el ciclo de optimización matemática estimando pérdidas y magnitudes de gradientes en tiempo real a lo largo de un ciclo extendido de **360 épocas**.
* **Retorno Métrico:** Generan un reporte unificado en formato string JSON y lo transmiten de vuelta al maestro empleando el pipeline de fiabilidad GBN.

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

El sistema entrena una arquitectura multi-clase utilizando el dataset `Diabetes.csv` (1000 muestras totales distribuidas equitativamente según la cantidad de esclavos).

* **Variables de Entrada (`input_dim = 11`):** Mapeo estricto de las primeras 11 columnas características del dataset científico (desde MF hasta B0I).
* **Variables de Salida (`num_classes = 3`):** Codificación One-Hot de categorías de diagnóstico clínico (P, N, Y).
* **Hiperparámetros fijos:** Lotes (*Batch*) de 50 muestras, tasa de aprendizaje de 0.001 empleando el optimizador Adam.

### Arquitectura de la Red Neuronal
1. **Capa Oculta 1:** `Linear(11 -> 128)` + Activación No Lineal `ReLU`
2. **Capa Oculta 2:** `Linear(128 -> 64)` + Activación No Lineal `ReLU`
3. **Capa de Salida:** Proyección paralela de Logits de clasificación y varianza logarítmica para estimar la incertidumbre intrínseca del clúster distribuido.

---

## 4. Estructura Completa del Repositorio

El diseño de la solución modular comparte definiciones de paquetes estructuradas de la siguiente manera:


```

Distributed-Neural-Network-RDT/
├── dataset/
│   └── Diabetes.csv            # Dataset
├── maestro/
│   ├── maestro.py              # Orquestador del entrenamiento distribuido
│   ├── rdt_master.cpp          
│   ├── rdt_master.hpp          
│   ├── rdt_common.hpp          # Estructuras comunes (RdtPacket, Checksum)
│   ├── bindings.cpp            # Exportación Pybind11 para módulo maestro
│   └── setup.py                # Script de compilación del módulo nativo
├── esclavo/
│   ├── esclavo.py              # Script de cómputo local y retropropagación
│   ├── rdt_slave.cpp           # Capa de transporte del esclavo (Receptor secuencial)
│   ├── rdt_slave.hpp           
│   ├── rdt_common.hpp          
│   ├── bindings.cpp            
│   └── setup.py                
├── Protocolo.txt               # Especificación teórica del diseño de red
├── one_terminal.sh             # Automatización en una sola consola (Multiplataforma)
└── multiple_terminal.sh        # Automatización en consolas independientes (Linux/Mac)

```

---

## 5. Matriz de Compatibilidad y Requisitos de Sistema

> [IMPORTANT]
> **Restricción de Arquitectura:** El núcleo del motor de transporte en C++ interactúa directamente con la API de sockets POSIX de UNIX (`<sys/socket.h>`, `<sys/select.h>`, `<arpa/inet.h>`). Por ende, **no compila de forma nativa en el CMD o PowerShell de Windows**.

### Guía de Entornos para el Equipo:
* **macOS y Linux Nativo:** Compila directamente usando la cadena de herramientas estándar (`GCC` / `Clang`).
* **Windows:** **Debe ejecutarse bajo WSL (Windows Subsystem for Linux)**. 
  * *Instrucciones rápidas para Windows:* Abrir PowerShell como Administrador, ejecutar `wsl --install`, reiniciar la PC e instalar la distribución de Ubuntu desde la Microsoft Store. Dentro de este entorno Linux integrado, el código compilará de forma perfecta.

---

## 6. Instrucciones de Compilación e Instalación

### Requisitos Previos (Instalar en el entorno Linux/Mac/WSL)
* Compilador compatible con C++11 o superior (`g++` / `clang`)
* Python 3.x y gestor de paquetes pip
* Dependencias científicas: `pybind11`, `setuptools`, `pandas`, `torch`, `numpy`, `matplotlib`, `scikit-learn`

### Paso 1: Compilar el Core Nativo del Maestro
```bash
cd maestro
python3 setup.py build_ext --inplace

```

### Paso 2: Compilar el Core Nativo de los Esclavos

```bash
cd ../esclavo
python3 setup.py build_ext --inplace

```

*Este paso inyecta los bindings generando librerías dinámicas binarias (`.so` en Linux/WSL/Mac) para la interoperabilidad directa con los scripts de Python.*

---

## 7. Ejecución del Clúster Distribuido

El sistema puede ejecutarse de dos formas: manualmente, iniciando cada proceso desde una terminal independiente, o mediante scripts de automatización.

### Método A: Ejecución Manual (Recomendado)

Este método es compatible con **Linux, macOS y WSL** y facilita la observación del comportamiento de cada proceso de forma independiente.

1. **Iniciar los nodos esclavos**

Para cada esclavo, abrir una nueva terminal y ejecutar:

```bash
cd esclavo
source ../venv/bin/activate
python3 esclavo.py <ID_ESCLAVO>
````

Por ejemplo:

```bash
python3 esclavo.py 1
python3 esclavo.py 2
python3 esclavo.py 3
```

2. **Iniciar el Nodo Maestro**

Una vez que todos los esclavos se encuentren en estado de espera, abrir otra terminal y ejecutar:

```bash
cd maestro
source ../venv/bin/activate
python3 maestro.py <NUM_ESCLAVOS>
```

Ejemplo para tres esclavos:

```bash
python3 maestro.py 3
```

---

### Método B: Ejecución mediante Scripts

Antes de utilizar los scripts, otorgar permisos de ejecución:

```bash
chmod +x one_terminal.sh
chmod +x multiple_terminal.sh
```

#### `one_terminal.sh`

Inicia automáticamente los esclavos en segundo plano y posteriormente ejecuta el maestro en la terminal actual.

```bash
./one_terminal.sh 3
```

#### `multiple_terminal.sh`

> [!WARNING]
> Este script utiliza `gnome-terminal`, por lo que solo es compatible con **Linux** o **WSL con soporte gráfico**.

Abre una terminal independiente para cada esclavo, espera unos segundos para permitir su inicialización y finalmente ejecuta el maestro.

```bash
./multiple_terminal.sh 3
```

---

## 8. Monitorización y Resultados

Durante la ejecución, el sistema muestra información de depuración tanto de la capa de transporte como del entrenamiento distribuido.

### Capa de Transporte (C++)

Se registran eventos como:

* Envío y recepción de paquetes.
* ACKs acumulativos.
* Retransmisiones por timeout.
* Desplazamiento de la ventana Go-Back-N.
* Detección de paquetes descartados o duplicados.

### Entrenamiento Distribuido (Python)

Cada esclavo muestra en tiempo real:

* Época actual.
* Loss.
* Accuracy.
* Magnitud del gradiente (*Gradient Norm*).

Al finalizar el entrenamiento, el Nodo Maestro consolida las métricas recibidas desde todos los esclavos y genera:

* Curva global de convergencia del **Loss**.
* Curva global de **Accuracy**.
* Matriz de Confusión Global.



