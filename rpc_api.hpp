#pragma once
#include "connection.hpp"

namespace mutils {
	namespace rpc{
		struct ReceiverFun {
			//this *must* be wait-free.  We're calling it in the receive thread!
			virtual void deliver_new_event(const void*) = 0;
			//this *must* be wait-free.  We're calling it in the receive thread!
			virtual void async_tick() = 0;
			//must be able to select() on this int as an FD
			//where a "read" ready indicates it's time to
			//call async_tick
			virtual int underlying_fd() = 0;
			virtual ~ReceiverFun(){}
		};
		
		using action_t = std::unique_ptr<ReceiverFun>;
		
		using new_connection_t = std::function<action_t (whendebug(std::ofstream&,) connection& c ) >;
	}
};
