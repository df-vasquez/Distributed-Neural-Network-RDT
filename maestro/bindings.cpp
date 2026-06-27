#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "rdt_master.hpp"

namespace py = pybind11;

PYBIND11_MODULE(modulo_maestro_nativo, m) {
    m.doc() = "Modulo de Capa de Transporte RDT-UDP (Go-Back-N) para el Nodo Maestro";

    py::class_<rdt::MasterRdt>(m, "MasterRdt")
        .def(py::init<>())
        .def("init_master", &rdt::MasterRdt::init_master)
        .def("add_slave", &rdt::MasterRdt::add_slave)
        .def("send_data_to_slave", &rdt::MasterRdt::send_data_to_slave)
        .def("receive_data_from_slave", &rdt::MasterRdt::receive_data_from_slave);
}