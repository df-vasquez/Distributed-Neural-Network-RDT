from setuptools import setup, Extension
import pybind11

ext_modules = [
    Extension(
        "modulo_esclavo_nativo",
        ["rdt_slave.cpp", "bindings.cpp"],
        include_dirs=[pybind11.get_include()],
        language='c++'
    ),
]

setup(
    name="modulo_esclavo_nativo",
    ext_modules=ext_modules,
    install_requires=['pybind11>=2.10.0'],
    python_requires=">=3.6",
)