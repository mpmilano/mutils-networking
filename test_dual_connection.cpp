#include "dual_connection.hpp"
#include "batched_connection.hpp"

using namespace mutils;

int main(){
	dual_connection_manager<batched_connection::connections> dcm(0,0,50);
	dual_connection_receiver<batched_connection::receiver> dcr(
		0,new_dualstate_t{});
}
