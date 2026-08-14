// bc_allocator.h - WillEngine patch: settable allocator hook for all heap use in the encode path.
#pragma once
#include <cstddef>
#include <cassert>
#include <vector>

namespace bc_alloc
{
	typedef void* (*alloc_fn)(size_t size);
	typedef void (*free_fn)(void* p);

	extern alloc_fn g_alloc;
	extern free_fn g_free;

	void bc7enc_set_allocator(alloc_fn a, free_fn f);

	template <typename T>
	struct allocator
	{
		typedef T value_type;

		allocator() = default;

		template <typename U>
		allocator(const allocator<U>&) {}

		T* allocate(size_t n)
		{
			assert(g_alloc && "bc7enc_set_allocator not called");
			return static_cast<T*>(g_alloc(n * sizeof(T)));
		}

		void deallocate(T* p, size_t)
		{
			g_free(p);
		}
	};

	template <typename T, typename U>
	bool operator==(const allocator<T>&, const allocator<U>&) { return true; }

	template <typename T, typename U>
	bool operator!=(const allocator<T>&, const allocator<U>&) { return false; }

	template <typename T>
	using vector = std::vector<T, allocator<T>>;
}
