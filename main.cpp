#include <iostream>
#include <span>
#include <vector>

template <typename T>
T* Align(T* ptr, size_t alignment) {
  auto ptr_int = reinterpret_cast<uintptr_t>(ptr);
  auto byte_ptr = reinterpret_cast<const volatile std::byte*>(ptr);
  byte_ptr += ((ptr_int + alignment - 1) & ~(alignment - 1)) - ptr_int;
  auto cvt_ptr = reinterpret_cast<const volatile T*>(byte_ptr);
  return const_cast<T*>(cvt_ptr);
}


struct NoopResource {
  void* allocate(size_t, size_t) {
    throw std::bad_alloc();
  }

  void deallocate(void*, size_t) {
    std::abort();
  }
};


struct NewDeleteResource {
  void* allocate(size_t sz, size_t) {
    return new std::byte[sz];
  }

  void deallocate(void* ptr, size_t) {
    delete[] reinterpret_cast<std::byte*>(ptr);
  }
};

template<typename FalbackResourceT>
struct MonotonicResource {
  MonotonicResource(std::span<std::byte> storage, FalbackResourceT fallback)
    : cur_head(storage.data()), storage(storage), fallback(fallback) {}

  void* allocate(size_t sz, size_t alginment) {
    if (std::byte* res = Align(cur_head, alginment)) {
      if (res + sz - storage.data() > storage.size()) {
        return fallback.allocate(sz, alginment);
      }
      cur_head = res + sz;
      return res;
    }
  }

  void deallocate(void* ptr, size_t sz) {
    if (storage.data() <= ptr && ptr < cur_head) {
      // NOOP
    } else {
      fallback.deallocate(ptr, sz);
    }
  }


  std::byte* cur_head;
  std::span<std::byte> storage;
  FalbackResourceT fallback;
};

struct AbstractMemoryResource {
  virtual void* allocate(size_t sz, size_t alginment) = 0;
  virtual void deallocate(void* ptr, size_t sz) = 0;
};

struct PolymorphicMemoryResource {
  void* allocate(size_t sz, size_t alginment) {
    return res->allocate(sz, alginment);
  }

  void deallocate(void* ptr, size_t sz) {
    res->deallocate(ptr, sz);
  }
  AbstractMemoryResource* res;
};

template<typename T>
PolymorphicMemoryResource MakePolymorphicMemoryResource(T resource) {
  struct ConcreteMemoryResource : AbstractMemoryResource {
    void* allocate(size_t sz, size_t alginment) override {
      return res->allocate(sz, alginment);
    }

    void deallocate(void* ptr, size_t sz) override {
      res->deallocate(ptr, sz);
    }
    std::remove_pointer_t<T>* res;
  };
  if constexpr (std::is_pointer_v<T>) {
    return PolymorphicMemoryResource{ new ConcreteMemoryResource{resource}};
  } else {
    return PolymorphicMemoryResource{ new ConcreteMemoryResource{new auto(resource)}};
  }
}

template<typename ValueT, typename ResourceT>
struct ResourceBackedAlocator {
  explicit ResourceBackedAlocator(ResourceT resource) : resource(resource) {}

  ResourceT resource;

  using value_type = ValueT;

  value_type* allocate(size_t sz) {
    return resource.allocate(sz, alignof(ValueT));
  }

  void deallocate(value_type* ptr, size_t sz) {
    resource.deallocate(ptr, sz);
  }
};



int main() {
  auto resource = MonotonicResource<NewDeleteResource>(std::span(new std::byte[100], 100), {});
  auto alloc = ResourceBackedAlocator<int, decltype(resource)>{resource};
  std::vector<int, decltype(alloc)> vec(alloc);

  auto poly_alloc = ResourceBackedAlocator<int, PolymorphicMemoryResource>(MakePolymorphicMemoryResource(&resource));
  std::vector<int, ResourceBackedAlocator<int, PolymorphicMemoryResource>> poly_vec(poly_alloc);

}
