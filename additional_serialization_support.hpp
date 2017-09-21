#pragma once
#include "../mutils-serialization/SerializationSupport.hpp"
#include "connection.hpp"

namespace mutils{


	template<typename T, typename... ctxs>
	std::enable_if_t<std::is_pod<T>::value,std::unique_ptr<T> > receive_from_connection(DeserializationManager<ctxs...>*, mutils::connection &c){
		auto ret = std::make_unique<T>();
		c.receive(*ret);
		return ret;
	}

	template<typename T, typename... ctxs>
	std::enable_if_t<is_vector<T>::value,std::unique_ptr<T> > receive_from_connection(DeserializationManager<ctxs...>* dsm, mutils::connection &c){
#ifndef NDEBUG
		const static std::string typenonce = type_name<T>();
		const auto typenonce_size = bytes_size(typenonce);
		char remote_string[typenonce_size];
		bzero(remote_string,typenonce_size);
		c.receive_data(typenonce_size,remote_string);
		if (typenonce != remote_string){
			std::cout << typenonce << std::endl << std::endl;
			std::cout << remote_string << std::endl;
		}
		assert(typenonce == remote_string);
#endif
		using member = typename T::value_type;
		static_assert(!std::is_same<bool,member>::value);
		int _size{-1};
		c.receive(_size);
		assert(_size > -1);
		std::size_t size = (std::size_t) _size;
		if (std::is_pod<member>::value && !std::is_same<bool,member>::value){
			std::unique_ptr<T> ret{new T()};
			if (size > 0){
				ret->resize(size);
				c.receive_data(size,ret->data());
			}
			return ret;
		}
		else{
			std::unique_ptr<std::vector<member> > accum{new T()};
			for(std::size_t i = 0; i < size; ++i){
				std::unique_ptr<member> item = receive_from_connection<member>(dsm,c);
				accum->push_back(*item);
			}
			return accum;
		}
		
	}

}
