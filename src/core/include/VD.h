/*                                              -*- mode:C++ -*- */
/*
  VD.h A description of a typed, sized, bounded, positioned value
  Copyright (C) 2014 The Regents of the University of New Mexico.  All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
  USA
*/

/**
   \file VD.h A description of a typed, sized, bounded, positioned value
   \author David H. Ackley.
   \date (C) 2014 All rights reserved.
   \lgpl
*/

#ifndef VD_H
#define VD_H

#include "itype.h"
#include "Fail.h"
#include "Util.h"

namespace MFM
{
  template <class EC> class Atom;  // FORWARD

  /**
   * A VD is a 'value description'.
   */

  class VD
  {
  public:
    /**
     * VD::Type enumerates the possible value types that may be found
     * in a value described by a VD (i.e., a field in an Atom).
     */
    enum Type {
      INVALID=0,
      U32,
      S32,
      BOOL,
      UNARY,
      BITS,
      TYPE_COUNT
    };

    static bool ValidType(u32 type)
    {
      return type > INVALID && type < TYPE_COUNT;
    }

    static void AssertValidType(u32 type)
    {
      MFM_API_ASSERT_ARG(ValidType(type));
    }

    static const char *(m_typeNames[TYPE_COUNT]);

    static const char * GetTypeName(u32 t)
    {
      if (!ValidType(t))
      {
        t = INVALID;
      }
      return m_typeNames[t];
    }

    const u32 m_type;
    const u32 m_length;
    const u32 m_start;

    const s32 m_min;
    const s32 m_vdef;
    const s32 m_max;

    const u64 m_longdef;

    static s32 GetMin(u32 type, u32 bits)
    {
      switch (type)
      {
      case U32: return 0;
      case S32: return bits==0 ? 0 : -MakeMask(bits-1) - 1;
      case BOOL: return 0;
      case UNARY: return 0;
      case BITS: // FALL THROUGH
      default: FAIL(ILLEGAL_ARGUMENT);
      }
    }

    static s32 GetMax(u32 type, u32 bits)
    {
      switch (type)
      {
      case U32: return MakeMask(bits > 31 ? 31 : bits);  // Not 32 because we use all-s32
      case S32: return MakeMask(bits-1);
      case BOOL: return 1;
      case UNARY: return bits;
      case BITS: // FALL THROUGH
      default: FAIL(ILLEGAL_ARGUMENT);
      }
    }

    bool InRangeByLength(s32 val) const
    {
      return GetMin(m_type, m_length) <= val && GetMax(m_type, m_length) >= val;
    }

    void AssertInRangeByLength(s32 val) const
    {
      MFM_API_ASSERT(InRangeByLength(val), OUT_OF_BOUNDS);
    }

    u32 GetType() const
    {
      return m_type;
    }

    s32 GetMin() const
    {
      return m_min;
    }

    s32 GetMax() const
    {
      return m_max;
    }

    s32 GetDefault() const
    {
      return m_vdef;
    }

    u64 GetLongDefault() const
    {
      return m_longdef;
    }

    bool InRange(s32 val) const
    {
      return m_min <= val && m_max >= val;
    }

    void AssertInRange(s32 val) const
    {
      MFM_API_ASSERT(InRange(val), OUT_OF_BOUNDS);
    }

    VD(u32 type, u32 len, u32 pos, s32 min, s32 vdef, s32 max)
      : m_type(type), m_length(len), m_start(pos), m_min(min), m_vdef(vdef), m_max(max), m_longdef(0)
    {
      AssertValidType(m_type);
      MFM_API_ASSERT_ARG(m_length <= 32 && m_type != BITS);
      AssertInRangeByLength(m_min);
      AssertInRangeByLength(m_max);
      AssertInRange(m_vdef);
    }

    VD(u32 type, u32 len, u32 pos, u64 longdef)
      : m_type(type), m_length(len), m_start(pos), m_min(0), m_vdef(0), m_max(0), m_longdef(longdef)
    {
      MFM_API_ASSERT_ARG(m_type == BITS && m_length <= 64);
    }

    void AssertIsType(u32 type) const
    {
      MFM_API_ASSERT_ARG(m_type == type);
    }

    static u32 MakeMask(u32 length)
    {
      return _GetNOnes32(length);
    }

    /////////
    /// The following are the slow-path, general-purpose, Atom field
    /// accessors, for use by the reflection system.  In these methods,
    /// the field length and start position are passed as runtime
    /// function arguments rather than compile-time template arguments
    /// (which see BitField.h for those).

    /////////
    /// Raw bits value
    template <class T> static u32 GetFieldAsBits(const u32 length, const u32 start, const T& a) ;
    template <class T> static void SetFieldAsBits(const u32 length, const u32 start, T& a, const u32 val) ;

    template <class T> static u64 GetLongFieldAsBits(const u32 length, const u32 start, const T& a) ;
    template <class T> static void SetLongFieldAsBits(const u32 length, const u32 start, T& a, const u64 val) ;

    /**
     * Get a raw bit field cast as an s32 -- without checking whether
     * this VD is an S32.  For use by generic routines like Sliders
     * that treat everything as an S32.
     */
    template <class T> s32 GetBitsAsS32(const T& a) const ;

    /**
     * Set a raw bit field from an s32 -- without checking whether
     * this VD is an S32.  For use by generic routines like Sliders
     * that treat everything as an S32.
     */
    template <class T> void SetBitsAsS32(T& a, s32 val) const ;

    /**
     * Get a raw bit field cast as a u64 -- without checking whether
     * this VD is a BITS.
     */
    template <class T> u64 GetBitsAsU64(const T& a) const ;

    /**
     * Set a raw bit field from an u64 -- without checking whether
     * this VD is a BITS.
     */
    template <class T> void SetBitsAsU64(T& a, u64 val) const ;

    /////////
    /// u32 value
    template <class T> static u32 GetFieldAsU32(const u32 length, const u32 start, const T& a) ;
    template <class T> static void SetFieldAsU32(const u32 length, const u32 start, T& a, const u32 val) ;
    template <class T> u32 GetValueU32(const T& a) const ;
    template <class T> void SetValueU32(T& a, const u32 val) const ;

    /////////
    /// s32 value
    template <class T> static s32 GetFieldAsS32(const u32 length, const u32 start, const T& a) ;
    template <class T> static void SetFieldAsS32(const u32 length, const u32 start, T& a, const s32 val) ;
    template <class T> s32 GetValueS32(const T& a) const ;
    template <class T> void SetValueS32(T& a, const s32 val) const ;

    /////////
    /// bool value
    template <class T> static bool GetFieldAsBool(const u32 length, const u32 start, const T& a) ;
    template <class T> static void SetFieldAsBool(const u32 length, const u32 start, T& a, const bool val) ;
    template <class T> bool GetValueBool(const T& a) const ;
    template <class T> void SetValueBool(T& a, const bool val) const ;

    /////////
    /// unary value
    template <class T> static u32 GetFieldAsUnary(const u32 length, const u32 start, const T& a) ;
    template <class T> static void SetFieldAsUnary(const u32 length, const u32 start, T& a, const u32 val) ;
    template <class T> u32 GetValueUnary(const T& a) const ;
    template <class T> void SetValueUnary(T& a, const u32 val) const ;

    ///Generic by VD type
    template <class T> void StoreValueByType(T & a, const u32 val) const ;

    ///Generic by VD type -> s32
    template <class T> s32 LoadValueByType(T & a) const ;

  };

  template <VD::Type VTYPE> struct VTypeToType { /* INVALID WON'T COMPILE WITHOUT TYPE */ };
  template<> struct VTypeToType<VD::U32>  { typedef u32 TYPE; };
  template<> struct VTypeToType<VD::S32>  { typedef s32 TYPE; };
  template<> struct VTypeToType<VD::BOOL>  { typedef bool TYPE; };
  template<> struct VTypeToType<VD::UNARY>  { typedef u32 TYPE; };
  template<> struct VTypeToType<VD::BITS>  { typedef u64 TYPE; };
}

#include "VD.tcc"

#endif /* VD_H */
