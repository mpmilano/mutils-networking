#pragma once
#include "batched_connection.hpp"

namespace mutils{
	
	std::size_t send_with_id(::mutils::connection& sock, std::size_t id, std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs);
	
	std::size_t receive_with_id(::mutils::connection& sock, std::size_t id, std::size_t how_many, std::size_t const * const sizes, void ** bufs);

}
