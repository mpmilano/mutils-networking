#pragma once
#include "connection.hpp"
#include "epoll.hpp"
#include "rpc_api.hpp"
#include "../mutils-containers/SimpleConcurrentMap.hpp"

namespace mutils{
	//connection has two modes; Control-mode and data-mode.
	//will throw an exception when transfer from control-mode to data-mode is detected.
	//we don't transmit sizes, so you need to make sure you've destroyed the 
	namespace dual_connection{

		//throw this when we get a message on the control channel
		struct ControlChannel : public std::exception, public connection {
		//this socket is ready for reading and contains the message
			dual_connection& parent;
			const char first_byte;
			bool consumed_first_byte{false};
			
			ControlChannel(dual_connection& parent, const char first_byte);
			
			std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs);
			
			std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs);
			
			bool valid () const ;
			
			const char* what() const noexcept;
			
			~ControlChannel();
		};


		struct connection : public ::mutils::connection {

			std::unique_ptr<::mutils::connection> internal_conn;
			connection(decltype(internal_conn) ic):internal_conn(std::move(ic)){}
			
			std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
				static_assert(sizeof(bool) == 1,"");
				bool is_data{false};
				std::size_t size = 1;
				void* buf = &is_data;
				auto recvd = internal_conn->raw_receive(1,&size,&buf);
				if (is_data){
					
				}
				else throw ControlChannel{*this};
			}
			
			std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes_o, void const * const * const bufs_o){
				static_assert(sizeof(bool) == 1,"");
				constexpr const bool is_data = true;
				const std::size_t how_many = _how_many + 1;
				std::size_t sizes[how_many];
				sizes[0] = 1;
				memcpy(sizes + 1, sizes,sizeof(size_t)*_how_many);
				void* bufs[how_many];
				bufs[0] = &is_data;
				return internal_conn->raw_send(how_many,sizes,bufs);
			}
			
			//The following is just boilerplate to forward
			//templated methods to the superclass
			using ::mutils::connection::receive;
			using ::mutils::connection::send;
			
			template<typename... T> auto receive(T&& ... t){
				using this_t = ::mutils::connection;
				this_t& _this = *this;
				return _this.receive(std::forward<T>(t)...);
			}
			
			template<typename... T> auto send(T&& ... t){
				::mutils::connection& _this = *this;
				return _this.send(std::forward<T>(t)...);
			}
			
			bool valid() const {
				return s.valid();
			}
		
		};
		
		template<typename ConnectionManager>
		struct connections {

			ConnectionManager cm;
			using subconnection = std::decay_t<decltype(cm.spawn())>;
			using supconnection = ::mutils::connection;
			
			template<typename... Args>
			connections(Args && ... args)
				:cm(std::forward<Args>(args)...){}
			
			connections(const connections&) = delete;
			
			connection spawn(){
				return connection{std::unique_ptr<supconnection>(new subconnection(cm.spawn()))};
			}
		};
		
		struct receiver {
			using action_t = std::unique_ptr<ReceiverFun>;
			
			const int listen;
			
			AcceptConnectionLoop acl;
			
			void loop_until_false_set();
			
			receiver(int port,std::function<action_t () > new_connection);
		};
	}
}
