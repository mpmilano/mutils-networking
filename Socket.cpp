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

	bool Socket::valid() const {
		return i->sockID > 0;
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
		server = gethostbyaddr(&ip,sizeof(ip),AF_INET);
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

	void Socket::receive(const std::size_t s, void* where){
		int n = read(i->sockID,where,s);
		if (n < 0) {
			std::stringstream err;
			err << "expected " << s << " bytes, received " << n << " accompanying error: " << std::strerror(errno);
			throw ProtocolException(err.str());
		}
		while (n < s) {
			//std::cout << "WARNING: only got " << n << " bytes, expected " << s << " bytes" <<std::endl;
			int k = read(i->sockID,((char*) where) + n,s-n);
			if (k <= 0) {
				std::stringstream err;
				err << "expected " << s << " bytes, received " << n << " accompanying error: " << std::strerror(errno);
				throw ProtocolException(err.str());
			}
			n += k;
		}
	}

	void Socket::send(std::size_t amount, void const * what){
		bool complete = write(i->sockID,what,amount) == amount;
		assert(complete);
	}
}
