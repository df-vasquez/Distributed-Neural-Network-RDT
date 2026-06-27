#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "rdt_slave.hpp"

namespace py = pybind11;

PYBIND11_MODULE(modulo_esclavo_nativo, m) {
    m.doc() = "Modulo de Capa de Transporte RDT-UDP (Go-Back-N) para el Nodo Esclavo";

    py::class_<rdt::SlaveRdt>(m, "SlaveRdt")
        .def(py::init<>())
        .def("init_slave", &rdt::SlaveRdt::init_slave)
        .def("receive_data_from_master", &rdt::SlaveRdt::receive_data_from_master)
        .def("send_data_to_master", &rdt::SlaveRdt::send_data_to_master);
}