#include "batched_connection_common.hpp"

namespace mutils{
	
	std::size_t send_with_id(::mutils::connection& sock, std::size_t id, std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs, std::size_t total_size){
		
		std::size_t size_bufs[how_many + 2];
		size_bufs[0] = sizeof(id);
		size_bufs[1] = sizeof(total_size);
		memcpy(size_bufs + 2,sizes,how_many * sizeof(std::size_t));
		const void * payload_bufs[how_many + 2];
		payload_bufs[0] = &id;
		payload_bufs[1] = &total_size;
		memcpy(payload_bufs + 2,bufs,how_many * sizeof(void*));
		
		return sock.raw_send(how_many+2,size_bufs,payload_bufs) -
			(sizeof(id) + sizeof(total_size));
	}
	

}
