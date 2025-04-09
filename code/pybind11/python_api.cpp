#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/complex.h>
#include <pybind11/functional.h>
#include <pybind11/chrono.h>
#include <pybind11/operators.h>
#include "seq_db.hpp"
#include "par_db_client.hpp"
#include "util.hpp"

namespace py = pybind11;

using namespace std;

PYBIND11_MODULE(PYBIND_MODULE_NAME, m) {
    m.doc() = "Pando database"; // optional module docstring

    /* We probably don't want (or need) to give users access to the SeqDB
    py::class_<pando::SeqDB>(m, "SeqDB")
        .def(py::init<>())
        .def(py::init<string>())
        .def("install_filter", static_cast<void (SeqDB::*)(string)>(&SeqDB::install_filter))
        .def("install_filter", static_cast<void (SeqDB::*)(filter_p)>(&SeqDB::install_filter))
        .def("add_filter_dir", &SeqDB::add_filter_dir)
        .def("process", &SeqDB::process)
        .def("add_db_file", &SeqDB::add_db_file)
        .def("num_filters", &SeqDB::num_filters)
        .def("clear_filters", &SeqDB::clear_filters)
        .def("size", &SeqDB::size)
        .def("entries", [](SeqDB &self) {
            vector<DBEntry<>> entries;
            for (auto &[key, entry] : self.entries()) {
                entries.push_back(entry);
            }
            return entries;
        })
        ;
    */

    py::class_<DBEntry<>>(m, "DBEntry")
        .def(py::init<>())
        .def("value", py::overload_cast<>(&DBEntry<>::value, py::const_))
        .def("add_to_value", &DBEntry<>::add_to_value)
        .def("tags", &DBEntry<>::tags)
        .def("get_key", &DBEntry<>::get_key)
        .def("has_tag", &DBEntry<>::has_tag)
        .def("set_value", &DBEntry<>::set_value)
        .def("set_key", &DBEntry<>::set_key)
        .def("add_tag", &DBEntry<>::add_single_tag)
        .def(py::pickle( // modified version of example from https://pybind11.readthedocs.io/en/stable/advanced/classes.html
            [](const DBEntry<> &e) { // __getstate__
                /* Return a tuple that fully encodes the state of the object */
                return py::make_tuple(e.value(), e.tags(), e.get_key());
            },
            [](py::tuple t) { // __setstate__
                if (t.size() != 3)
                    throw std::runtime_error("Invalid state!");

                /* Create a new C++ instance */
                DBEntry<> e;
                e.set_value(t[0].cast<std::string>());
                for (auto & tag : t[1].cast<set<string>>()) {
                    e.add_tag(tag);
                }
                e.set_key(t[2].cast<dbkey_t>());

                return e;
            }
        ))
        ;

    py::class_<dbkey_t>(m, "dbkey_t")
        .def(py::init<>())
        .def_readwrite("a", &dbkey_t::a)
        .def_readwrite("b", &dbkey_t::b)
        .def_readwrite("c", &dbkey_t::c)
        .def(py::self == py::self)
        .def(py::pickle(
            [](const dbkey_t &k) { // __getstate__
                /* Return a tuple that fully encodes the state of the object */
                return py::make_tuple(k.a, k.b, k.c);
            },
            [](py::tuple t) { // __setstate__
                if (t.size() != 3)
                    throw std::runtime_error("Invalid state!");

                dbkey_t key {t[0].cast<chain_info_t>(), t[1].cast<vtx_t>(), t[2].cast<vtx_t>()};
                return key;
            }
        ))
        ;

    py::class_<pando::ParDBClient>(m, "ParDBClient")
        .def(py::init([](std::string server_addr) {
            elga::ZMQChatterbox::Setup();
            return std::unique_ptr<ParDBClient>(new ParDBClient(get_zmq_addr(server_addr)));
        }))
        .def("db_size", &ParDBClient::db_size)
        .def("num_neighbors", &ParDBClient::num_neighbors)
        .def("ring_size", &ParDBClient::ring_size)
        .def("process", &ParDBClient::process)
        .def("print_entries", &ParDBClient::print_entries)
        .def("add_filter_dir", static_cast<void (ParDBClient::*)(string)>(&ParDBClient::add_filter_dir))
        .def("install_filter", static_cast<void (ParDBClient::*)(string)>(&ParDBClient::install_filter))
        .def("add_db_file", static_cast<void (ParDBClient::*)(string)>(&ParDBClient::add_db_file))
        .def("get_entries", &ParDBClient::get_entries)
        .def("query", static_cast<vector<DBEntry<>> (ParDBClient::*)(string)>(&ParDBClient::query))
        .def("query", static_cast<vector<DBEntry<>> (ParDBClient::*)(vector<string>)>(&ParDBClient::query))
        .def("neighbors", &ParDBClient::neighbors)
        .def("processing", &ParDBClient::processing)
        .def("clear_filters", &ParDBClient::clear_filters)
        .def("installed_filters", &ParDBClient::installed_filters)
        .def("export_db", static_cast<void (ParDBClient::*)(string)>(&ParDBClient::export_db))
        .def("export_db_with_tag", static_cast<void (ParDBClient::*)(string, string)>(&ParDBClient::export_db_with_tag))
        .def("import_db", static_cast<void (ParDBClient::*)(string)>(&ParDBClient::import_db))
        .def("add_entry", &ParDBClient::add_entry)
        .def("get_state", &ParDBClient::get_state)
        //void add_neighbor(ZMQAddress addr) {
        //void add_entry(DBEntry<>& e) {
        ;
}
