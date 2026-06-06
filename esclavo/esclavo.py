import sys
import io
import json
import numpy as np
import pandas as pd
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, TensorDataset
import modulo_esclavo_nativo

class MulticlassClassifier(nn.Module):
    def __init__(self, input_dim: int, num_classes: int, hidden1: int = 128, hidden2: int = 64):
        super(MulticlassClassifier, self).__init__()
        self.fc1 = nn.Linear(input_dim, hidden1)
        self.fc2 = nn.Linear(hidden1, hidden2)
        self.class_logits = nn.Linear(hidden2, num_classes)
        self.class_log_vars = nn.Linear(hidden2, num_classes)

    def forward(self, x: torch.Tensor):
        x = F.relu(self.fc1(x))
        x = F.relu(self.fc2(x))
        logits = self.class_logits(x)
        log_vars = self.class_log_vars(x)
        return logits, log_vars

def main():
    if len(sys.argv) < 2:
        print("Uso: python3 esclavo.py <id_esclavo>")
        return

    esclavo_id = sys.argv[1]
    puerto_local = 8000 + int(esclavo_id)

    print(f"=== INICIANDO NODO ESCLAVO {esclavo_id} ===")
    
    slave = modulo_esclavo_nativo.SlaveRdt()
    slave.init_slave("127.0.0.1", puerto_local)
    print(f"Esclavo {esclavo_id} esperando datos por RDT 3.0...")

    csv_data_string = slave.receive_data_from_master()
    print("¡Dataset recibido con éxito! Entrenando y recolectando métricas...")

    df = pd.read_csv(io.StringIO(csv_data_string))

    input_dim = 11
    num_classes = 3
    batch_size = 50

    X_np = df.iloc[:, :input_dim].values.astype(np.float32)
    y_onehot_np = df.iloc[:, -num_classes:].values.astype(np.float32)

    X_tensor = torch.tensor(X_np)
    y_tensor = torch.tensor(y_onehot_np)

    dataset = TensorDataset(X_tensor, y_tensor)
    train_loader = DataLoader(dataset, batch_size=batch_size, shuffle=True)

    model = MulticlassClassifier(input_dim=input_dim, num_classes=num_classes)
    criterion = nn.CrossEntropyLoss()
    optimizer = torch.optim.Adam(model.parameters(), lr=0.001)

    model.train()
    
    # Trackers idénticos a los del profesor para poder graficar
    batch_losses = []
    y_true_epoch = []
    y_pred_epoch = []
    epoch_loss = 0.0

    for batch_x, batch_y in train_loader:
        optimizer.zero_grad()
        logits, log_vars = model(batch_x)
        loss = criterion(logits, batch_y)
        loss.backward()
        optimizer.step()
        
        # Guardar la pérdida del lote actual (Mismo comportamiento que el tracker)
        batch_losses.append(loss.item())
        epoch_loss += loss.item()

        # Extraer predicciones reales vs esperadas para la matriz de confusión del profesor
        predictions = torch.argmax(logits, dim=1)
        y_true_epoch.extend(torch.argmax(batch_y, dim=1).tolist())
        y_pred_epoch.extend(predictions.tolist())

    promedio_loss = epoch_loss / len(train_loader)
    print(f"Entrenamiento completado. Loss Promedio: {promedio_loss:.4f}")

    # Empaquetamos todo en una estructura estructurada segura (JSON) para que viaje limpio por la red
    payload_graficas = {
        "epoch_loss": promedio_loss,
        "batch_losses": batch_losses,
        "y_true": y_true_epoch,
        "y_pred": y_pred_epoch
    }
    
    # Convertimos a string plano puro (sin bytes extraños) para enviar por RDT 3.0
    reporte_json = json.dumps(payload_graficas)

    print("Enviando trackers y matrices de vuelta al Maestro por RDT 3.0...")
    slave.send_data_to_master(reporte_json)
    print("¡Proceso terminado en el esclavo!")

if __name__ == "__main__":
    main()