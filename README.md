# Distributed Neural Network RDT (UDP | C++ | Pybind11)

**Universidad Católica San Pablo**  
**Curso:** Redes y Comunicaciones  
**Docente:** Dr. Julio Santisteban Pablo  

### Integrantes:
- Jorge Chávez
- José Cornejo
- Marela Mendoza
- Diego Vásquez

---

## 1. Descripción del Proyecto

Este proyecto implementa el entrenamiento distribuido de una red neuronal artificial mediante una arquitectura tipo *Parameter Server*.

El sistema utiliza UDP como protocolo base de comunicación. Sobre este se construye una capa de transporte confiable denominada **RDT-UDP (Reliable Data Transfer over UDP)**, desarrollada en C++.

El objetivo del sistema es implementar un protocolo de transporte funcional que permita:

- Entrega confiable de datos sobre UDP
- Control de integridad mediante checksum
- Control de orden mediante números de secuencia
- Confirmación de recepción (ACK/NACK)
- Retransmisión ante pérdidas o errores

---

## 2. Arquitectura del Sistema

El sistema está compuesto por:

- **1 Nodo Maestro:** coordina el entrenamiento, distribuye datos y agrega gradientes.
- **3 Nodos Esclavos:** realizan entrenamiento local en paralelo.
  
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
 │  Esclavo 1  │      │  Esclavo 2  │      │  Esclavo 3  │
 │ esclavo.py  │      │ esclavo.py  │      │ esclavo.py  │
 └─────────────┘      └─────────────┘      └─────────────┘
```
---

## 3. Capas del Sistema

### Capa de Aplicación (Python)

Implementada con PyTorch. Responsable de:

* Entrenamiento del modelo neuronal
* Forward y Backward Pass
* Cálculo de métricas
* Visualización de resultados

---

### Capa de Comunicación (C++)

Implementada con sockets UDP en C++.

Responsable de:

* Implementación del protocolo RDT-UDP
* Fragmentación y reensamblaje de mensajes
* Control de errores e integridad
* Manejo de retransmisiones
* Agregación de gradientes

---

### Interfaz Python–C++ (Pybind11)

Permite conectar PyTorch con el módulo en C++ de forma directa, evitando sobrecarga de comunicación adicional.

---

## 4. Estructura del Proyecto

```text
Distributed-Neural-Network-RDT/
├── dataset/
│   └── Diabetes.csv
│
├── maestro/
│   ├── maestro.py
│   ├── rdt_master.cpp
│   ├── rdt_master.hpp
│   ├── bindings.cpp
│   └── setup.py
│
├── esclavo/
│   ├── esclavo.py
│   ├── rdt_slave.cpp
│   ├── rdt_slave.hpp
│   ├── bindings.cpp
│   └── setup.py
│
└── Protocolo.txt
```

---

## 5. Formato del Datagrama (500 bytes)

Cada paquete tiene tamaño fijo obligatorio de **500 bytes exactos**.

```
+-------------------------------------------------------+
| Bytes 0-1    → FLAGS (2 bytes)                        |
| Bytes 2-5    → SEQ (4 bytes)                         |
| Bytes 6-9    → ACK/NACK (4 bytes)                    |
| Bytes 10-498 → PAYLOAD (489 bytes)                   |
| Byte 499     → CHECKSUM (1 byte)                     |
+-------------------------------------------------------+
```

---

## 6. Tipos de Datos en el PAYLOAD

El campo PAYLOAD transporta tres tipos de información:

* **DATASET:** particiones del dataset de entrenamiento
* **WEIGHTS:** parámetros del modelo neuronal
* **GRADIENT:** gradientes calculados en los nodos esclavos

Todos los datos se serializan en texto plano y se rellenan con `#` si es necesario.

---

## 7. Mecanismos del Protocolo

El sistema implementa los siguientes mecanismos:

* **Checksum:** detección de corrupción de datos
* **SEQ:** control de orden y reensamblaje
* **ACK:** confirmación de recepción correcta
* **NACK:** solicitud de retransmisión inmediata
* **Timeout con select():** detección de pérdida de paquetes
* **Algoritmo de Karn:** estimación correcta del RTT
* **Backoff exponencial:** control de congestión en red

---

## 8. Flujo del Sistema

1. El maestro divide el dataset en 4 partes.
2. Envía 3 partes a los esclavos.
3. Inicializa y distribuye los pesos del modelo.
4. Cada esclavo ejecuta entrenamiento local.
5. Los gradientes son enviados al maestro.
6. El maestro valida y promedia gradientes.
7. Se actualiza el modelo global.

---

## 9. Compilación

### Maestro

```bash
cd maestro
python setup.py build_ext --inplace
```

### Esclavos

```bash
cd esclavo
python setup.py build_ext --inplace
```

---

## 10. Ejecución

### Iniciar esclavos primero

```bash
python esclavo.py
```

### Luego iniciar maestro

```bash
python maestro.py
```

---

## 11. Resultados

El sistema muestra:

* Accuracy
* Precision / Recall / F1-score
* Curva de pérdida
* Matriz de confusión

---

## 12. Referencias

Kurose, J. F., & Ross, K. W. (2021). *Computer Networking: A Top-Down Approach* (8th ed.). Pearson.

---

