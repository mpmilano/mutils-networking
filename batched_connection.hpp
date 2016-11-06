#pragma once
#include "Socket.hpp"
#include <mutex>
#include <fstream>
#include <sstream>
#include "abortable_lock.hpp"
#include <atomic>
#include "ServerSocket.hpp"
#include "buffer_generator.hpp"

namespace mutils {
	namespace batched_connection {

		struct batched_connections_impl;
		using id_type = unsigned short;
		using size_type = unsigned short;
		using BufGen = BufferGenerator<4096*8*8>;
		using buf_ptr = BufGen::pointer;
		static const constexpr std::size_t hdr_size = sizeof(id_type) + sizeof(size_type);

		/** Number of logical connections per physical connection. */
		static const constexpr unsigned short connection_factor = 8;
		
		struct incoming_message_queue{
			std::list<buf_ptr> queue;
			std::mutex queue_lock;
		};

		using locked_socket_t = std::unique_lock<typename abortable_locked_guardian::abortable_mutex>;

		/** Maintains the message queues for all logical connections
		 *  associated with a physical TCP socket
		 */
		struct SocketBundle {
			Socket sock;
			const id_type socket_id = gensym();
			std::atomic<id_type> unused_id{0};
			std::vector<std::unique_ptr<incoming_message_queue> > incoming{connection_factor*2};
			abortable_locked_guardian socket_lock;
			
			//represents a partial message whose
			//destination is not cubrrently known
			//(we don't know if we've only gotten a prefix of the id)
			struct orphan_t{
				buf_ptr buf;
				size_type size;
				
				orphan_t(buf_ptr b, size_type s)
					:buf(std::move(b)),size(s){}
			};

			//if there was an orphan on the last read, it is here.
			std::unique_ptr<orphan_t> orphans{nullptr};

			//this is some leftover free space;
			//treat it like a freshly allocated buffer.
			//invariant: not in use, of non-zero size.
			buf_ptr spare{BufGen::allocate()};
			
			SocketBundle(Socket _sock)
				:sock(std::move(_sock)){
				sock.send(socket_id);
			}
			SocketBundle(const SocketBundle&) = delete;
			SocketBundle(SocketBundle&&) = delete;
		};
		
		struct connection : public ::mutils::connection {
			SocketBundle& sock;
			
			const id_type id;
#ifndef NDEBUG
			std::ofstream log_file{[&](){
					std::stringstream ss;
					ss << "/tmp/event_log_client"
					   << sock.socket_id
					   << "-"
					   << id;
					return ss.str(); }()};
#endif
			incoming_message_queue &my_queue;

			connection(SocketBundle& s, id_type id);
			operator bool() const {return valid();}
			connection(const connection&) = delete;
			connection(connection&&) = default;
		private:
			locked_socket_t process_data (locked_socket_t sock_lock, buf_ptr _payload, size_type payload_size);
			locked_socket_t from_network (locked_socket_t l,
										  size_type expected_size, buf_ptr from,
										  size_type offset);
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
		
		/** Manages a set of logical TCP connections over a smaller set of physical TCP
		 *  connections.
		 */
		struct connections {
			struct Internals;
			Internals *i;
			
			//connect to a server at address IP and port PORT, and open max_connections logical connections.
			connections(const int ip, const int port, const int max_connections);
			
			connections(const connections&) = delete;
			
			connection spawn();
			~connections();
		};

		//Manages a physical TCP socket (to accept on) on behalf
		//of its clients.  Is intended to be an RPC mechanism;
		//the user passes in a "new_connection" function,
		//which must return action functions.  On receipt of a message,
		//the action funciton is called.  There is a unique action function
		//(produced by a separate call to new_connection) per logical connection.

		struct ReceiverFun {
			virtual void operator()(const void*, ::mutils::connection&) = 0;
			virtual ~ReceiverFun(){}
		};
		
		struct receiver {
			
			//returns expected next message size
			struct connection;
			// physical TCP port
			const int port;
			//function to call when new messages come in.
			using action_t = std::unique_ptr<ReceiverFun>;

			using new_connection_t = std::function<action_t (whendebug(std::ofstream&)) >;

			//Represents  a logical connection; consider it an
			//"instance" of the action that this receiver is set up to
			//perform.  
			struct action_items {
				const id_type socket_id;
				const id_type id;
#ifndef NDEBUG
				std::ofstream log_file{[&](){
						std::stringstream ss;
						ss << "/tmp/event_log_server"
						   << socket_id
						   << "-"
						   << id;
						return ss.str(); }()};
#endif
				action_t action;
				std::mutex mut;
				action_items() = default;
				action_items(const id_type sid, const id_type id, new_connection_t& nc)
					:socket_id(sid),id(id),action(nc(whendebug(log_file))){}
			};
			
			new_connection_t new_connection;

			AcceptConnectionLoop acl;

			void on_accept(bool& alive, Socket s);

			void loop_until_false_set();

			receiver(int port, new_connection_t new_connection);
			~receiver();
		};
	}
}
