#pragma once
#include "connection.hpp"
#include "SerializationSupport.hpp"

namespace mutils{
	
	struct SocketException{};
	struct ProtocolException : public std::exception{
		const std::string why;
		ProtocolException(std::string why):why(why){}
		const char* what() const noexcept{
			return why.c_str();
		}
	};
	
	struct Socket : public connection{
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
		
		static Socket connect(int ip, int port);
		
		bool valid() const;
		
		std::size_t receive(std::size_t how_much, void* where);

		template<typename... T> auto receive(T&& ... t){
			connection& _this = *this;
			return _this.receive(std::forward<T>(t)...);
		}

		template<typename... T> auto send(T&& ... t){
			connection& _this = *this;
			return _this.send(std::forward<T>(t)...);
		}
		
		std::size_t send(std::size_t how_much, void const * what);

		std::size_t peek(std::size_t how_much, void* where);
		
	};
}
