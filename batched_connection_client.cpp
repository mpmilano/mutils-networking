#include "Socket.hpp"
#include "resource_pool.hpp"
#include "batched_connection.hpp"
#include "batched_connection_common.hpp"
#include <mutex>

using namespace std;
using namespace chrono;

namespace mutils{
	namespace batched_connection {

	struct batched_connections_impl {
		const int ip;
		const int port;
		const std::size_t max_connections;
		const std::size_t modulus = (max_connections > connection_factor ?
									  max_connections / connection_factor :
									  1);
		std::atomic<std::size_t> current_connection_id{0};
		std::vector<std::unique_ptr<SocketBundle> > bundles{modulus};

		batched_connections_impl(const int ip, const int port, const int max_connections)
			:ip(ip),
			 port(port),
			 max_connections(max_connections){
			for (auto &bundle : bundles){
				bundle.reset(new SocketBundle{Socket::connect(ip,port).set_timeout(10s)});
			}
		}
	};


		connection::connection(SocketBundle &s, std::size_t id)
			:sock(s),id(id),my_queue(s.incoming[id]){}

		void connection::process_data (std::unique_lock<std::mutex> sock_lock, buf_ptr _payload, std::size_t payload_size)
		{
			static auto const max_vector_size =
				std::numeric_limits<unsigned char>::max()
				+ (sizeof(std::size_t)*3);
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
				assert(size <= max_vector_size);
				if (payload_size < size + hdr_size) {
					assert(!sock.orphans);
					sock.orphans =
						std::unique_ptr<orphan_t>
						{new orphan_t(std::move(*payload),payload_size)};
					assert(payload_size > 0);
				}
				else {
					//payload is at least a full message; queue up that message and keep going.
					auto &queue = sock.incoming.at(id);
					std::unique_lock<std::mutex> l{queue.queue_lock};
					//this is the right client.  give it the part after the header.
					queue.queue.emplace_back(payload->split(hdr_size));
					payload = &queue.queue.back();
					auto leftover = payload->split(size);
					l.unlock();
					if (payload_size > size + hdr_size){
						process_data(std::move(sock_lock), std::move(leftover),
									 payload_size - size - hdr_size);
					}
					else {
						sock.spare = std::move(leftover);
					}
				}
			}
		}

		namespace {
			struct ResourceReturn{
				std::unique_lock<std::mutex> l;
				buf_ptr from;
			};
		}

		void connection::from_network (std::unique_lock<std::mutex> l,
									   std::size_t expected_size, buf_ptr from,
									   std::size_t offset)
		{
			auto into = from.grow_to_fit(expected_size + offset);
			auto into_size = into.size();
			assert(into_size > offset);
			std::size_t recv_size{0};
			try {
				recv_size = sock.sock.drain(into.size() - offset,
											into.payload + offset);
				assert(into_size >= recv_size);
			}
			catch (const Timeout&){
				throw ResourceReturn{std::move(l), std::move(into)};
			}
			process_data(std::move(l),std::move(into),recv_size + offset);
		};

		std::size_t connection::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
			const auto expected_size = total_size(how_many, sizes);
			while (true){
				if (my_queue.queue.size() > 0){
					std::unique_lock<std::mutex> l{my_queue.queue_lock};
					auto msg = std::move(my_queue.queue.front());
					my_queue.queue.pop_front();
					if (msg.size() != expected_size){
						std::cerr << msg.size() << std::endl;
						std::cerr << expected_size << std::endl;
					}
					assert(msg.size() == expected_size);
					copy_into(how_many,sizes,bufs,(char*)msg.payload);
					return expected_size;
				}
				else {
					std::unique_lock<std::mutex> l{sock.socket_lock};
					//retest condition - someone might have found us a message!
					//this will implicitly drop the lock.
					if (my_queue.queue.size() > 0) continue;
					assert(sock.spare.payload || sock.orphans);
					const auto real_expected = expected_size + hdr_size;
					if (sock.orphans){
						auto orphan = std::move(sock.orphans); //nulls sock.orphans
						const auto remaining_size = (orphan->size >= real_expected ?
													 orphan->size :
													 real_expected - orphan->size);
						try {
							from_network(std::move(l),
										 remaining_size,
										 std::move(orphan->buf),orphan->size);
						}
						catch(ResourceReturn &rr){
							//we still have the lock; it's in rr
							orphan->buf = std::move(rr.from);
							sock.orphans = std::move(orphan);
						}
					}
					else {
						assert(sock.spare.payload);
						try {
							from_network(std::move(l),
										 real_expected,
										 std::move(sock.spare),0);
						}
						catch(ResourceReturn &rr){
							//we still have the lock; it's in rr
							sock.spare = std::move(rr.from);
						}
					}
				}
			}
		}
		
		std::size_t connection::raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs){
			return send_with_id(sock.sock,id,how_many,sizes,bufs,total_size(how_many, sizes));
		}

		struct connections::Internals{
			batched_connections_impl _this;
			Internals(const int ip, const int port, const int max_connections)
				:_this(ip,port,max_connections){}
		};
		
		connections::~connections(){
			delete i;
		}
		
		connection connections::spawn(){
			auto &_i = i->_this;
			auto my_id = ++_i.current_connection_id;
			return connection{*_i.bundles.at(my_id% _i.modulus),my_id};
		}

		connections::connections(const int ip, const int port, const int max_connections)
			:i(new Internals(ip,port,max_connections)){}
	
	}
}

