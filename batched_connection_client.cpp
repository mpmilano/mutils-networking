#include "Socket.hpp"
#include "resource_pool.hpp"
#include "batched_connection.hpp"
#include "batched_connection_common.hpp"
#include <mutex>

using namespace std;

namespace mutils{
	namespace batched_connection {
		
	struct batched_connections_impl {
		static const constexpr std::size_t connection_factor = 8;
		const int ip;
		const int port;
		const std::size_t max_connections;
		const std::size_t modulous = (max_connections > connection_factor ?
									  max_connections / connection_factor :
									  1);
		std::mutex launch_lock;
		std::atomic<std::size_t> current_connections{0};
		SocketPool rp;
		std::vector<std::unique_ptr<SocketBundle> > bundles{modulous};
		
		connection* new_resource(){
			const std::size_t id = current_connections;
			auto index = id % (modulous);
			assert(index < bundles.size());
			auto &bundle = bundles.at(index);
			if (!bundle){
				std::unique_lock<mutex> l{launch_lock};
				if (!bundle){
					bundle.reset(new SocketBundle{Socket::connect(ip,port)});
				}
			}
			++current_connections;
			//don't overflow
			assert(current_connections > id);
			return new connection{*bundle,id};
		}
		batched_connections_impl(const int ip, const int port, const int max_connections)
			:ip(ip),
			 port(port),
			 max_connections(max_connections),
			 rp(modulous,max_connections - modulous,std::bind(&batched_connections_impl::new_resource,this)){
			//std::cout << "beginning pre-init" << std::endl;
			//pre-init all the connections
			std::function<void (const locked_connection&, std::size_t)> init_all;
			init_all = [&](const locked_connection&, std::size_t ind){
				//std::cout << "pre-init " << ind << std::endl;
				if (ind < this->max_connections) {
					init_all(this->rp.acquire(),ind+1);
				}
			};
			init_all(this->rp.acquire(),0);
			//std::cout << "pre-init done" << std::endl;
		}
	};
	const constexpr std::size_t batched_connections_impl::connection_factor;


		connection::connection(SocketBundle &s, std::size_t id)
			:sock(s),id(id){}

		std::size_t connection::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
			return [&](const auto &){
				return receive_with_id(sock.sock,id,how_many,sizes,bufs);
			}(sock.cv.wait([&]{
						std::size_t buf;
						void* buf_p = &buf;
						void** buf_pp = &buf_p;
						static constexpr auto buf_size = sizeof(buf);
						sock.sock.peek(1,&buf_size,buf_pp);
						return buf == id;
					}));
		}
		
		std::size_t connection::raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs){
			//std::cout << "beginning send" << std::endl;
			return [&](const auto &){
				return send_with_id(sock.sock,id,how_many,sizes,bufs);
			}(sock.cv.wait([]{return true;}));
		}

		struct batched_connections::Internals{
			batched_connections_impl _this;
			Internals(const int ip, const int port, const int max_connections)
				:_this(ip,port,max_connections){}
		};
		
		batched_connections::~batched_connections(){
			delete i;
		}
		
		locked_connection batched_connections::spawn(){
			return i->_this.rp.acquire();
		}

		batched_connections::batched_connections(const int ip, const int port, const int max_connections)
			:i(new Internals(ip,port,max_connections)){}
	
	}
}

