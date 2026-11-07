// bevy port: aligned_allocator.h comes from krkrz and is not in the
// Kirikiroid2 repository. ResampleImage.cpp uses it as
// std::vector<T, aligned_allocator<T, Align>>, Align in bytes.
#pragma once
#include <cstddef>
#include <cstdlib>
#include <new>

template <typename T, std::size_t Align = 16>
class aligned_allocator {
    static constexpr std::size_t kAlign = Align < alignof(void *) ? alignof(void *) : Align;

public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    template <typename U> struct rebind { using other = aligned_allocator<U, Align>; };

    aligned_allocator() noexcept = default;
    template <typename U> aligned_allocator(const aligned_allocator<U, Align> &) noexcept {}

    T *allocate(size_type n) {
        if (n == 0) return nullptr;
        void *p = nullptr;
        if (posix_memalign(&p, kAlign, n * sizeof(T)) != 0) throw std::bad_alloc();
        return static_cast<T *>(p);
    }
    void deallocate(T *p, size_type) noexcept { std::free(p); }

    template <typename U> bool operator==(const aligned_allocator<U, Align> &) const noexcept { return true; }
    template <typename U> bool operator!=(const aligned_allocator<U, Align> &) const noexcept { return false; }
};
