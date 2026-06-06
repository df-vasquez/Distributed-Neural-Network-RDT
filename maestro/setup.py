from setuptools import setup, Extension
import pybind11

ext_modules = [
    Extension(
        "modulo_maestro_nativo",
        ["rdt_master.cpp", "bindings.cpp"],
        include_dirs=[pybind11.get_include()],
        language='c++'
    ),
]

setup(
    name="modulo_maestro_nativo",
    ext_modules=ext_modules,
    install_requires=['pybind11>=2.10.0'],
    python_requires=">=3.6",
)