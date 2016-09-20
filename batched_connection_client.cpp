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
		bool init_done{false};
		
		connection* new_resource(){
			assert(!init_done);
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
			init_done = true;
			//std::cout << "pre-init done" << std::endl;
		}
	};
	const constexpr std::size_t batched_connections_impl::connection_factor;


		connection::connection(SocketBundle &s, std::size_t id)
			:sock(s),id(id),my_queue(s.incoming[id]){}

		void connection::process_data (std::unique_lock<std::mutex> sock_lock, buf_ptr _payload, std::size_t payload_size)
		{
			auto* payload = &_payload;

			using orphan_t = typename SocketBundle::orphan_t;

			if (payload_size < hdr_size){
				assert(!sock.orphans);
				assert(payload_size > 0);
				sock.orphans =
					std::unique_ptr<orphan_t>
					{new orphan_t(std::move(*payload),payload_size)};
			}
			else {
				const std::size_t &id = ((std::size_t*)payload->payload)[0];
				const std::size_t &size = ((std::size_t*)payload->payload)[1];
				std::cout << "Received message of size: " << size << std::endl;
				if (payload_size < size + hdr_size){
					std::cout << payload_size << std::endl;
					assert(!sock.orphans);
					sock.orphans =
						std::unique_ptr<orphan_t>
						{new orphan_t(std::move(*payload),payload_size)};
					assert(payload_size > 0);
				}
				else {
					{
						//payload is at least a full message; queue up that message and keep going.
						auto &queue = sock.incoming.at(id);
						std::unique_lock<std::mutex> l{queue.queue_lock};
						queue.queue.emplace_back(payload->split(hdr_size));
						payload = &queue.queue.back();
					}
					if (payload_size > (size + hdr_size)){
						process_data(std::move(sock_lock), payload->split(size),
									 payload_size - size - hdr_size);
					}
					else {
						sock.spare = payload->split(size);
					}
				}
			}
		}

		void connection::from_network (std::unique_lock<std::mutex> l,
									   std::size_t expected_size, buf_ptr from,
									   std::size_t offset)
		{
			auto into = from.grow_to_fit(expected_size + offset);
                        assert(into.size() > offset);
			auto recv_size = sock.sock.drain(into.size() - offset,
											 into.payload + offset);
			process_data(std::move(l),std::move(into),recv_size + offset);
		};

		std::size_t connection::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
			const auto expected_size = total_size(how_many, sizes);
			while (true){
				if (my_queue.queue.size() > 0){
					std::unique_lock<std::mutex> l{my_queue.queue_lock};
					auto msg = std::move(my_queue.queue.front());
					my_queue.queue.pop_front();
					std::cout << "popping message of expected size: " << expected_size << std::endl;
					assert(msg.size() == expected_size);
					copy_into(how_many,sizes,bufs,(char*)msg.payload);
					return expected_size;
				}
				else {
					std::unique_lock<std::mutex> l{sock.socket_lock};
					assert(sock.spare.payload || sock.orphans);
					const auto real_expected = expected_size + hdr_size;
					if (sock.orphans){
						auto orphan = std::move(sock.orphans); //nulls sock.orphans
						const auto remaining_size = (orphan->size >= real_expected ?
													 orphan->size :
													 real_expected - orphan->size);
						from_network(std::move(l),
									 remaining_size,
									 std::move(orphan->buf),orphan->size);
					}
					else {
						assert(sock.spare.payload);
						from_network(std::move(l),
									 real_expected,
									 std::move(sock.spare),0);
					}
				}
			}
		}
		
		std::size_t connection::raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs){
			return send_with_id(sock.sock,id,how_many,sizes,bufs,total_size(how_many, sizes));
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

