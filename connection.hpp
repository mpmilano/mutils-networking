#pragma once
#include <memory>
#include "SerializationSupport.hpp"

namespace mutils{

//interface.
struct connection{
	virtual bool valid() const = 0;
	virtual	std::size_t receive(std::size_t how_much, void* where) = 0;
	virtual std::size_t send(std::size_t how_much, void const * const) = 0;
	
	template<typename T>
	void receive(T &t){
		static_assert(std::is_pod<T>::value,
					  "Error: can't do non-POD right now");
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

	template<typename T>
	void send(const T &t){
		static_assert(std::is_pod<T>::value || std::is_base_of<ByteRepresentable, T>::value,
			"Error: cannot serialize this type.");
		auto size = bytes_size(t);
		char buf[size];
		auto tbs = to_bytes(t,buf);
		assert(size == tbs);
		send(size,buf);
	}
	
};
}
