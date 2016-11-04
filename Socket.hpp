#pragma once
#include "connection.hpp"
#include "SerializationSupport.hpp"
#include "Socket.hpp"
#include "mutils.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <thread>
#include <netinet/tcp.h>

namespace mutils{
	
	struct SocketException{};
	struct ProtocolException : public std::exception{
		const std::string why;
		ProtocolException(std::string why):why(why){}
		const char* what() const noexcept{
			return why.c_str();
		}
	};

	struct Timeout : public std::exception{
		const char* what() const noexcept{
			return "timeout on recv";
		}
	};
	
	struct Socket : public connection {
	private:
		struct Internals{
			int sockID;
			Internals(int);
			virtual ~Internals();
		};
		
		std::shared_ptr<Internals> i;
		
	public:
		
		Socket(int sockID);
		Socket();
		Socket(const Socket&) = default;
		Socket& operator=(const Socket&);

		Socket(int ip, int port);
		static Socket connect(int ip, int port);
		
		bool valid() const;
		auto underlying_fd() const { return i->sockID;}
		
		std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs);
		template<typename duration>
		Socket set_timeout(duration _time){
			using namespace std::chrono;
			microseconds time = duration_cast<microseconds>(_time);
			seconds _seconds = duration_cast<seconds>(time);
			microseconds _microseconds = time - _seconds;
			assert(_seconds + _microseconds == _time);
			struct timeval tv{_seconds.count(),_microseconds.count()};
			auto success = setsockopt(i->sockID, SOL_SOCKET,SO_RCVTIMEO, (char*) &tv, sizeof(tv));
			assert(success == 0);
			return *this;
		}
		std::size_t drain(std::size_t buf_size, void* target);

		using connection::receive;
		using connection::send;

		template<typename... T> auto receive(T&& ... t){
			connection& _this = *this;
			return _this.receive(std::forward<T>(t)...);
		}

		template<typename... T> auto send(T&& ... t){
			connection& _this = *this;
			return _this.send(std::forward<T>(t)...);
		}
		
		std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs);
		std::size_t peek(std::size_t how_many, std::size_t const * const sizes, void ** bufs);
		virtual ~Socket(){}
		
	};
}
