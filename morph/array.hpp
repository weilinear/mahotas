/* Copyright 2008 (C)
 * Luís Pedro Coelho <lpc@cmu.edu>
 * License GPL Version 2.
 */

#include <iterator>
extern "C" {
    #include <Python.h>
    #include <numpy/ndarrayobject.h>
}

#include <stdio.h>

namespace numpy {

struct position {
    position()
        :nd_(0) {
        }
    position(const npy_intp* pos, int nd)
        :nd_(nd)
        { for (int i = 0; i != nd_; ++i) position_[i]=pos[i]; }
    int nd_;
    npy_intp position_[NPY_MAXDIMS];
    bool operator == (const position& other) { return !std::memcmp(this->position_,other.position_,sizeof(this->position_[0])*this->nd_); }
    bool operator != (const position& other) { return !(*this == other); }
};

position operator + (const position& a, const position& b) {
    assert(a.nd_ == b.nd_);
    position res = a;
    for (int i = 0; i != a.nd_; ++i) res.position_[i] += b.position_[i];
    return res;
}

position operator - (const position& a, const position& b) {
    assert(a.nd_ == b.nd_);
    position res = a;
    for (int i = 0; i != a.nd_; ++i) res.position_[i] -= b.position_[i];
    return res;
}

template <typename BaseType>
struct iterator_base : std::iterator<std::forward_iterator_tag, BaseType>{
    protected:
        BaseType* data_;
        // steps is similar to strides, but more useful for iteration, see implementation of operator ++
        // Also, I divide by sizeof(BaseType)
        int steps_[NPY_MAXDIMS];
        int dimensions_[NPY_MAXDIMS];
        // This is not actually the position we are at, but the reverse of the position!
        ::numpy::position position_;

    public:
        iterator_base(PyArrayObject* array) {
            int nd = array->nd;
            position_.nd_=nd;
            data_=reinterpret_cast<BaseType*>(array->data);
            for (int i = 0; i != position_.nd_; ++i) position_.position_[i]=0;
            unsigned cummul = 0;
            for (int i = 0; i != position_.nd_; ++i) {
                dimensions_[i] = array->dimensions[nd-i-1];
                steps_[i] = array->strides[nd-i-1]/sizeof(BaseType)-cummul;
                cummul *= array->dimensions[nd-i-1];
                cummul += steps_[i]*array->dimensions[nd-i-1];
            }
        }

        iterator_base& operator ++ () {
            for (int i = 0; i != position_.nd_; ++i) {
                data_ += steps_[i];
                ++position_.position_[i];
                if (position_.position_[i] != dimensions_[i]) {
                    return *this;
                }
                position_.position_[i] = 0;
            }
            return *this;
        }

        bool operator == (const iterator_base& other) { return this->position_ == other.position_; }
        bool operator != (const iterator_base& other) { return !(*this == other); }

        ::numpy::position position() const {
            ::numpy::position res = position_;
            std::reverse(res.position_,res.position_+res.nd_);
            return res;
        }
};

template <typename BaseType>
class iterator_type : public iterator_base<BaseType> {
    public:
        iterator_type(PyArrayObject* array)
            :iterator_base<BaseType>(array) {
            }
        BaseType operator * () {
            BaseType res;
            std::memcpy(&res,this->data_,sizeof(res));
            return res;
        }
};

template <typename BaseType>
class aligned_iterator_type : public iterator_base<BaseType> {
    public:
        aligned_iterator_type(PyArrayObject* array)
            :iterator_base<BaseType>(array) {
                assert(PyArray_ISALIGNED(array));
            }
        BaseType& operator * () {
            return *this->data_;
        }
};


template <typename BaseType>
class array_base {
    protected:
        PyArrayObject* array_;

        void* raw_data(const position& pos) {
            assert(this->validposition(pos));
            return PyArray_GetPtr(array_,const_cast<npy_intp*>(pos.position_));
        }
    public:
        array_base(PyArrayObject* array)
            :array_(array)
            {
                Py_INCREF(array_);
            }

        ~array_base() {
            Py_XDECREF(array_);
        }
        
        unsigned size() const { return PyArray_SIZE(array_); }
        unsigned ndims() const { return PyArray_NDIM(array_); }
        unsigned dim(unsigned i) const {
            assert(i < this->ndims());
            return PyArray_DIM(array_,i);
        }

        PyArrayObject* raw_array() const { return array_; }
        void* raw_data() const { return PyArray_DATA(array_); }
        const npy_intp* raw_dims() const { return array_->dimensions; }

        bool validposition(const position& pos) const {
            if (ndims() != pos.nd_) return false;
            for (int i=0; i != pos.nd_; ++i) {
                if (pos.position_[i] < 0 || pos.position_[i] >= this->dim(i)) return false;
            }
            return true;
        }
        bool is_aligned() const {
            return PyArray_ISALIGNED(array_);
        }

        BaseType at(const position& pos) const {
            BaseType val;
            void* datap=raw_data(pos);
            memcpy(&val,datap,sizeof(BaseType));
            return val;
        }
};

template<typename BaseType>
struct array : public array_base<BaseType> {
    public:
        array(PyArrayObject* array)
            :array_base<BaseType>(array) {
            }
        typedef iterator_type<BaseType> iterator;
        typedef iterator_type<const BaseType> const_iterator;

        iterator begin() {
            return iterator(this->array_);
        }
        iterator end() {
            iterator res = begin();
            for (unsigned i = 0, N = this->size(); i!= N; ++i) {
                ++res;
            }
            return res;
        }
};

template <typename BaseType>
struct aligned_array : public array_base<BaseType> {
    public:
        aligned_array(PyArrayObject* array)
            :array_base<BaseType>(array) {
                assert(PyArray_ISALIGNED(array));
            }
        typedef aligned_iterator_type<BaseType> iterator;
        typedef aligned_iterator_type<const BaseType> const_iterator;

        const_iterator begin() const {
            return const_iterator(this->array_);
        }
        iterator begin() {
            return iterator(this->array_);
        }
        iterator end() {
            iterator res = begin();
            for (unsigned i = 0, N = this->size(); i!= N; ++i) {
                ++res;
            }
            return res;
        }

        BaseType* data() {
            return reinterpret_cast<BaseType*>PyArray_DATA(this->array_);
        }
        const BaseType* data(const position& pos) const {
            return reinterpret_cast<const BaseType*>(raw_data(pos));
        }

        BaseType* data(const position& pos) {
            return reinterpret_cast<BaseType*>(raw_data(pos));
        }

        BaseType& at(const position& pos) {
            return *data(pos);
        }
        BaseType at(const position& pos) const {
            return *data(pos);
        }
};

template <typename BaseType>
aligned_array<BaseType> array_like(const array_base<BaseType>& orig) {
    PyArrayObject* array = orig.raw_array();
    return aligned_array<BaseType>((PyArrayObject*)PyArray_FromDims(array->nd,array->dimensions,PyArray_TYPE(array)));
}

} // namespace numpy

