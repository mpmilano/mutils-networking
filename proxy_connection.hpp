#pragma once
#include "Socket.hpp"
#include "ServerSocket.hpp"
#include "batched_connection_common.hpp"
#include "mutils.hpp"
#include "readerwriterqueue.h"
#include "SimpleConcurrentMap.hpp"
#include <list>

namespace mutils{

	namespace proxy_connection{

		template<typename... T>
		using BlockingReaderWriterQueue = moodycamel::BlockingReaderWriterQueue<T...>;
		using queue_t = BlockingReaderWriterQueue<std::vector<char> >;
		using queue_p = std::shared_ptr<queue_t>;
		
		struct connection : public ::mutils::connection {
			const std::size_t id;
			queue_p queue;
			Socket s; //for calling valid() and sending data
			void* bonus_item{nullptr};

			connection(decltype(id) id, decltype(queue) queue, Socket s)
				:id(id),queue(queue),s(s){}
			connection(const connection&) = delete;
			connection(connection&&) = delete;
			
			std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs);
			
			std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes_o, void const * const * const bufs_o);
			
			//The following is just boilerplate to forward
			//templated methods to the superclass
			using ::mutils::connection::receive;
			using ::mutils::connection::send;
			
			template<typename... T> auto receive(T&& ... t){
				::mutils::connection::connection& _this = *this;
				return _this.receive(std::forward<T>(t)...);
			}
			
			template<typename... T> auto send(T&& ... t){
				::mutils::connection::connection& _this = *this;
				return _this.send(std::forward<T>(t)...);
			}

			bool valid() const {
				return s.valid();
			}
			
		};
		
		using SocketPool = ResourcePool<connection>;
		using locked_connection = typename SocketPool::LockedResource;
		using weak_connection = typename SocketPool::WeakResource;
		
		
		struct connections {
			SocketPool sp;
			bool alive{true};
			std::thread receiver_thread;
			connections(const int ip, const int port, const int);
			connections(const connections&) = delete;
			locked_connection spawn();
			
			template<typename d>
			std::vector<std::size_t> abandoned_conections(const d&){return std::vector<std::size_t>();}
			~connections();
		private:
			connections(Socket s, std::shared_ptr<SimpleConcurrentMap<std::size_t,queue_p> >);
		};
		
		struct receiver {
			using action_t =
				std::function<void (const void*, ::mutils::connection&)>;
			
			const int listen;
			
			AcceptConnectionLoop acl;
		
			void loop_until_false_set();
		
			receiver(int port,std::function<action_t () > new_connection);
		};
	}
}
