#pragma once
#include "../mutils-serialization/SerializationSupport.hpp"
#include "connection.hpp"

namespace mutils{

/*	
	//Note: T is the type of the vector, not the vector's type parameter T
	template<typename T>
	std::enable_if_t<is_vector<T>::value,std::unique_ptr<T> > from_bytes(DeserializationManager* ctx, char const * v){
		using member = typename T::value_type;
#ifndef NDEBUG
		auto otherstr = from_bytes<std::string>(ctx,v);
		if (*otherstr != type_name<T>()) {
			std::cout << *otherstr << std::endl;
			std::cout << std::endl;
			std::cout << type_name<T>() << std::endl;
		}
		assert(*otherstr == type_name<T>());
		v += bytes_size(*otherstr);
#endif
		if (std::is_same<bool,member>::value){
			return boolvec_from_bytes<T>(ctx,v);
		}
		else if (std::is_pod<member>::value && !std::is_same<bool,member>::value){
			member const * const start = (member*) (v + sizeof(int));
			const int size = ((int*)v)[0];
			return std::unique_ptr<T>{new T{start, start + size}};
		}
		else{
			int size = ((int*)v)[0];
			auto* v2 = v + sizeof(int);
			int per_item_size = -1;
			std::unique_ptr<std::vector<member> > accum{new T()};
			for(int i = 0; i < size; ++i){
				std::unique_ptr<member> item = from_bytes<member>(ctx,v2 + (i * per_item_size));
				if (per_item_size == -1)
					per_item_size = bytes_size(*item);
				accum->push_back(*item);
			}
			return accum;
		}
	}
*/

	template<typename T>
	std::enable_if_t<std::is_pod<T>::value,std::unique_ptr<T> > receive_from_connection(DeserializationManager*, mutils::connection &c){
		auto ret = std::make_unique<T>();
		c.receive(*ret);
		return ret;
	}

	template<typename T>
	std::enable_if_t<is_vector<T>::value,std::unique_ptr<T> > receive_from_connection(DeserializationManager* dsm, mutils::connection &c){
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
