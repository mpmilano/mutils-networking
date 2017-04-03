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

		virtual ~ProtocolException(){}
	};

	struct BrokenSocketException : public ProtocolException {
		BrokenSocketException(std::string why):ProtocolException(why){}
		virtual ~BrokenSocketException(){}
	};

	struct Timeout : public std::exception{
		const char* what() const noexcept{
			return "timeout on recv";
		}
	};
	
	struct Socket : public connection {
	private:
		const int sockID{0};
		bool is_valid{false};
		whendebug(std::string why_not_valid);
		
	public:
		
		Socket(int sockID);
		Socket() = default;
		Socket(Socket&&);
		Socket(const Socket&) = delete;

		Socket(int ip, int port);
		static Socket connect(int ip, int port);
		
		bool valid() const;
		auto underlying_fd() const { return sockID;}
		
		std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs);

		static Socket set_timeout(Socket s, std::chrono::microseconds time);
		
		template<typename duration>
		static Socket set_timeout(Socket s, duration _time){
			using namespace std::chrono;
			microseconds time = duration_cast<microseconds>(_time);
			return set_timeout(std::move(s),time);
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

#ifndef NDEBUG
		std::ostream& get_log_file(){assert(false && "no logs on raw sockets, sorry");}
#endif
		
		std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs);
		std::size_t peek(std::size_t how_many, std::size_t const * const sizes, void ** bufs);
		virtual ~Socket();
		
	};
}
