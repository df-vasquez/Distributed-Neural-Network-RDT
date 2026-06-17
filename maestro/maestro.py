import sys
import os
import pandas as pd
import io
import json
import numpy as np
import matplotlib.pyplot as plt
from sklearn.metrics import confusion_matrix, ConfusionMatrixDisplay
import modulo_maestro_nativo

def main():
    if len(sys.argv) < 2:
        print("Uso: python3 maestro.py <cantidad_esclavos>")
        return

    print("=== INICIANDO NODO MAESTRO DISTRIBUIDO (RDT 3.0) ===")
    
    csv_path = "../dataset/Diabetes.csv"
    if not os.path.exists(csv_path):
        print(f"Error: No se encontró el dataset en {csv_path}")
        return

    print(f"Cargando dataset desde: {csv_path}")
    df = pd.read_csv(csv_path)
    total_filas = len(df)
    print(f"Dataset cargado correctamente. Total filas: {total_filas}")

    # 1. DIVIDIR EL DATASET EN 3 PARTES EQUITATIVAS
    num_esclavos = int(sys.argv[1])
    filas_por_esclavo = total_filas // num_esclavos # 1000 // 3 = 333 filas aprox.

    partes_csv = []
    for i in range(num_esclavos):
        inicio = i * filas_por_esclavo
        # El último esclavo se lleva el residuo si la división no es exacta
        fin = total_filas if i == (num_esclavos - 1) else (i + 1) * filas_por_esclavo
        
        df_fragmento = df.iloc[inicio:fin]
        # Muy importante: mantener la cabecera original en cada string para que el esclavo la lea bien
        partes_csv.append(df_fragmento.to_csv(index=False))
        print(f"-> Fragmento para Esclavo {i+1}: Filas desde la {inicio} hasta la {fin-1} (Total: {len(df_fragmento)})")

    # 2. INICIALIZAR EL TRANSPORTE RDT DEL MAESTRO
    master = modulo_maestro_nativo.MasterRdt()
    master.init_master("127.0.0.1", 8000)
    
    # Registrar los n esclavos en la capa nativa de C++
    for i in range(num_esclavos):
        master.add_slave("127.0.0.1", 8001 + i)
    print("\nMaestro RDT inicializado en puerto 8000.")
    print(f"Puertos esclavos registrados: {[8001+i for i in range(num_esclavos)]}")

    # Listas para consolidar las métricas de todos los esclavos
    todos_los_losses_batches = []
    y_true_global = []
    y_pred_global = []

    # 3. COMUNICACIÓN SECUENCIAL CON CADA ESCLAVO
    for idx in range(num_esclavos):
        id_real = idx + 1
        print(f"\n[Esclavo {id_real}] Enviando fragmento de datos por RDT 3.0...")
        master.send_data_to_slave(idx, partes_csv[idx])
        print(f"[Esclavo {id_real}] Datos enviados con éxito. Esperando métricas...")

        # Recibir JSON de respuesta del esclavo correspondiente
        resultado_json_string = master.receive_data_from_slave(idx)
        print(f"[Esclavo {id_real}] ¡Métricas recibidas!")

        try:
            metricas = json.loads(resultado_json_string)
            print(f" -> Loss promedio reportado por Esclavo {id_real}: {metricas['epoch_loss']:.4f}")
            
            # Acumular datos para las gráficas globales
            todos_los_losses_batches.extend(metricas['batch_losses'])
            y_true_global.extend(metricas['y_true'])
            y_pred_global.extend(metricas['y_pred'])

        except Exception as e:
            print(f"Error procesando respuesta del Esclavo {id_real}: {e}")

    # 4. GENERAR LOS GRÁFICOS CONSOLIDADOS DE LA RED NEURONAL DISTRIBUIDA
    print("\n=== GENERANDO GRÁFICOS CONSOLIDADOS DE LA RED DISTRIBUIDA ===")

    # Gráfico 1: Evolución de Pérdidas combinando los batches procesados por toda la red
    plt.figure(figsize=(10, 5))
    plt.plot(todos_los_losses_batches, marker='o', color='orange', label='Batch loss (Distributed)')
    plt.title("Evolución del Loss en la Red Distribuida (3 Esclavos - 1 Época)")
    plt.xlabel("Iteración de Lote Acumulado")
    plt.ylabel("Loss")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.show()

    # Gráfico 2: Matriz de Confusión Global (Suma del rendimiento de los 3 esclavos)
    num_classes = 3
    cm_global = confusion_matrix(y_true_global, y_pred_global)
    disp = ConfusionMatrixDisplay(confusion_matrix=cm_global, display_labels=list(range(num_classes)))
    disp.plot(cmap=plt.cm.Greens)
    plt.title("Matriz de Confusión Global (Sistema Distribuido)")
    plt.tight_layout()
    plt.show()

    print("=== PROCESO COMPLETO COMPLETADO CON EXITO ===")

if __name__ == "__main__":
    main()
