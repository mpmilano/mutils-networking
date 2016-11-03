#include "Socket.hpp"
#include "mutils.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <thread>
#include <netinet/tcp.h>
								
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
	
	Socket::Socket(int ip, int portno){
		int sockfd;
		struct sockaddr_in serv_addr;
		struct hostent *server;
		
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0){
			std::cerr << ("ERROR opening socket: ") << std::strerror(errno) << std::endl; //*/
			throw SocketException{};
		}
		{ int optval = 1;
			setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&optval,sizeof(int));
		}
		{ int optval = 1;
			setsockopt(sockfd,IPPROTO_TCP,TCP_NODELAY,&optval,sizeof(int));
		}
		if (sockfd < 0){
			std::cerr << ("ERROR opening socket: ") << std::strerror(errno) << std::endl; //*/
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
			std::cerr << "tried to connect to host " << string_of_ip(ip) << " on port " << portno << std::strerror(errno) << std::endl; //*/
			throw SocketException{};
		}
		complete = true;
		this->i.reset(new Internals(sockfd));
	}
	
	Socket Socket::connect(int ip, int portno){
		return Socket{ip,portno};
	}
	
	std::size_t Socket::drain(std::size_t size, void* target){
		auto ret = recv(i->sockID, target,size,0);
		if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)){
			throw Timeout{};
		}
		else return ret;
	}

	namespace {
	std::size_t receive_helper_socket_cpp(int& sockID, std::size_t how_many, std::size_t const * const sizes, void ** bufs, bool peek){
		/*std::cout << "receiving " << how_many << " payloads" << std::endl; //*/
		iovec msgs[how_many];
		std::size_t total_size = 0;
		{
			int i = 0;
			for (auto& vec : msgs) {
				vec.iov_base = bufs[i];
				vec.iov_len = sizes[i];
				total_size += sizes[i];
				++i;
			}
		}
		/*std::cout << "we think this should be " << total_size << " bytes" << std::endl; //*/
		struct msghdr dst{
			nullptr,0,
				msgs,
				how_many,
				nullptr,0,0};
		auto n = recvmsg(sockID,&dst,(peek ? MSG_PEEK : MSG_WAITALL));
		/*std::cout << "we actually received " << n << " bytes" << std::endl; //*/
		if (n < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK){
				throw Timeout{};
			}
			std::stringstream err;
			err << "error: " << std::strerror(errno);
			throw ProtocolException(err.str());
		}
		else if (n == 0){
			std::stringstream err;
			err << "connection broken";
			sockID = -1;
			throw ProtocolException(err.str());
		}
		else if (n < (int) total_size){
			throw ProtocolException("MSG_WAITALL is supposed to do something here, right?");
		}
		return total_size;
	}
	}

	std::size_t Socket::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
		return receive_helper_socket_cpp(i->sockID,how_many,sizes,bufs,false);
	}

	std::size_t Socket::raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs){
		/*std::cout << "sending " << how_many << "payloads" << std::endl; //*/
		iovec iovec_buf[how_many];
		std::size_t total_size{0};
		{
			int i = 0;
			for (auto& vec : iovec_buf){
				vec.iov_base = const_cast<void*>(bufs[i]);
				vec.iov_len = sizes[i];
				total_size += vec.iov_len;
				++i;
			}
		}
		/*std::cout << "we think this should be " << total_size << " bytes" << std::endl; //*/
		struct msghdr payload{
			nullptr,
				0,
				iovec_buf,
				how_many,
				nullptr,
				0,
				0};
		if (valid()){
			auto sent = ::sendmsg(i->sockID,&payload,MSG_NOSIGNAL);
			/*std::cout << "sent " << sent << " bytes in total" << std::endl; //*/
			bool complete = sent == (long) total_size;
			if (!complete) {
				if (sent == -1 && errno == EPIPE) i->sockID = -1;
				else if (sent == -1){
					std::stringstream err;
					err << "tried sending " << total_size << " bytes, achieved " << sent << " accompanying error: " << std::strerror(errno);
					throw ProtocolException(err.str());
				}
			}
		}
		else throw ProtocolException("attempt to send on broken connection!");
		return total_size;
	}

	std::size_t Socket::peek(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
		return receive_helper_socket_cpp(i->sockID,how_many,sizes,bufs,true);
	}
}
