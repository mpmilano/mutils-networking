#include "Socket.hpp"
#include "resource_pool.hpp"
#include "batched_connection.hpp"
#include <mutex>

namespace mutils{
	namespace batched_connection {
		
		struct receiver::connection: public ::mutils::connection {
			Socket s;
			const std::size_t id;
			connection(Socket s, size_t id):s(s),id(id){}
			std::size_t send(std::size_t expected,
							 void const * const buf){
				auto size1 = s.send(sizeof(id),&id);
				assert(size1 == sizeof(id));
				auto size2 = s.send(expected,buf);
				assert(size2 == expected);
				return expected;
			}
			bool valid() const {return true;}
			std::size_t receive(std::size_t, void*) {assert(false);}
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
								std::unique_lock<std::mutex> l{p.mut};
								char msg[p.next_expected_size];
								s.receive(p.next_expected_size,msg);
								//std::cout << "message received" << std::endl;
								p.next_expected_size = p.action(msg,conn);
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

