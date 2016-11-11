#include "dual_connection.hpp"
#include "batched_connection.hpp"

using namespace mutils;

int main(){
	dual_connection_manager<batched_connection::connections> dcm;
	dual_connection_receiver<batched_connection::receiver> dcr;
}
