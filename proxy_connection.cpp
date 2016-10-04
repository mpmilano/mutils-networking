#include "proxy_connection.hpp"
#include "GlobalPool.hpp"

namespace mutils{
	
	namespace proxy_connection{

		using namespace std::chrono;

			namespace {
				auto send_with_id2(Socket &s, std::size_t id, std::size_t how_many, std::size_t const * const sizes_o, void const * const * const bufs_o){
					std::size_t real_size = total_size(how_many,sizes_o);
					std::size_t sizes[how_many + 2];
					sizes[0] = sizeof(std::size_t);
					sizes[1] = sizeof(std::size_t);
					void* bufs[how_many + 2];
					bufs[0] = &id;
					bufs[1] = &real_size;
					memcpy(sizes + 2,sizes_o,how_many * sizeof(std::size_t));
					memcpy(bufs + 2,bufs_o,how_many * sizeof(void*));
					return s.raw_send(how_many+2,sizes,bufs);				
				}
			auto retry_receive_unless_false(const bool& alive, Socket& s, std::size_t how_many, std::size_t const * const sizes, void ** bufs){
				while (alive){
					try{
						return s.raw_receive(how_many,sizes,bufs);
					}
					catch(const Timeout&){
						//just keep trying unless we're not alive!
					}
				}
				return std::size_t{0};
			}

			struct receive_bundle{
				std::size_t id{0};
				std::size_t size{0};
				std::vector<char> payload_buf;
			};

			auto receive_message(const bool& alive, Socket& s){
				receive_bundle rcv;
				std::size_t sizes[] = {sizeof(rcv.id),sizeof(rcv.size)};
				void* bufs[] = {&rcv.id,&rcv.size};
				auto size1 =
					retry_receive_unless_false(alive,s,2,sizes,bufs);
				assert(size1 == sizeof(rcv.id) + sizeof(rcv.size));
				rcv.payload_buf.resize(rcv.size,0);
				void* data = rcv.payload_buf.data();
				auto size2 = retry_receive_unless_false
					(alive,s,1,&rcv.size,&data);
				assert(size2 == rcv.size);

				return rcv;
			}
		}

		std::size_t connection::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs) {
			std::vector<char> payload;
			queue->wait_dequeue(payload);
			assert(payload.size() > 0);
			copy_into(how_many,sizes,bufs,payload.data());
			return payload.size();
		}
		std::size_t connection::raw_send(std::size_t how_many, std::size_t const * const sizes_o, void const * const * const bufs_o) {
			return send_with_id2(s,id,how_many,sizes_o,bufs_o);
		}

		connections::connections(Socket s, std::shared_ptr<SimpleConcurrentMap<std::size_t,queue_p> > qmap)
			:sp(5000,5000,[qmap,s]{
					const auto id = gensym();
					(*qmap)[id].reset(new queue_t());
					return new connection{id,qmap->at(id),s};
				}),
			 receiver_thread([s,qmap,alive = &this->alive]{
					 //copy, because s is const
					 Socket s2 = s;
					 s2.set_timeout(5s);
					 while (*alive){
						 auto rcv = receive_message(*alive,s2);
						 qmap->at(rcv.id)->enqueue(std::move(rcv.payload_buf));
					 }
				 })
		{}
		connections::~connections(){
			receiver_thread.join();
		}
		
		connections::connections(const int ip, const int port, const int)
			:connections(Socket::connect(ip,port), std::make_shared<SimpleConcurrentMap<std::size_t,queue_p> >()){}
		
		
		locked_connection connections::spawn(){
			return sp.acquire();
		}

		//BEGIN RECEIVER CODE
		
		void receiver::loop_until_false_set(){
			acl.loop_until_dead(listen,true);
		}
		
		receiver::receiver(int port,std::function<action_t () > new_connection)
			:listen(port),acl{
			//This function is onReceipt in ServerSocket.cpp
			[new_connection](bool &alive, Socket s) {
				SimpleConcurrentMap<std::size_t, action_t> action_map;
				while (alive) {
					GlobalPool::push([&s,&action_map,
									  &new_connection,
									  rcv = receive_message(alive,s)](int){
							if (action_map.count(rcv.id) == 0){
								action_map[rcv.id] = new_connection();
							}
							else {
								struct connection : ::mutils::connection {
									Socket &s;
									const std::size_t id;
									connection(decltype(s) s, decltype(id) id)
										:s(s),id(id){}
									
									bool valid() const {return s.valid();}
									std::size_t raw_receive(std::size_t, std::size_t const * const, void **) {assert(false);}
									std::size_t raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const buf){
										return send_with_id2(s,id,how_many,sizes,buf);
									}
								};
								connection c{s,rcv.id};
								const void* v = rcv.payload_buf.data();
								action_map.at(rcv.id)(v,c);
							}
									 });
				}
			}}
            {}
	}
}
