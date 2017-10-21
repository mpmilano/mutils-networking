#include "additional_serialization_support.hpp"
#include "local_connection.hpp"
#include "../tracker/Tombstone.hpp"

using namespace mutils;
using namespace myria;


int main(){
	local_connection conn;
	DeserializationManager dsm{{}};
	std::vector<tracker::Tombstone> test_vec1;
	std::vector<char> test_vec2{{2,3,5,43,5,43,21,12,34,12,35,2,31,3,2}};
	conn.send(test_vec1,test_vec2,test_vec2);
	auto rcv1 = *mutils::receive_from_connection<DECT(test_vec1)>(&dsm,conn);
	auto rcv2 = *mutils::receive_from_connection<DECT(test_vec2)>(&dsm,conn);
	auto rcv3 = *mutils::from_bytes<DECT(test_vec2)>(&dsm,conn.raw_buf());
	assert(test_vec1 == rcv1);
	if (test_vec1 != rcv1) throw 3;
	assert(test_vec2 == rcv2);
	if (test_vec2 != rcv2) throw 2;
	assert(test_vec2 == rcv3 );
	if (test_vec2 != rcv3) throw 5;
}
