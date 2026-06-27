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

    print(f"SISTEMA: Iniciando Nodo Esclavo {esclavo_id}")
    
    slave = modulo_esclavo_nativo.SlaveRdt()
    slave.init_slave("127.0.0.1", puerto_local)
    print(f"ESTADO: Esperando transmision de datos por canal RDT-UDP GBN")

    csv_data_string = slave.receive_data_from_master()
    print("ESTADO: Segmento de dataset recibido de forma integra")

    df = pd.read_csv(io.StringIO(csv_data_string))

    input_dim = 11
    num_classes = 3
    batch_size = 50
    num_epochs = 360

    X_np = df.iloc[:, :input_dim].values.astype(np.float32)
    y_onehot_np = df.iloc[:, -num_classes:].values.astype(np.float32)

    X_tensor = torch.tensor(X_np)
    y_tensor = torch.tensor(y_onehot_np)

    dataset = TensorDataset(X_tensor, y_tensor)
    train_loader = DataLoader(dataset, batch_size=batch_size, shuffle=True)

    model = MulticlassClassifier(input_dim=input_dim, num_classes=num_classes)
    criterion = nn.CrossEntropyLoss()
    optimizer = torch.optim.Adam(model.parameters(), lr=0.001)

    batch_losses_tracker = []
    y_true_final = []
    y_pred_final = []
    ultimo_promedio_loss = 0.0

    print("PROCESAMIENTO: Inicializando ciclo de entrenamiento concurrente")

    for epoch in range(num_epochs):
        model.train()
        epoch_loss = 0.0
        total_gradient_norm = 0.0
        num_batches = len(train_loader)
        
        # Variables temporales para calcular el accuracy real por época del set completo
        epoch_correct = 0
        epoch_total = 0
        
        for batch_x, batch_y in train_loader:
            optimizer.zero_grad()
            logits, log_vars = model(batch_x)
            loss = criterion(logits, batch_y)
            loss.backward()
            
            # Capturar la magnitud de las gradientes del lote
            grad_norm = 0.0
            for param in model.parameters():
                if param.grad is not None:
                    grad_norm += param.grad.data.norm(2).item() ** 2
            grad_norm = grad_norm ** 0.5
            total_gradient_norm += grad_norm
            
            optimizer.step()
            epoch_loss += loss.item()
            
            # Calcular métricas de entrenamiento
            predictions = torch.argmax(logits, dim=1)
            epoch_correct += (predictions == torch.argmax(batch_y, dim=1)).sum().item()
            epoch_total += batch_x.size(0)
            
            # Guardar predicciones de la última época para la matriz global
            if epoch == (num_epochs - 1):
                batch_losses_tracker.append(loss.item())
                y_true_final.extend(torch.argmax(batch_y, dim=1).tolist())
                y_pred_final.extend(predictions.tolist())

        ultimo_promedio_loss = epoch_loss / num_batches
        promedio_gradient_norm = total_gradient_norm / num_batches
        epoch_accuracy = epoch_correct / epoch_total
        
        print(f"PROCESAMIENTO: Epoca {epoch+1:03d}/{num_epochs} | Loss: {ultimo_promedio_loss:.4f} | Accuracy: {epoch_accuracy:.4f} | Grad Norm: {promedio_gradient_norm:.4f}")

    print(f"\nESTADO: Entrenamiento completado con exito en el Nodo Esclavo {esclavo_id}")

    payload_graficas = {
        "epoch_loss": ultimo_promedio_loss,
        "batch_losses": batch_losses_tracker,
        "y_true": y_true_final,
        "y_pred": y_pred_final
    }
    
    reporte_json = json.dumps(payload_graficas)

    print("ESTADO: Desplegando ráfagas GBN para el retorno del stream metrico JSON")
    slave.send_data_to_master(reporte_json)
    print("SISTEMA: Cierre seguro del nodo esclavo")

if __name__ == "__main__":
    main()