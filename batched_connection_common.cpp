#include "batched_connection_common.hpp"

namespace mutils{
	namespace batched_connection{
	
		std::size_t send_with_id(std::ofstream& log_file, ::mutils::connection& sock, id_type id, std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs, size_type total_size){
			
			std::size_t size_bufs[how_many + 2];
			size_bufs[0] = sizeof(id);
			size_bufs[1] = sizeof(total_size);
			memcpy(size_bufs + 2,sizes,how_many * sizeof(std::size_t));
			const void * payload_bufs[how_many + 2];
			payload_bufs[0] = &id;
			payload_bufs[1] = &total_size;
			memcpy(payload_bufs + 2,bufs,how_many * sizeof(void*));
			auto sent = sock.raw_send(how_many+2,size_bufs,payload_bufs) -
				(sizeof(id) + sizeof(total_size));
			log_file << "sent " << sent << " bytes" << std::endl;
			log_file.flush();
			return sent;
		}
	}
}
