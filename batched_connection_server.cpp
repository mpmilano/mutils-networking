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
			:port(port),
			 new_connection(new_connection),
			 acl([this](auto &a, auto b){return this->on_accept(a,b);})
		{
			//std::cout << "receiving on port: " << port << std::endl;
		}

		void receiver::loop_until_false_set(){
			//std::cout << "receiver thread up " << std::endl;
			acl.loop_until_dead(port,true);
		}

		void receiver::on_accept(bool& alive, Socket s){
			//std::cout << "beginning accept loop" << std::endl;
			std::map<std::size_t,  std::unique_ptr<action_items> > receivers;
			std::unique_ptr<ctpl::thread_pool> tp;
			try { 
				while (alive) {
					//std::cout << "looping " << std::endl;
					std::size_t id{0};
					std::size_t size{0};
					s.receive(id);
					s.receive(size);
					assert(size > 0);
					if (receivers.count(id) == 0){
						receivers[id].reset(new action_items(new_connection()));
						if (!tp) tp.reset(new ctpl::thread_pool{1});
						else tp->resize(receivers.size());
					}
					//ready to receive
					constexpr std::size_t max_size = 4096;
					assert(size <= max_size);
					std::array<char, max_size> recv_buf;
					s.receive(size,recv_buf.data());
					//std::cout << "message received" << std::endl;
					auto &p = *receivers.at(id);
					tp->push(
						[recv_buf, &p,
						 conn = connection{s,id},
						 l = std::unique_lock<std::mutex>{p.mut}
							](int) mutable
						{p.action(recv_buf.data(),conn);}
						);
					//std::cout << "action performed" << std::endl;
				}
			}
			catch (const ProtocolException&){
				//we don't really care what this error is,
				//destroy the socket and force everybody to
				//re-open on the client side.
			}
		}
		
		receiver::~receiver(){
			*(acl.alive) = false;
		}
	}
}

