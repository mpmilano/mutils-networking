#include "Socket.hpp"
#include "resource_pool.hpp"
#include "batched_connection.hpp"
#include "batched_connection_common.hpp"
#include "ctpl_stl.h"
#include <mutex>

namespace mutils{
	namespace batched_connection {
		
		struct receiver::connection: public ::mutils::connection {
			Socket s;
			const std::size_t id;
			connection(Socket s, size_t id):s(s),id(id){}
			connection(connection&& c):s(c.s),id(c.id){}
			std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const buf){
				return send_with_id(s,id,how_many,sizes,buf,total_size(how_many,sizes));
			}
			bool valid() const {return true;}
			std::size_t raw_receive(std::size_t, std::size_t const * const, void **) {assert(false);}
			connection(const connection&) = delete;
		};
		
		receiver::receiver(int port, decltype(new_connection) new_connection)
			:receiver((std::make_index_sequence<thread_count>*) nullptr,
					  port, new_connection){}

		template<std::size_t... indices>
		receiver::receiver(std::index_sequence<indices...> const * const,
						   int port, decltype(new_connection) new_connection)
			:new_connection(new_connection),
			 port(port),
			 active_sockets{(indices ? *this : *this)...}
		{
			//std::cout << "receiving on port: " << port << std::endl;
		}
		
		void receiver::receiver_state::tick(){
			using namespace std::chrono;
			std::size_t id{0};
			std::size_t size{0};
			s.receive(id);
			try {
				s.receive(size);
				assert(size > 0);
				if (receivers.count(id) == 0){
					receivers[id].reset(new action_items(super.new_connection()));
				}
				//ready to receive
				constexpr std::size_t max_size = 4096;
				assert(size <= max_size);
				std::array<char, max_size> recv_buf;
				s.receive(size,recv_buf.data());
				//std::cout << "message received" << std::endl;
				auto &p = *receivers.at(id);
				connection conn{s,id};
				(*p.action)(recv_buf.data(),conn);
			}
			catch(const Timeout&){
				//this is a pretty bad error, since it indicates a hangup on the client
				assert(false && "bad bad timeout detected!");
			}
		}
		receiver::receiver_state::receiver_state(Socket s, receiver& super)
			:s(s),super(super){}

		receiver::receiver_state::receiver_state(receiver_state&& o)
			:receivers(std::move(o.receivers)),
			 s(std::move(o.s)),
			 super(o.super){}

		receiver::receiver_state&
		receiver::receiver_state::operator=(receiver_state&& o){
			assert(&super == &o.super);
			s = std::move(o.s);
			receivers = std::move(o.receivers);
			return *this;
		}

		void receiver::receiver_thread::tick_one(){
			std::unique_ptr<receiver_state> state;
			while (active_sockets.try_dequeue(state)){
				assert(state);
				epoll_obj.add(std::move(*state));
			}
			auto ready_sockets = epoll_obj.wait();
			for (auto &state : ready_sockets){
				try{
					state.tick();
					epoll_obj.return_ownership(std::move(state));
				}
				catch (const Timeout&){
					//timed out on receive, try again later.
					//we know this timeout is on the first receive call,
					//so we're not going to leave the socket in an inconsistent state
					epoll_obj.return_ownership(std::move(state));
				}
				catch( const ProtocolException&){
					//this socket is dead, let the resource get cleaned up.
				}
			}
		}

		void receiver::acceptor_fun(){
			using namespace std::chrono;
			ServerSocket ss{port};
			unsigned char last_used_list{0};
			while (alive){
				auto next = (last_used_list + 1)%thread_count;
				active_sockets[next].active_sockets.enqueue(
					std::make_unique<receiver_state>(
						ss.receive().set_timeout(5ms),*this));
				last_used_list = next;
			}
		}
		
		receiver::~receiver(){
			alive = false;
			for (int i = 0; i < thread_count; ++i){
				active_sockets[i].this_thread.join();
			}
		}
	}
}

