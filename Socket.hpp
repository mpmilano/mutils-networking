#pragma once
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
	
struct Socket {
private:
	struct Internals{
		int sockID;
		Internals(int);
		virtual ~Internals();
	};
		
	std::shared_ptr<Internals> i;
		
public:

	Socket(int sockID);
	Socket(const Socket&) = default;

	static Socket connect(int ip, int port);

	bool valid() const;
	
	void receive(std::size_t how_much, void* where);
	
	template<typename T>
	void receive(T &t){
		static_assert(std::is_pod<T>::value,
					  "Error: can't do non-POD right now");
		static_assert(std::is_trivially_constructible<T>::value,
					  "Error: can't build this with new correctly");
		receive(sizeof(T),&t);
	}

	template<typename T>
	std::unique_ptr<T> receive(){
		static_assert(std::is_pod<T>::value,
					  "Error: can't do non-POD right now");
		static_assert(std::is_trivially_constructible<T>::value,
					  "Error: can't build this with new correctly");
		std::unique_ptr<T> ret{new T()};
		receive(sizeof(T),ret.get());
		return ret;
	}

	
	template<typename T>
	std::unique_ptr<T> receive(DeserializationManager* dsm, int nbytes){
		static_assert(std::is_base_of<ByteRepresentable, T>::value,
					  "Error: can't do non-POD right now");
		char recv[nbytes];
		receive(nbytes,recv);
		return from_bytes<T>(dsm,recv);
	}

	void send(std::size_t how_much, void const * what);
	
	template<typename T>
	void send(const T &t){
		static_assert(std::is_pod<T>::value || std::is_base_of<ByteRepresentable, T>::value,
			"Error: cannot serialize this type.");
		auto size = bytes_size(t);
		char buf[size];
                int tbs = to_bytes(t,buf);
                assert(size == tbs);
		send(size,buf);
	}
};
}
