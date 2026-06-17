#!/bin/bash

NUM_ESCLAVOS=$1

source venv/bin/activate

cd esclavo
for ((i=1; i<=NUM_ESCLAVOS; i++)); do
    python3 esclavo.py "$i" &
done

cd ../maestro
python3 maestro.py "$NUM_ESCLAVOS"