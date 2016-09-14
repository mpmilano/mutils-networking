#include "Socket.hpp"
#include "resource_pool.hpp"
#include "batched_connection.hpp"
#include "batched_connection_common.hpp"
#include <mutex>

namespace mutils{
	namespace batched_connection {
		
		struct receiver::connection: public ::mutils::connection {
			Socket s;
			const std::size_t id;
			connection(Socket s, size_t id):s(s),id(id){}
			std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const buf){
				return send_with_id(s,id,how_many,sizes,buf);
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
			while (alive) {
				if (alive) {
					//std::cout << "looping " << std::endl;
					std::size_t id;
					s.receive(id);
					connection conn{s,id};
					bool need_new_entry = false;
					//std::cout << "received a message on connection " << id << std::endl;
					for (bool i = true; i || need_new_entry; i = false){
						while (need_new_entry){
							//std::cout << "generating new entry" << std::endl;
							need_new_entry = false;
							std::unique_lock<std::shared_mutex> l{map_lock};
							auto rcv = new_connection();
							auto &elem = receivers[id];
							elem.action = rcv.first;
							elem.next_expected_size = rcv.second;
						}
						{
							//std::cout << "locking for receipt" << std::endl;
							std::shared_lock<std::shared_mutex> l{map_lock};
							if (receivers.count(id) > 0) {
								//std::cout << "receiver ready, receiving message" << std::endl;
								auto &p = receivers.at(id);
								//std::cout << " message sizes: ";
								//for (auto &s : p.next_expected_size)
									//std::cout << s << " ";
								//std::cout << std::endl;
								std::unique_lock<std::mutex> l{p.mut};
								void* bufs[p.next_expected_size.size()];
								for (std::size_t i = 0; i < p.next_expected_size.size(); ++i){
									bufs[i] = alloca(p.next_expected_size.at(i));
								}
								s.raw_receive(p.next_expected_size.size(),
										  p.next_expected_size.data(),
										  bufs);
								//std::cout << "message received" << std::endl;
								p.next_expected_size = p.action(bufs,conn);
								//std::cout << "action performed" << std::endl;
								break;
							}
							else {
								need_new_entry = true;
							}
						}
					}
				}
				else break;
			}
		}
		
		receiver::~receiver(){
			*acl.alive = false;
		}
	}
}

