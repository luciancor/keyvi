// -*- mode: c++; tab-width: 4; indent-tabs-mode: t; eval: (progn (c-set-style "stroustrup") (c-set-offset 'innamespace 0)); -*-
// vi:set ts=4 sts=4 sw=4 noet :
// Copyright 2010, The TPIE development team
// 
// This file is part of TPIE.
// 
// TPIE is free software: you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
// 
// TPIE is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
// License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with TPIE.  If not, see <http://www.gnu.org/licenses/>

#ifndef TPIE_SERIALIZATION2_H
#define TPIE_SERIALIZATION2_H

///////////////////////////////////////////////////////////////////////////////
/// \file tpie/serialization2.h Binary serialization and unserialization.
///
/// This serialization framework is based on generic writers that have a write
/// method accepting a source buffer and a byte size as parameters, and generic
/// readers that have a read method accepting a destination buffer and a byte
/// size as parameters.
///////////////////////////////////////////////////////////////////////////////

#include <tpie/config.h>
#include <tpie/portability.h>
#include <typeinfo>
#include <boost/type_traits/is_pod.hpp>
#include <boost/type_traits/is_pointer.hpp>
#include <boost/utility/enable_if.hpp>
#include <tpie/is_simple_iterator.h>

namespace tpie {

#ifdef DOXYGEN

///////////////////////////////////////////////////////////////////////////////
// The following two declarations are for documentation purposes only.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
/// \brief  Sample tpie::serialize prototype.
///
/// To enable serialization of your own type, overload tpie::serialize.
/// This docstring is an example for a type named foo, but it is for exposition
/// purposes only.
///
/// The implementation of tpie::serialize(dst, v) shall call dst.write(src, n)
/// a number of times. Each time, src is a const pointer to a byte buffer of
/// size n (bytes) that represents a piece of the serialized object.
///
/// A common idiom for polymorphic and/or variable-sized objects is to first
/// serialize a constant-size tag or length and then serialize the variably
/// sized payload. For this purpose, you may want to use
/// tpie::serialize(dst, a, b) to serialize all elements in the range [a, b).
///////////////////////////////////////////////////////////////////////////////
template <typename D>
void serialize(D & dst, const foo & v);

///////////////////////////////////////////////////////////////////////////////
/// \brief  Sample tpie::unserialize prototype.
///
/// To enable unserialization of your own type, overload tpie::unserialize.
/// This docstring is an example for a type named foo, but it is for exposition
/// purposes only.
///
/// The implementation of tpie::unserialize(src, v) shall call src.read(dst, n)
/// a number of times. Each time, src is a pointer to a byte buffer that can
/// hold at least n bytes, where n is the number of bytes to be read.
///
/// See also tpie::serialize.
///////////////////////////////////////////////////////////////////////////////
template <typename S>
void unserialize(S & src, foo & v);

#endif // DOXYGEN
///////////////////////////////////////////////////////////////////////////////
// Library implementations of tpie::serialize and tpie::unserialize.
///////////////////////////////////////////////////////////////////////////////

template <bool> struct is_trivially_serializable_enable_if { };
template <> struct is_trivially_serializable_enable_if<true> {typedef void type;};

template <typename T>
struct is_trivially_serializable {
private:
	template <typename TT>
	static char magic(TT *, typename is_simple_iterator_enable_if<TT::is_trivially_serializable>::type *_=0);

	template <typename TT>
	static long magic(...);
public:
	static bool const value=
		boost::is_pod<T>::value || sizeof(magic<T>((T*)0))==sizeof(char);
};

///////////////////////////////////////////////////////////////////////////////
/// \brief tpie::serialize for POD/array types.
///////////////////////////////////////////////////////////////////////////////
template <typename D, typename T>
void serialize(D & dst, const T & v,
			   typename boost::enable_if<is_trivially_serializable<T> >::type * = 0,
			   typename boost::disable_if<boost::is_pointer<T> >::type * = 0) {
	dst.write((const char *)&v, sizeof(T));
}

///////////////////////////////////////////////////////////////////////////////
/// \brief tpie::unserialize for POD/array types.
///////////////////////////////////////////////////////////////////////////////
template <typename S, typename T>
void unserialize(S & src, T & v,
				 typename boost::enable_if<is_trivially_serializable<T> >::type * = 0,
				 typename boost::disable_if<boost::is_pointer<T> >::type * = 0) {
	src.read((char *)&v, sizeof(T));
}

namespace bits {

///////////////////////////////////////////////////////////////////////////////
/// \brief Helper to facilitate fast serialization of trivially copyable
/// arrays.
///////////////////////////////////////////////////////////////////////////////
template <typename D, typename T,
		  bool is_simple_itr=tpie::is_simple_iterator<T>::value,
		  bool is_pod=boost::is_pod<typename std::iterator_traits<T>::value_type>::value,
		  bool is_pointer=boost::is_pointer<typename std::iterator_traits<T>::value_type>::value>
struct array_encode_magic {
	void operator()(D & dst, T start, T end) {
		using tpie::serialize;
		for (T i=start; i != end; ++i) serialize(dst, *i);
	}
};

template <typename D, typename T>
struct array_encode_magic<D, T, true, true, false> {
	void operator()(D & d, T start, T end) {
		if (start == end) {
			// Do not dereference two iterators pointing to null
			return;
		}
		const char * from = reinterpret_cast<const char *>(&*start);
		const char * to = reinterpret_cast<const char *>(&*end);
		d.write(from, to-from);
	}
};

///////////////////////////////////////////////////////////////////////////////
/// \brief Helper to facilitate fast unserialization of trivially copyable
/// arrays.
///////////////////////////////////////////////////////////////////////////////
template <typename D, typename T,
		  bool is_simple_itr=tpie::is_simple_iterator<T>::value,
		  bool is_pod=boost::is_pod<typename std::iterator_traits<T>::value_type>::value,
		  bool is_pointer=boost::is_pointer<typename std::iterator_traits<T>::value_type>::value>
struct array_decode_magic {
	void operator()(D & dst, T start, T end) {
		using tpie::unserialize;
		for (T i=start; i != end; ++i) unserialize(dst, *i);
	}
};

template <typename D, typename T>
struct array_decode_magic<D, T, true, true, false> {
	void operator()(D & d, T start, T end) {
		if (start == end) {
			// Do not dereference two iterators pointing to null
			return;
		}
		char * from = reinterpret_cast<char *>(&*start);
		char * to = reinterpret_cast<char *>(&*end);
		d.read(from, to-from);
	}
};

///////////////////////////////////////////////////////////////////////////////
/// \brief Helper to count the serialized size of objects.
///////////////////////////////////////////////////////////////////////////////
struct counter {
	size_t size;
	counter(): size(0) {}
	void write(const void *, size_t s) {size += s;}
};

} // namespace bits

///////////////////////////////////////////////////////////////////////////////
/// \brief Serialize an array of serializables.
///
/// This uses direct memory copying for POD typed arrays, and tpie::serialize
/// for proper objects.
///////////////////////////////////////////////////////////////////////////////
template <typename D, typename T>
void serialize(D & dst, T start, T end) {
	bits::array_encode_magic<D, T> magic;
	magic(dst, start, end);
}

///////////////////////////////////////////////////////////////////////////////
/// \brief Unserialize an array of serializables.
///
/// This uses direct memory copying for POD typed arrays, and tpie::unserialize
/// for proper objects.
///////////////////////////////////////////////////////////////////////////////
template <typename D, typename T>
void unserialize(D & dst, T start, T end) {
	bits::array_decode_magic<D, T> magic;
	magic(dst, start, end);
}

///////////////////////////////////////////////////////////////////////////////
/// \brief tpie::serialize for std::vectors of serializable items.
///////////////////////////////////////////////////////////////////////////////
template <typename D, typename T, typename alloc_t>
void serialize(D & dst, const std::vector<T, alloc_t> & v) {
	using tpie::serialize;
	serialize(dst, v.size());
	serialize(dst, v.begin(), v.end());
}

///////////////////////////////////////////////////////////////////////////////
/// \brief tpie::unserialize for std::vectors of unserializable items.
///////////////////////////////////////////////////////////////////////////////
template <typename S, typename T, typename alloc_t>
void unserialize(S & src, std::vector<T, alloc_t> & v) {
	typename std::vector<T>::size_type s;
	using tpie::unserialize;
	unserialize(src, s);
	v.resize(s);
	unserialize(src, v.begin(), v.end());
}

///////////////////////////////////////////////////////////////////////////////
/// \brief tpie::serialize for std::basic_strings of serializable items,
/// including std::strings.
///////////////////////////////////////////////////////////////////////////////
template <typename D, typename T>
void serialize(D & dst, const std::basic_string<T> & v) {
	using tpie::serialize;
	serialize(dst, v.size());
	serialize(dst, v.c_str(), v.c_str() + v.size());
}

///////////////////////////////////////////////////////////////////////////////
/// \brief tpie::unserialize for std::basic_strings of unserializable items,
/// including std::strings.
///////////////////////////////////////////////////////////////////////////////
template <typename S, typename T>
void unserialize(S & src, std::basic_string<T> & v) {
	typename std::basic_string<T>::size_type s;
	using tpie::unserialize;
	unserialize(src, s);
	v.resize(s);
	unserialize(src, v.c_str(), v.c_str() + v.size());
}

///////////////////////////////////////////////////////////////////////////////
/// \brief Given a serializable, serialize it and measure its serialized size.
///////////////////////////////////////////////////////////////////////////////
template <typename T>
size_t serialized_size(const T & v) {
	using tpie::serialize;
	bits::counter c;
	serialize(c, v);
	return c.size;
}

} // namespace tpie

#endif // TPIE_SERIALIZATION2_H
