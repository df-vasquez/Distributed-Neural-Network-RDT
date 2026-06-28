#!/bin/bash

source .venv/bin/activate

cd maestro
python3 setup.py build_ext --inplace

cd ..
cd esclavo
python3 setup.py build_ext --inplace