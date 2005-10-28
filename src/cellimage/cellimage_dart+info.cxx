#include "foureightsegmentation.hxx"
#include <vigra/pythonutil.hxx>

using namespace vigra;
using namespace boost::python;
using namespace vigra::cellimage;

class DartIterator
{
public:
    DartIterator(const GeoMap::DartTraverser &dart)
    : it_(dart.neighborCirculator()),
      cellsUL_(dart.segmentation()->cells)
    {}

    DartIterator __iter__()
    {
        return *this;
    }

    Diff2D next()
    {
        if(it_.atEnd())
        {
            PyErr_SetString(PyExc_StopIteration, "");
            boost::python::throw_error_already_set();
        }
        Diff2D result(it_.neighborCirculator().base() - cellsUL_);
        ++it_;
        return result;
    }

    EdgelIterator it_;
    CellImage::traverser cellsUL_;
};

DartIterator DartTraverser__iter__(const GeoMap::DartTraverser &dart)
{
    return DartIterator(dart);
}

void defineDartTraverser()
{
    class_<GeoMap::DartTraverser> scope(
        "DartTraverser", init<const GeoMap::DartTraverser &>());
    scope
        .def("nextAlpha", &GeoMap::DartTraverser::nextAlpha, return_internal_reference<>())
        .def("prevAlpha", &GeoMap::DartTraverser::prevAlpha, return_internal_reference<>())
        .def("nextPhi", &GeoMap::DartTraverser::nextPhi, return_internal_reference<>())
        .def("prevPhi", &GeoMap::DartTraverser::prevPhi, return_internal_reference<>())
        .def("nextSigma", &GeoMap::DartTraverser::nextSigma, return_internal_reference<>())
        .def("prevSigma", &GeoMap::DartTraverser::prevSigma, return_internal_reference<>())
        .def("isSingular", &GeoMap::DartTraverser::isSingular)
		//.def("carefulNextSigma", &GeoMap::DartTraverser::carefulNextSigma)
		//.def("carefulPrevSigma", &GeoMap::DartTraverser::carefulPrevSigma)
        .def("startNodeLabel", &GeoMap::DartTraverser::startNodeLabel)
        .def("endNodeLabel", &GeoMap::DartTraverser::endNodeLabel)
        .def("edgeLabel", &GeoMap::DartTraverser::edgeLabel)
        .def("leftFaceLabel", &GeoMap::DartTraverser::leftFaceLabel)
        .def("rightFaceLabel", &GeoMap::DartTraverser::rightFaceLabel)
        .def("startNode", &GeoMap::DartTraverser::startNode, return_internal_reference<>())
        .def("endNode", &GeoMap::DartTraverser::endNode, return_internal_reference<>())
        .def("edge", &GeoMap::DartTraverser::edge, return_internal_reference<>())
        .def("leftFace", &GeoMap::DartTraverser::leftFace, return_internal_reference<>())
        .def("rightFace", &GeoMap::DartTraverser::rightFace, return_internal_reference<>())
        .def("__iter__", &DartTraverser__iter__)
        .def(self == self)
        .def(self != self);

    class_<DartIterator>("DartIterator", init<const GeoMap::DartTraverser &>())
        .def("__iter__", &DartIterator::__iter__)
        .def("next", &DartIterator::next);
}

GeoMap::DartTraverser &contourGetItem(
	std::vector<GeoMap::DartTraverser> &contours, long index)
{
	if(index >= (long)contours.size())
	{
		PyErr_SetObject(PyExc_IndexError, incref(object(index).ptr()));
		throw_error_already_set();
	}
	return contours[index];
}

void defineCellInfos()
{
	class_<std::vector<GeoMap::DartTraverser> >("Contours", no_init)
		.def("__getitem__", &contourGetItem,
             return_internal_reference<>())
		.def("__len__", &std::vector<GeoMap::DartTraverser>::size);

    class_<GeoMap::CellInfo>("CellInfo", no_init)
        .def_readonly("label", &GeoMap::CellInfo::label)
        .def_readwrite("bounds", &GeoMap::NodeInfo::bounds)
        .def_readwrite("size", &GeoMap::NodeInfo::size)
        .def("initialized", &GeoMap::CellInfo::initialized)
        .def("uninitialize", &GeoMap::CellInfo::uninitialize);

    class_<GeoMap::NodeInfo,
           bases<GeoMap::CellInfo> >("NodeInfo", no_init)
        .def_readwrite("anchor", &GeoMap::NodeInfo::anchor)
        .def_readwrite("degree", &GeoMap::NodeInfo::degree);

    class_<GeoMap::EdgeInfo,
           bases<GeoMap::CellInfo> >("EdgeInfo", no_init)
        .def_readwrite("start", &GeoMap::EdgeInfo::start)
        .def_readwrite("end", &GeoMap::EdgeInfo::end);

    class_<GeoMap::FaceInfo,
		   bases<GeoMap::CellInfo> >("FaceInfo", no_init)
		.def_readonly("contours", &GeoMap::FaceInfo::contours);
}
