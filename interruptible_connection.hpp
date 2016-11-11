#pragma once

#include "connection.hpp"

namespace mutils{
	
	struct ReadInterruptedException : public std::exception {
		const char* what() const noexcept {
			return "connection client read interrupted";
		}
	};
	
	struct interruptible_connection : public connection {

		virtual void interrupt() = 0;
		virtual void clear_interrupt() = 0;
		
		//The following is just boilerplate to forward
		//templated methods to the superclass
		using ::mutils::connection::receive;
		using ::mutils::connection::send;
		using ::mutils::connection::valid;
		
		template<typename... T> auto receive(T&& ... t){
			using this_t = ::mutils::connection;
			this_t& _this = *this;
			return _this.receive(std::forward<T>(t)...);
		}
		
		template<typename... T> auto send(T&& ... t){
			::mutils::connection& _this = *this;
			return _this.send(std::forward<T>(t)...);
		}
	};
}
