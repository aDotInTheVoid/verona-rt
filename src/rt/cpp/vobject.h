// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "../region/region_api.h"

#include <new>
#include <type_traits>

namespace verona::rt
{
  using namespace snmalloc;

  // These helpers are used to determine if various methods are provided by the
  // child class of V<>. They intentionally only check for the name of the
  // method, not for its precise signature.
  //
  // If for example a class C has a notified method with an incorrect
  // signature, `has_notified<C>` will still be true. However the
  // implementation of V<C> (in this case `gc_notified`) would not compile.
  //
  // This is better than ignoring methods with the right name but the wrong
  // signature.
  template<class T, class = void>
  struct has_notified : std::false_type
  {};
  template<class T>
  struct has_notified<T, std::void_t<decltype(&T::notified)>> : std::true_type
  {};

  template<class T, class = void>
  struct has_finaliser : std::false_type
  {};
  template<class T>
  struct has_finaliser<T, std::void_t<decltype(&T::finaliser)>> : std::true_type
  {};

  template<class T>
  struct has_destructor
  {
    constexpr static bool value = !std::is_trivially_destructible_v<T>;
  };

  /**
   * Common base class for V and VCown to build descriptors
   * from C++ objects using compile time reflection.
   */
  template<class T, class Base = Object>
  class VBase : public Base
  {
  private:
    static void gc_trace(const Object* o, ObjectStack& st)
    {
      ((T*)o)->trace(st);
    }

    static void gc_notified(Object* o)
    {
      if constexpr (has_notified<T>::value)
        ((T*)o)->notified(o);
      else
      {
        UNUSED(o);
      }
    }

    static void gc_final(Object* o, Object* region, ObjectStack& sub_regions)
    {
      if constexpr (has_finaliser<T>::value)
        ((T*)o)->finaliser(region, sub_regions);
      else
      {
        UNUSED(o);
        UNUSED(region);
        UNUSED(sub_regions);
      }
    }

    static void gc_destructor(Object* o)
    {
      ((T*)o)->~T();
    }

    void trace(ObjectStack&) {}

  public:
    VBase() : Base() {}

    static Descriptor* desc()
    {
      static Descriptor desc = {
        vsizeof<T>,
        gc_trace,
        has_finaliser<T>::value ? gc_final : nullptr,
        has_notified<T>::value ? gc_notified : nullptr,
        has_destructor<T>::value ? gc_destructor : nullptr};

      return &desc;
    }

    void operator delete(void*)
    {
      // Should not be called directly, present to allow calling if the
      // constructor throws an exception. The object lifetime is managed by the
      // region.
    }

    void operator delete(void*, size_t)
    {
      // Should not be called directly, present to allow calling if the
      // constructor throws an exception. The object lifetime is managed by the
      // region.
    }

    void operator delete(void*, Alloc&)
    {
      // Should not be called directly, present to allow calling if the
      // constructor throws an exception. The object lifetime is managed by the
      // region.
    }

    void operator delete(void*, Object*)
    {
      // Should not be called directly, present to allow calling if the
      // constructor throws an exception. The object lifetime is managed by the
      // region.
    }

    void operator delete(void*, Alloc&, Object*)
    {
      // Should not be called directly, present to allow calling if the
      // constructor throws an exception. The object lifetime is managed by the
      // region.
    }

    void operator delete(void*, RegionType)
    {
      // Should not be called directly, present to allow calling if the
      // constructor throws an exception. The object lifetime is managed by the
      // region.
    }

    void* operator new[](size_t size) = delete;
    void operator delete[](void* p) = delete;
    void operator delete[](void* p, size_t sz) = delete;
  };

  /**
   * Converts a C++ class into a Verona Object
   *
   * Will fill the Verona descriptor with relevant fields.
   */
  template<class T>
  class V : public VBase<T, Object>
  {
  public:
    V() : VBase<T, Object>() {}

    void* operator new(size_t)
    {
      return api::create_object(VBase<T, Object>::desc());
    }

    void* operator new(size_t, RegionType rt)
    {
      return api::create_fresh_region<V>(rt, V::desc());
    }
  };

  /**
   * Converts a C++ class into a Verona Cown
   *
   * Will fill the Verona descriptor with relevant fields.
   */
  template<class T>
  class VCown : public VBase<T, Cown>
  {
  public:
    VCown() : VBase<T, Cown>() {}

    void* operator new(size_t)
    {
      return Object::register_object(
        ThreadAlloc::get().alloc<vsizeof<T>>(), VBase<T, Cown>::desc());
    }

    void* operator new(size_t, Alloc& alloc)
    {
      return Object::register_object(
        alloc.alloc<vsizeof<T>>(), VBase<T, Cown>::desc());
    }

    // This is used by the boxcar bindings to request an object with extra
    // capacity.
    //
    //     new (req_size) ActualCown(conw_args)
    //
    // `conw_args` are used to construct the value in the ActualCown, whereas
    // `req_size` is sent here to control the allocation. It must have enough
    // storage for the object header, the ActualCown, and the rust managed data.
    void* operator new(size_t base_size, size_t req_size)
    {
      assert(req_size >= base_size);

      // This advances the pointer forward from that returned by the allocator,
      // reducing the amount of usable bytes.
      return Object::register_object(
        ThreadAlloc::get().alloc(req_size), VBase<T, Cown>::desc());
    }
  };
} // namespace verona::rt
