#pragma once
#include "Socket.hpp"
#include "ServerSocket.hpp"
#include "rpc_api.hpp"

namespace mutils{

	namespace simple_rpc{
		struct connection : public ::mutils::connection {
			Socket s;
			whendebug(std::ofstream log);
			void* bonus_item{nullptr};
			connection(int ip, int portno);
			
			std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs);
			
			std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes_o, void const * const * const bufs_o);

#ifndef NDEBUG
			std::ostream& get_log_file();
#endif
			
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
		
		struct connections {
			const int ip;
			const int port;
			connections(const int ip, const int port, const int);
			connections(const connections&) = delete;
			connection spawn();
			template<typename d>
			std::vector<std::size_t> abandoned_conections(const d&){return std::vector<std::size_t>();}
		};
		
		struct receiver {
			using action_t = rpc::action_t;
			
			const int listen;
			
			AcceptConnectionLoop acl;
		
			void loop_until_false_set();
			void acceptor_fun();
		
			receiver(int port,rpc::new_connection_t new_connection);
		};
	}
}
