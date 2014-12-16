#include "cellimage_module.hxx"
#include "cellstatistics.hxx"
#define protected public
#include "cellpyramid.hxx"

using namespace vigra;
using namespace boost::python;
using namespace vigra::cellimage;

typedef vigra::CellPyramid<GeoMap, CellStatistics>
    GeoMapPyramid;

using namespace boost::python;

GeoMapPyramid::Operation &historyGetItem(GeoMapPyramid::History &history,
										 long index)
{
	if((index >= (long)history.size()) || (index < -(long)history.size()))
	{
		PyErr_SetObject(PyExc_IndexError, incref(object(index).ptr()));
		throw_error_already_set();
	}
	if(index < 0)
		index += history.size();
	return history[index];
}

void definePyramid()
{
	scope fourEightPyramidScope(
	class_<GeoMapPyramid>("GeoMapPyramid", no_init)
		//init<const GeoMap&>())
		.def("storeCheckpoint", &GeoMapPyramid::storeCheckpoint)
		.def("removeIsolatedNode", &GeoMapPyramid::removeIsolatedNode,
			 return_internal_reference<>())
		.def("mergeFaces", &GeoMapPyramid::mergeFaces,
			 return_internal_reference<>())
		.def("removeBridge", &GeoMapPyramid::removeBridge,
			 return_internal_reference<>())
		.def("mergeEdges", &GeoMapPyramid::mergeEdges,
			 return_internal_reference<>())
		.def("removeEdge", &GeoMapPyramid::removeEdge,
			 return_internal_reference<>())
		.def("removeEdgeWithEnds", &GeoMapPyramid::removeEdgeWithEnds,
			 return_internal_reference<>())
		.def("beginComposite", &GeoMapPyramid::beginComposite)
		.def("changeIntoComposite", &GeoMapPyramid::changeIntoComposite)
		.def("endComposite", &GeoMapPyramid::endComposite)
		.def("topLevel", (GeoMapPyramid::Level &(GeoMapPyramid::*)(void))
			 &GeoMapPyramid::topLevel, return_internal_reference<>())
		.def("__get__", (GeoMapPyramid::Level *(GeoMapPyramid::*)(unsigned int))
			 &GeoMapPyramid::getLevel, return_value_policy<manage_new_object>())
		.def("__len__", &GeoMapPyramid::levelCount)
		.def("cutAbove", (void(GeoMapPyramid::*)(const GeoMapPyramid::Level &))
			 &GeoMapPyramid::cutAbove)
		.def("cutAbove", (void(GeoMapPyramid::*)(unsigned int))
			 &GeoMapPyramid::cutAbove)
		.def_readonly("history", &GeoMapPyramid::history_));

	class_<GeoMapPyramid::Level>("Level", no_init)
		.def("index", &GeoMapPyramid::Level::index)
		.def("segmentation", &GeoMapPyramid::Level::segmentation,
			 return_internal_reference<>())
		.def("cellStatistics", &GeoMapPyramid::Level::cellStatistics,
			 return_internal_reference<>())
		.def("approachLevel", &GeoMapPyramid::Level::approachLevel)
		.def("gotoLevel", &GeoMapPyramid::Level::gotoLevel);

	enum_<GeoMapPyramid::OperationType>("OperationType")
		.value("RemoveIsolatedNode", GeoMapPyramid::RemoveIsolatedNode)
		.value("MergeFaces", GeoMapPyramid::MergeFaces)
		.value("RemoveBridge", GeoMapPyramid::RemoveBridge)
		.value("MergeEdges", GeoMapPyramid::MergeEdges)
		.value("RemoveEdge", GeoMapPyramid::RemoveEdge)
		.value("RemoveEdgeWithEnds", GeoMapPyramid::RemoveEdgeWithEnds)
		.value("RemoveEdgeWithEnds", GeoMapPyramid::Composite);

	class_<GeoMapPyramid::Operation>("Operation", no_init)
		.def_readonly("type", &GeoMapPyramid::Operation::type);
//		.def_readonly("param", &GeoMapPyramid::Operation::param);

	class_<GeoMapPyramid::History>("History", no_init)
		.def("__len__", &GeoMapPyramid::History::size)
		.def("__getitem__", &historyGetItem,
			 return_internal_reference<>());
}
