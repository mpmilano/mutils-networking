#include "dual_connection.hpp"
#include "batched_connection.hpp"
#include "unistd.h"

using namespace mutils;

int main(){
	dual_connection_manager<batched_connection::connections> dcm(
		decode_ip("127.0.0.1"),9876,50);
	try{
		auto _c = dcm.spawn();
		connection &c = _c;
		int never_to_come{0};
		c.send(never_to_come);
		c.receive(never_to_come);
		assert(never_to_come != 42);
	}
	catch(ControlChannel &_c){
		connection &c = _c;
		int it_worked = 0;
		c.receive(it_worked);
		assert(it_worked == 42);
	}

	sleep(40000);
}
