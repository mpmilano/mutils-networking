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
			const id_type id;
			std::ofstream &log_file;
			connection(Socket s, id_type id, std::ofstream& log_file):s(s),id(id),log_file(log_file){}
			connection(connection&& c) = default;
			std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const buf){
				return send_with_id(log_file,s,id,how_many,sizes,buf,total_size(how_many,sizes));
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
			std::vector<std::unique_ptr<action_items> > receivers;
			id_type socket_id{0};
			s.receive(socket_id);
			ctpl::thread_pool tp{1};
			try { 
				while (alive) {
					//std::cout << "looping " << std::endl;
					id_type id{0};
					size_type size{0};
					s.receive(id);
					s.receive(size);
					assert(size > 0);
					if (receivers.size() <= id){
						receivers.resize(id + 1);
						tp.resize(id+1);
					}
					if (!receivers[id]) {
						receivers[id].reset(new action_items(socket_id, id,new_connection));
					}
					auto &p = *receivers[id];
					auto &log_file = p.log_file;
					//ready to receive
					auto move_on_receive = [&](){
						constexpr size_type max_size = 4096;
						assert(size <= max_size);
						std::array<char, max_size> recv_buf;
						auto size_rcvd = s.receive(size,recv_buf.data());
						log_file << "received " << size_rcvd << "bytes" << std::endl;
						log_file.flush();
						return recv_buf;
					};
					//std::cout << "message received" << std::endl;

					tp.push(
						[recv_buf = move_on_receive(),
						 conn = connection{s,id,log_file},
						 l = std::unique_lock<std::mutex>(p.mut),
						 &p](int) mutable {
							(*p.action)(recv_buf.data(),conn);
						});
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

