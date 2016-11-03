#pragma once
#include "batched_connection.hpp"

namespace mutils{

	namespace batched_connection {
	
		std::size_t send_with_id(::mutils::connection& sock, id_type id, std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs, size_type total_size);

	}
}
