#pragma once
#include "batched_connection.hpp"

namespace mutils{
	
	std::size_t send_with_id(::mutils::connection& sock, std::size_t id, std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs);

	constexpr auto total_size(std::size_t how_many, std::size_t const * const sizes){
		std::size_t accum{0};
		for (unsigned int i = 0; i < how_many; ++i){
			accum += sizes[i];
		}
		return accum;
	}

}
