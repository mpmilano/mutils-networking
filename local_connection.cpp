#include "mutils.hpp"
#include "connection.hpp"
#include "local_connection.hpp"

namespace mutils{
	bool local_connection::valid() const {return true;}
	std::size_t local_connection::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs) {
		whendebug(auto data_size = data.size() - pos;)
		auto data_p = data.data() + pos;
		auto size = total_size(how_many,sizes);
		assert(size <= data_size);
		copy_into(how_many, sizes, bufs, data_p);
		pos += size;
		return size;
	}
	std::size_t local_connection::raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs){
		std::size_t ret{0};
		for (std::size_t i = 0; i < how_many; ++i){
			ret += sizes[i];
			data.insert(data.end(),(char*)bufs[i], ((char*) (bufs[i]))+sizes[i]);
		}
		return ret;
	}

	char* local_connection::raw_buf(){
		return data.data() + pos;
	}
	void local_connection::mark_used(std::size_t s){
		pos += s;
	}
#ifndef NDEBUG
	std::ostream& local_connection::get_log_file(){
		return ss;
	}
#endif
		
}

