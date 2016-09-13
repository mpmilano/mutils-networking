#pragma once
#include <memory>
#include "SerializationSupport.hpp"

namespace mutils{

//interface.
struct connection{
	virtual bool valid() const = 0;
	virtual std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs) = 0;
	virtual std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const) = 0;

	
	
	template<typename... T>
	void receive(T&... t){
		static_assert(forall(std::is_pod<T>::value...),
					  "Error: can't do non-POD right now");
		void* recv[] = {&t...};
		std::size_t size_buf[] = {sizeof(T)...};
		raw_receive(sizeof...(T),size_buf,recv);
	}

	template<typename... T>
	auto receive_helper(T*... t){
		receive(*t...);
		return std::make_tuple(std::unique_ptr<T>(t)...);
	}

	template<typename... T>
	auto receive(){
		static_assert(forall(std::is_pod<T>::value...),
					  "Error: can't do non-POD right now");
		static_assert(forall(std::is_trivially_constructible<T>::value...),
					  "Error: can't build this with new correctly");
		return receive_helper(new T()...);
	}

	template<typename> using make_these_ints = int;

	template<typename Fst, typename... Rst>
	auto receive_helper(DeserializationManager* dsm, void** recv, int indx = 0){
		return std::tuple_cat(
			std::make_tuple(from_bytes<Fst>(dsm,(char*)recv[indx])),
			receive_helper<Rst...>(dsm,recv,indx+1));
	}
	
	template<typename T1, typename... T2>
	auto receive(DeserializationManager* dsm,
				 int size,
				 make_these_ints<T2>... sizes){
		static_assert(std::is_base_of<ByteRepresentable, T1>::value &&
					  forall(std::is_base_of<ByteRepresentable, T2>::value...),
					  "Error: can't do non-POD right now");
		
		void* recv[] = {alloca(size),alloca(sizes)...};
		std::size_t size_buf[] = {size,sizes...};
		raw_receive(sizeof...(T2) + 1 ,size_buf,recv);
		return receive_helper<T1,T2...>(dsm,recv);
	}
	template<typename T>
	auto receive(DeserializationManager* dsm, std::size_t size){
		void* recv[] = {alloca(size)};
		void** _recv = recv;
		std::size_t size_buf[] = {size};
		raw_receive(1 ,size_buf,_recv);
		return std::get<0>(receive_helper<T>(dsm,recv));
	}

	template<typename T>
	void* to_bytes_helper(const T &t, void* v){
		to_bytes(t,(char*)v);
		return v;
	}

	template<typename... T>
	void send(const T&... t){
		std::size_t sizes[] = {bytes_size(t)...};
		void *bufs[] = {to_bytes_helper(t,alloca(bytes_size(t)))...};
		static_assert(forall((std::is_pod<T>::value || std::is_base_of<ByteRepresentable, T>::value)...),
			"Error: cannot serialize these types.");
		raw_send(sizeof...(T),sizes,bufs);
	}
	
};
}
