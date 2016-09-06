#include "Socket.hpp"
#include "resource_pool.hpp"
#include "batched_connection.hpp"
#include <mutex>

using namespace std;

namespace mutils{
	namespace batched_connection {
		
	struct batched_connections_impl {
		static const constexpr std::size_t connection_factor = 8;
		const int ip;
		const int port;
		const std::size_t max_connections;
		std::mutex launch_lock;
		std::size_t current_connections{0};
		SocketPool rp;
		std::vector<SocketBundle> bundles{
			max_connections / connection_factor};
		
		connection* new_resource(){
			assert(this);
			const auto id = current_connections;
			auto index = id / connection_factor;
			auto &bundle = bundles[index];
			std::unique_lock<mutex> l{launch_lock};
			if (!bundle.sock.valid()){
				bundle.sock = Socket::connect(ip,port);
			}
			++current_connections;
			//don't overflow
			assert(current_connections > id);
			return new connection{bundle,id};
		}
		batched_connections_impl(const int ip, const int port, const int max_connections)
			:ip(ip),
			 port(port),
			 max_connections(max_connections),
			 rp(max_connections - (max_connections/connection_factor),max_connections/connection_factor,std::bind(&batched_connections_impl::new_resource,this)){
			//pre-init all the connections
			std::function<void (const locked_connection&, int)> init_all;
			init_all = [&](const locked_connection&, int ind){
				if (ind < this->max_connections) {
					init_all(this->rp.acquire(),ind+1);
				}
			};
			init_all(this->rp.acquire(),0);
		}
	};
	const constexpr std::size_t batched_connections_impl::connection_factor;


		connection::connection(SocketBundle &s, std::size_t id)
			:sock(s),id(id){}

		std::size_t connection::receive(std::size_t expected, void * _parent_buf){
			char * parent_buf = (char*) _parent_buf;
			return [&](const auto &){
				{
					std::size_t buf;
					sock.sock.receive(buf);
					assert(buf == id);
				}
				bool worked = expected ==
					sock.sock.receive(expected,parent_buf);
				assert(worked);
				return expected;
			}(sock.cv.wait([&]{
						std::size_t buf;
						sock.sock.peek(sizeof(buf),&buf);
						return buf == id;
					}));
		}
		
		std::size_t connection::send(std::size_t expected, void const * const _buf){
			char const * const buf = (char*) _buf;
			return [&](const auto &){
				sock.sock.send(sizeof(id),&id);
				bool worked = expected ==
					sock.sock.send(expected,buf);
				assert(worked);
				return expected;
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

