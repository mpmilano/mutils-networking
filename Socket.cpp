#include "Socket.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <thread>
								
namespace mutils{

	Socket::Internals::Internals(int sid):sockID(sid){}
	Socket::Internals::~Internals(){
		if (sockID > 0) close(sockID);
	}
	
	Socket::Socket(int sockID):i(new Internals{sockID}){}
	Socket::Socket(){}
	Socket& Socket::operator=(const Socket &s){
		i = s.i;
		return *this;
	}

	bool Socket::valid() const {
		return i && i->sockID > 0;
	}

	Socket Socket::connect(int ip, int portno){
		int sockfd;
		struct sockaddr_in serv_addr;
		struct hostent *server;
		
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0){
			std::cerr << ("ERROR opening socket") << std::endl;
			throw SocketException{};
		}
		bool complete = false;
		AtScopeEnd ase{[&](){if (!complete) close(sockfd);}};
		server = gethostbyname(string_of_ip(ip).c_str());
		assert(server);
		bzero((char *) &serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		bcopy((char *)server->h_addr,
			  (char *)&serv_addr.sin_addr.s_addr,
			  server->h_length);
		serv_addr.sin_port = htons(portno);
		if (::connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0){
			std::cerr << ("ERROR connecting");
			throw SocketException{};
		}
		complete = true;
		return Socket{sockfd};
	}

	std::size_t Socket::receive(const std::size_t s, void* where){
		assert(i);
		int n = recv(i->sockID,where,s,MSG_WAITALL);
		if (n < 0) {
			std::stringstream err;
			err << "expected " << s << " bytes, received " << n << " accompanying error: " << std::strerror(errno);
			throw ProtocolException(err.str());
		}
		else if (n == 0){
			std::stringstream err;
			err << "expected " << s << " bytes, received " << n << " before connection broken";
			i->sockID = -1;
			throw ProtocolException(err.str());
		}
		while (((std::size_t)n) < s) {
			//std::cout << "WARNING: only got " << n << " bytes, expected " << s << " bytes" <<std::endl;
			int k = recv(i->sockID,((char*) where) + n,s-n,MSG_WAITALL);
			if (k <= 0) {
				std::stringstream err;
				err << "expected " << s << " bytes, received " << n << " accompanying error: " << std::strerror(errno);
				if (k == 0) i->sockID = -1;
				throw ProtocolException(err.str());
			}
			n += k;
		}
		return s;
	}

	std::size_t Socket::send(std::size_t amount, void const * what){
		if (valid()){
			auto sent = ::send(i->sockID,what,amount,MSG_NOSIGNAL);
			bool complete = ((std::size_t)sent) == amount;
			if (!complete) {
				if (sent == -1 && errno == EPIPE) i->sockID = -1;
				else if (sent == -1){
					std::stringstream err;
					err << "tried sending " << amount << " bytes, achieved " << sent << " accompanying error: " << std::strerror(errno);
					throw ProtocolException(err.str());
				}
			}
		}
		else throw ProtocolException("attempt to send on broken connection!");
		return amount;
	}

	std::size_t Socket::peek(std::size_t how_much, void* where){
		auto ret = recv(i->sockID, where, how_much, MSG_PEEK);
		assert(ret > 0);
		assert((std::size_t)ret == how_much);
		return ret;
	}
}
