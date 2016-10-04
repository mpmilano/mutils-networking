#pragma once
#include "Socket.hpp"
#include "resource_pool.hpp"
#include "better_cv.hpp"
#include <shared_mutex>
#include "ServerSocket.hpp"
#include "buffer_generator.hpp"

namespace mutils {
	namespace batched_connection {

		struct batched_connections_impl;
		using BufGen = BufferGenerator<4096*8*8>;
		using buf_ptr = struct BufGen::pointer;
		static const constexpr std::size_t hdr_size = 2*sizeof(std::size_t);
		
		struct incoming_message_queue{
			std::list<buf_ptr> queue;
			std::mutex queue_lock;
			std::atomic<std::chrono::high_resolution_clock::time_point> last_used
				{std::chrono::high_resolution_clock::now()};
			std::size_t receive_count{0};
		};

		/** Maintains the message queues for all logical connections
		 *  associated with a physical TCP socket
		 */
		struct SocketBundle {
			Socket sock;
			std::map<std::size_t,incoming_message_queue> incoming;
			std::mutex socket_lock;
			//std::atomic<std::size_t> use_count{0};
			std::atomic<std::size_t> &bytes_received;

			//represents a partial message whose
			//destination is not currently known
			//(we don't know if we've only gotten a prefix of the id)
			struct orphan_t{
				buf_ptr buf;
				std::size_t size;
				
				orphan_t(buf_ptr b, std::size_t s)
					:buf(std::move(b)),size(s){}
			};

			//if there was an orphan on the last read, it is here.
			std::unique_ptr<orphan_t> orphans{nullptr};

			//this is some leftover free space;
			//treat it like a freshly allocated buffer.
			//invariant: not in use, of non-zero size.
			buf_ptr spare{BufGen::allocate()};
			
			SocketBundle(Socket sock, std::atomic<std::size_t> &b)
				:sock(sock),bytes_received(b){}
			SocketBundle(const SocketBundle&) = delete;
			SocketBundle(SocketBundle&&) = delete;
		};
		
		struct connection : public ::mutils::connection {
			SocketBundle& sock;

			/*
			void onAcquire(...){
				++sock.use_count;
				{ 
					my_queue = &sock.incoming[id];
				}
			}

			void onRelease(){
				--sock.use_count;
				{
					assert(my_queue->queue.size() == 0);
					sock.incoming.erase(id);
				}
			}
			*/
			
			const std::size_t id;
			void* bonus_item{nullptr};
			incoming_message_queue* my_queue{nullptr};
			//use this if there's nothing left over.
			connection(SocketBundle& s, std::size_t id);
			operator bool() const {return valid();}
			connection(const connection&) = delete;
			connection(connection&&) = default;
		private:
			void process_data (std::unique_lock<std::mutex> sock_lock, buf_ptr _payload, std::size_t payload_size);
			void from_network (std::unique_lock<std::mutex> l,
							   std::size_t expected_size, buf_ptr from,
							   std::size_t offset);
		public:
			std::size_t raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs);
			std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const);
			bool valid () const {return sock.sock.valid();}
			
			template<typename... T> auto receive(T&& ... t){
				::mutils::connection& _this = *this;
				return _this.receive(std::forward<T>(t)...);
			}
			
			template<typename... T> auto send(T&& ... t){
				::mutils::connection& _this = *this;
				return _this.send(std::forward<T>(t)...);
			}
		};

		using SocketPool = ResourcePool<connection>;
		using locked_connection = typename SocketPool::LockedResource;
		using weak_connection = typename SocketPool::WeakResource;
		
		/** Number of logical connections per physical connection. */
		static const constexpr std::size_t connection_factor = 8;
		
		/** Manages a set of logical TCP connections over a smaller set of physical TCP
		 *  connections.
		 */
		struct connections {
			struct Internals;
			Internals *i;
			
			//connect to a server at address IP and port PORT, and open max_connections logical connections.
			connections(const int ip, const int port, const int max_connections);
			
			connections(const connections&) = delete;

			std::vector<std::size_t> abandoned_conections(const std::chrono::microseconds& d, bool = false);
			
			template<typename duration>
			std::vector<std::size_t> abandoned_conections(const duration& d){
				return abandoned_conections(std::chrono::duration_cast<std::chrono::microseconds>(d),false);
			}
			
			locked_connection spawn();
			~connections();
		};

		//Manages a physical TCP socket (to accept on) on behalf
		//of its clients.  Is intended to be an RPC mechanism;
		//the user passes in a "new_connection" function,
		//which must return action functions.  On receipt of a message,
		//the action funciton is called.  There is a unique action function
		//(produced by a separate call to new_connection) per logical connection.
		struct receiver {

			std::atomic<std::size_t> bytes_sent{0};
			
			//returns expected next message size
			struct connection;
			// physical TCP port
			const int port;
			//function to call when new messages come in.
			using action_t =
				std::function<void (void*, ::mutils::connection&)>;

			//Represents  a logical connection; consider it an
			//"instance" of the action that this receiver is set up to
			//perform.  
			struct action_items {
				action_t action;
				std::unique_ptr<std::mutex> mut{new std::mutex()};
				std::size_t bytes_sent{0};
				action_items() = default;
				action_items(action_t a)
					:action(std::move(a)){}
				action_items(action_items&& o)
					:action(std::move(o.action)),
					 mut(std::move(o.mut)){}
			};

			using new_connection_t = std::function<action_t () >;
			
			new_connection_t new_connection;

			std::shared_mutex map_lock;
			std::map<std::size_t,  action_items> receivers;
			AcceptConnectionLoop acl;

			void on_accept(bool& alive, Socket s);

			void loop_until_false_set();

			receiver(int port, new_connection_t new_connection);
			~receiver();
		};
	}
}
