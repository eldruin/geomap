#ifndef CELLIMAGE_HXX
#define CELLIMAGE_HXX

#include <vigra/basicimage.hxx>
#include <vigra/pixelneighborhood.hxx>
#include <functional>

namespace vigra {

namespace cellimage {

enum CellType { CellTypeRegion = 0,
                CellTypeLine = 1,
                CellTypeVertex = 2,
                CellTypeError = 3 };

// -------------------------------------------------------------------
//                          CellPixel, CellImage
// -------------------------------------------------------------------
typedef unsigned int CellLabel;

struct CellPixel
{
private:
    CellLabel typeLabel_;

public:
    CellPixel() {}
    CellPixel(CellType type, CellLabel label = 0)
    : typeLabel_(label | (type << 30))
    {}

    inline CellType type() const
        { return (CellType)(typeLabel_ >> 30); }
    inline void setType(CellType type)
        { typeLabel_ = label() | (type << 30); }

    inline CellLabel label() const
        { return typeLabel_ & 0x3fffffff; }
    inline void setLabel(CellLabel label)
        { typeLabel_ = label | (type() << 30); }
    inline void setLabel(CellLabel label, CellType type)
        { typeLabel_ = label | (type << 30); }

    bool operator==(CellPixel const & rhs) const
        { return typeLabel_ == rhs.typeLabel_; }

    bool operator!=(CellPixel const & rhs) const
        { return typeLabel_ != rhs.typeLabel_; }
};

typedef BasicImage<CellPixel> CellImage;

typedef vigra::NeighborhoodCirculator<CellImage::Iterator, EightNeighborCode>
    CellImageEightCirculator;

// -------------------------------------------------------------------
//                     CellPixel/CellImage Accessors
// -------------------------------------------------------------------
template<class VALUE_TYPE = CellType>
struct TypeAccessor
{
    typedef VALUE_TYPE value_type;
    typedef VALUE_TYPE result_type;

    template<class Iterator>
    value_type operator()(const Iterator &it) const
    {
        return it->type();
    }

    template<class Iterator>
    void set(value_type type, const Iterator &it) const
    {
        it->setType(type);
    }
};

typedef TypeAccessor<unsigned char> TypeAsByteAccessor;

struct LabelAccessor
{
    typedef CellLabel value_type;

    template<class Iterator>
    CellLabel operator()(const Iterator &it) const
    {
        return it->label();
    }

    template<class Iterator>
    void set(CellLabel label, const Iterator &it) const
    {
        it->setLabel(label);
    }
};

template<CellType type>
struct LabelWriter
{
    typedef CellLabel value_type;

    template<class Iterator>
    void set(CellLabel label, const Iterator &it) const
    {
        it->setLabel(label, type);
    }
};

template<CellType type>
struct CellTypeEquals : public std::unary_function<CellType, bool>
{
    bool operator()(CellType t) const
    {
        return t == type;
    }

    template<class Iterator>
    bool operator()(const Iterator &it) const
    {
        return it->type() == type;
    }
};

// -------------------------------------------------------------------
//                             RelabelFunctor
// -------------------------------------------------------------------
template<class VALUETYPE>
struct RelabelFunctor
{
    typedef VALUETYPE value_type;
    typedef VALUETYPE argument_type;
    typedef VALUETYPE result_type;

    RelabelFunctor(VALUETYPE oldValue, VALUETYPE newValue)
        : oldValue_(oldValue),
          newValue_(newValue)
    {}

    VALUETYPE operator()(VALUETYPE value) const
    {
        return (value == oldValue) ? newValue : value;
    }

    VALUETYPE oldValue_, newValue_;
};

// -------------------------------------------------------------------
//                              inspectCell
// -------------------------------------------------------------------
// thinking about "typename IteratorTraits<EndIterator>::DefaultAccessor":
// is not needed since we're not implementing srcCellRange here, but
// algorithms.
// srcCellRange can not be implemented that easy, because most VIGRA
// functions expect an ImageIterator, not a std::iterator
template <class EndIterator, class Accessor, class Functor>
void inspectCell(EndIterator endIterator, Accessor a, Functor & f)
{
    for(; endIterator.inRange(); ++endIterator)
        f(a(endIterator));
}

template <class EndIterator, class Functor>
void inspectCell(EndIterator endIterator, Functor & f)
{
    for(; endIterator.inRange(); ++endIterator)
        f(*endIterator);
}

// -------------------------------------------------------------------
//                             transformCell
// -------------------------------------------------------------------
template <class SrcEndIterator, class SrcAccessor,
          class DestEndIterator, class DestAccessor, class Functor>
void transformCell(SrcEndIterator srcEndIterator, SrcAccessor sa,
                   DestEndIterator destEndIterator, DestAccessor da,
                   Functor const & f)
{
    for(; endIterator.inRange(); ++srcEndIterator, ++destEndIterator)
        da.set(f(sa(srcEndIterator)), destEndIterator);
}

template <class SrcEndIterator, class DestEndIterator, class Functor>
void transformCell(SrcEndIterator srcEndIterator,
                   DestEndIterator destEndIterator,
                   Functor const & f)
{
    for(; endIterator.inRange(); ++srcEndIterator, ++destEndIterator)
        *destEndIterator = f(*srcEndIterator);
}

} // namespace cellimage

} // namespace vigra

#endif // CELLIMAGE_HXX
