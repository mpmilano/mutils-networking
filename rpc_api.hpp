#pragma once
#include "connection.hpp"
#include "AsyncObject.hpp"

namespace mutils {
	namespace rpc{
		struct ReceiverFun : public AsyncObject{
			//this *must* be wait-free.  We're calling it in the receive thread!
			virtual void deliver_new_event(std::size_t size, const void*) = 0;
			virtual ~ReceiverFun() = default;
		};
		
		using action_t = std::unique_ptr<ReceiverFun>;
		
		using new_connection_t = std::function<action_t (whendebug(std::ostream&,) connection& c ) >;
	}
};
