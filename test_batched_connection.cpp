#include "batched_connection.hpp"

using namespace mutils;

int main(){
	const int msg = 42;
	using action_t = typename batched_connection::receiver::action_t;
	batched_connection::receiver r{9876,[msg]{
			return std::pair<action_t,std::size_t>{
				[msg](char* inbnd, connection&) -> std::size_t{
					int rcv = *((int*)(inbnd));
					assert(msg == rcv);
					return sizeof(int);
				}, sizeof(int)};
		}};
	batched_connection::batched_connections bc(decode_ip("127.0.0.1"),9876,1);
	auto l = bc.spawn();
	connection& c = *l;
	c.send(msg);
}
