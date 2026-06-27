#!/bin/bash

NUM_ESCLAVOS=$1

source venv/bin/activate

for ((i=1; i<=NUM_ESCLAVOS; i++)); do
    gnome-terminal -- bash -c "
        cd esclavo
        python3 esclavo.py $i
        exec bash
    "
done

sleep 10

cd maestro
python3 maestro.py "$NUM_ESCLAVOS"