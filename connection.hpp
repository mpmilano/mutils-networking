#pragma once
#include <memory>
#include <fstream>
#include "mutils-serialization/SerializationSupport.hpp"
#include "mutils/extras"

namespace mutils{

//interface.
struct connection {
	virtual bool valid() const = 0;
	virtual std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs) = 0;
	virtual std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const) = 0;

#ifndef NDEBUG
	virtual std::ostream& get_log_file() = 0;
#endif
	template<typename... T>
	void receive(T&... t){
		static_assert(forall_nt((std::is_standard_layout<T>::value && std::is_trivial<T>::value)...),
					  "Error: can't do non-POD right now");
		/*std::cout << "yup you called this one" << std::endl; //*/
		void* recv[] = {&t...};
		std::size_t size_buf[] = {sizeof(T)...};
		/*std::cout << "first element in size buf is: " << size_buf[0] << std::endl; //*/
		raw_receive(sizeof...(T),size_buf,recv);
	}

	template<typename... T>
	auto receive_helper(T*... t){
		receive(*t...);
		return std::make_tuple(std::unique_ptr<T>(t)...);
	}

	template<typename... T>
	auto receive(){
		static_assert(forall_nt((std::is_standard_layout<T>::value && std::is_trivial<T>::value)...),
					  "Error: can't do non-POD right now");
		static_assert(forall_nt(std::is_trivially_constructible<T>::value...),
					  "Error: can't build this with new correctly");
		return receive_helper(new T()...);
	}

	template<typename... T>
	std::tuple<T...> receive_tpl(std::tuple<T...>*){
		return receive<T...>();
	}
	
	template<typename T>
	void receive_tpl(){
		T *t{nullptr};
		return receive_tpl(t);
	}

	template<typename> using make_these_ints = int;

	template<typename Fst, typename... ctx>
	auto receive_helper(DeserializationManager<ctx...>* dsm, void** recv, int indx = 0){
		return std::make_tuple(from_bytes<Fst>(dsm,(char*)recv[indx]));
	}

	template<typename Fst, typename Snd, typename DSM, typename... Rst>
	auto receive_helper(DSM* dsm, void** recv, int indx = 0){
		return std::tuple_cat(
			std::make_tuple(from_bytes<Fst>(dsm,(char*)recv[indx])),
			receive_helper<Snd,Rst...>(dsm,recv,indx+1));
	}
	
	template<typename T1, typename DSM, typename... T2>
	auto receive(DSM* dsm,
				 int size,
				 make_these_ints<T2>... sizes){
		static_assert(std::is_base_of<ByteRepresentable, T1>::value &&
					  forall_nt(std::is_base_of<ByteRepresentable, T2>::value...),
					  "Error: can't do non-POD right now");
		
		void* recv[] = {alloca(size),alloca(sizes)...};
		std::size_t size_buf[] = {size,sizes...};
		raw_receive(sizeof...(T2) + 1 ,size_buf,recv);
		return receive_helper<T1,T2...>(dsm,recv);
	}
	template<typename T, typename... ctxs>
	auto receive(DeserializationManager<ctxs...>* dsm, std::size_t size){
		void* recv[] = {alloca(size)};
		void** _recv = recv;
		std::size_t size_buf[] = {size};
		raw_receive(1 ,size_buf,_recv);
		return from_bytes<T>(dsm,(char*) recv[0]);
	}

	template<typename T>
	void* to_bytes_helper(const T &t, void* v){
		to_bytes(t,(char*)v);
		return v;
	}

	template<typename... T>
	auto send(const T&... t){
		std::size_t sizes[] = {bytes_size(t)...};
		void *bufs[] = {to_bytes_helper(t,alloca(bytes_size(t)))...};
		return raw_send(sizeof...(T),sizes,bufs);
	}

	auto receive_data(std::size_t size, void *data){
		std::size_t sizes[] = {size};
		void* bufs[] = {data};
		return raw_receive(1,sizes,bufs);
	}

	auto receive(std::size_t size, void *data){
		return receive_data(size,data);
	}

	auto send_data (std::size_t size, void const * const data){
		std::size_t sizes[] = {size};
		const void* bufs[] = {data};
		return raw_send(1,sizes,bufs);
	}
	
	auto send (std::size_t size, void const * const data){
		return send_data(size,data);
	}
	
};

	//useful utilities for wrapping connections

#define MUTILS_CONNECTION_PREPEND_EXTRA_BODY												\
	static_assert((std::is_standard_layout<T>::value && std::is_trivial<T>::value), "Error: POD only for now" ); \
	using namespace std;																							\
	auto new_how_many = old_how_many+1;																\
	std::size_t new_sizes[new_how_many];															\
	memcpy(new_sizes+1, old_sizes,old_how_many);											\
	new_sizes[0] = sizeof(T);																					\
	memcpy(new_bufs+1, old_bufs,old_how_many);												\
	new_bufs[0] = &extra
	
	template<typename conn, typename T> std::size_t receive_prepend_extra(conn& c, std::size_t old_how_many, std::size_t const * const old_sizes, void ** old_bufs, T& extra){
		void* new_bufs[old_how_many+1];
		MUTILS_CONNECTION_PREPEND_EXTRA_BODY;
		return c.raw_receive(new_how_many,new_sizes, new_bufs) - sizeof(T);
	}
	
	template<typename conn, typename T> std::size_t send_prepend_extra(conn& c, std::size_t old_how_many, std::size_t const * const old_sizes, void const * const * const old_bufs, const T& extra){
		const void* new_bufs[old_how_many+1];
		MUTILS_CONNECTION_PREPEND_EXTRA_BODY;
		return c.raw_send(new_how_many,new_sizes, new_bufs) - sizeof(T);
	}
	
}
