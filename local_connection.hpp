#pragma once
#include "connection.hpp"
#include <sstream>

namespace mutils{
	struct local_connection : public connection {
		std::vector<char> data;
		std::size_t pos{0};
#ifndef NDEBUG
	  std::ostream& out;
	  local_connection(std::ostream& o):out(o){}
#endif
		bool valid() const;
		std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs);
		std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs);
		char* raw_buf();
		void mark_used(std::size_t);
		whendebug(std::ostream& get_log_file());
#ifndef NDEBUG
		void dump_bytes();
#endif
	};
}
