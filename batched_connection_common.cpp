#include "batched_connection_common.hpp"

namespace mutils{
	
	std::size_t send_with_id(::mutils::connection& sock, std::size_t id, std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs){
		
		std::size_t size_bufs[how_many + 1];
		size_bufs[0] = sizeof(id);
		memcpy(size_bufs + 1,sizes,how_many * sizeof(std::size_t));
		const void * payload_bufs[how_many + 1];
		payload_bufs[0] = &id;
		memcpy(payload_bufs + 1,bufs,how_many * sizeof(void*));
		
		return sock.raw_send(how_many+1,size_bufs,payload_bufs);
	}
	
	std::size_t receive_with_id(::mutils::connection& sock, std::size_t id, std::size_t how_many, std::size_t const * const sizes, void ** bufs)
	{
		std::size_t id_buf;
		std::size_t size_bufs[how_many + 1];
		size_bufs[0] = sizeof(id_buf);
		memcpy(size_bufs + 1,sizes,how_many * sizeof(std::size_t));
		void* payload_bufs[how_many+1];
		payload_bufs[0] = &id_buf;
		memcpy(payload_bufs + 1,bufs,how_many * sizeof(void*));
		auto ret =  sock.raw_receive(how_many + 1, size_bufs,payload_bufs);
		assert(id_buf == id);
		return ret;
	}
}
