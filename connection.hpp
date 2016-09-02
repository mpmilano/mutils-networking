#pragma once

namespace mutils{

//interface.
class connection{
	virtual	const char* recv() = 0;
	virtual std::size_t send(char const * const) = 0;
};
}
