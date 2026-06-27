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

    print("\n" + "="*60)
    print("INICIANDO COORDINADOR MAESTRO - ENTORNO DISTRIBUIDO RDT-UDP")
    print("="*60)
    
    csv_path = "../dataset/Diabetes.csv"
    if not os.path.exists(csv_path):
        print(f"Error: No se encontró el dataset en {csv_path}")
        return

    df = pd.read_csv(csv_path)
    total_filas = len(df)
    print(f"SISTEMA: Dataset maestro cargado. Dimensiones del archivo: {total_filas} filas")

    num_esclavos = int(sys.argv[1])
    filas_por_esclavo = total_filas // num_esclavos

    partes_csv = []
    for i in range(num_esclavos):
        inicio = i * filas_por_esclavo
        fin = total_filas if i == (num_esclavos - 1) else (i + 1) * filas_por_esclavo
        
        df_fragmento = df.iloc[inicio:fin]
        partes_csv.append(df_fragmento.to_csv(index=False))
        print(f"DISTRIBUCION: Fragmento asignado a Nodo {i+1} | Indices: {inicio} a {fin-1} | Volumen: {len(df_fragmento)} filas")

    master = modulo_maestro_nativo.MasterRdt()
    master.init_master("127.0.0.1", 8000)
    
    for i in range(num_esclavos):
        master.add_slave("127.0.0.1", 8001 + i)

    todos_los_losses_batches = []
    y_true_global = []
    y_pred_global = []

    # BUCLE 1: ENVIAR DATOS A TODOS (Esto despierta a ambos esclavos en paralelo)
    print("\n" + "="*60)
    print("FASE 1: DISTRIBUCION MASIVA SIMULTANEA (CONCURRENTE)")
    print("="*60)
    for idx in range(num_esclavos):
        id_real = idx + 1
        print(f"SISTEMA: Transmitiendo ráfagas de datos a Nodo {id_real}...")
        master.send_data_to_slave(idx, partes_csv[idx])
        print(f"ESTADO: Envío completado hacia Nodo {id_real}. Iniciando computo nativo en segundo plano.")

    # BUCLE 2: RECOLECTAR RESULTADOS (Se ejecuta después de que ambos están procesando)
    print("\n" + "="*60)
    print("FASE 2: RECOLECCION ASINCRONA DE METRICAS FINALES")
    print("="*60)
    for idx in range(num_esclavos):
        id_real = idx + 1
        print(f"SISTEMA: Sincronizando y esperando stream JSON de Nodo {id_real}...")
        resultado_json_string = master.receive_data_from_slave(idx)

        try:
            metricas = json.loads(resultado_json_string)
            print(f"LOG: Nodo {id_real} termino procesamiento. Pérdida reportada: {metricas['epoch_loss']:.4f}")
            
            todos_los_losses_batches.extend(metricas['batch_losses'])
            y_true_global.extend(metricas['y_true'])
            y_pred_global.extend(metricas['y_pred'])

        except Exception as e:
            print(f"ERROR: Fallo critico al deserializar el stream del Nodo {id_real}: {e}")

    print("\n" + "="*60)
    print("CONSOLIDACION MATEMATICA DEL CLUSTER DISTRIBUIDO")
    print("="*60)
    print(f"PROCESAMIENTO: Total de instancias recopiladas de la red: {len(y_true_global)}")

    plt.figure(figsize=(10, 5))
    plt.plot(todos_los_losses_batches, color='orange', label='Loss por lote distribuido')
    plt.title(f"Convergencia de Perdidas del Cluster ({num_esclavos} Nodos)")
    plt.xlabel("Iteracion de Lote Acumulado")
    plt.ylabel("Loss")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.show()

    cm_global = confusion_matrix(y_true_global, y_pred_global)
    disp = ConfusionMatrixDisplay(confusion_matrix=cm_global, display_labels=[0, 1, 2])
    disp.plot(cmap=plt.cm.Greens)
    plt.title(f"Matriz de Confusion Global Unificada")
    plt.tight_layout()
    plt.show()

    print("SISTEMA: Ejecucion del cluster distribuido finalizada de forma exitosa")

if __name__ == "__main__":
    main()