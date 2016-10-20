#include "simple_rpc.hpp"

namespace mutils{

	namespace simple_rpc{

		connection::connection(int ip, int portno):s(ip,portno){}
		std::size_t connection::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs) {
			//std::cout << "sub-process receive expects " << total_size(how_many,sizes) << " bytes" << std::endl;
			return s.raw_receive(how_many,sizes,bufs);
		}
		std::size_t connection::raw_send(std::size_t how_many, std::size_t const * const sizes_o, void const * const * const bufs_o) {
			std::size_t real_size = total_size(how_many,sizes_o);
			std::size_t sizes[how_many + 1];
			sizes[0] = sizeof(std::size_t);
			void* bufs[how_many + 1];
			bufs[0] = &real_size;
			memcpy(sizes + 1,sizes_o,how_many * sizeof(std::size_t));
			memcpy(bufs + 1,bufs_o,how_many * sizeof(void*));
			return s.raw_send(how_many+1,sizes,bufs) - sizes[0];
		}
		
		connections::connections(const int ip, const int port, const int)
			:ip(ip),port(port){}
		
		
		connection connections::spawn(){
			return connection{ip,port};
		}

		//BEGIN RECEIVER CODE
		
		void receiver::loop_until_false_set(){
			acl.loop_until_dead(listen,true);
		}
		
		receiver::receiver(int port,std::function<action_t () > new_connection)
			:listen(port),acl{
			//This function is onReceipt in ServerSocket.cpp
			[new_connection](bool &alive, Socket s) {
				auto processor = new_connection();
				while (alive) {
					std::size_t size{0};
					s.receive(size);
					char buf[size];
					void* buf_p = &buf;
					s.raw_receive(1,&size,&buf_p);
					processor(buf,s);
				}
			}}
            {}
	}
}
