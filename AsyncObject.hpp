#pragma once

namespace mutils{
	struct AsyncObject {
		using always_block_code = std::integral_constant<int, -37>;
		//this *must* be wait-free.  
		virtual void async_tick() = 0;
		//must be able to select() on this int as an FD
		//where a "read" ready indicates it's time to
		//call async_tick
		virtual int underlying_fd() = 0;
		virtual ~AsyncObject() = default;
	};
}
