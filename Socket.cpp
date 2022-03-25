#include "Socket.hpp"
#include "mutils/mutils.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <thread>
#include <netinet/tcp.h>
#include <cstring>
								
namespace mutils{

	Socket::~Socket(){
		if (is_valid && sockID > 0) {
			close(sockID);
			is_valid=false;
			whendebug(why_not_valid="socket closed in destructor");
		}
	}
	
	Socket::Socket(int sockID):
		sockID(sockID),
		is_valid(sockID > 0){
			assert(is_valid || why_not_valid != "no reason set for invalidity of socket!");
		}

	bool Socket::valid() const {
		assert(is_valid || why_not_valid != "no reason set for invalidity of socket!");
		return is_valid && sockID > 0;
	}

	namespace {
		int connect_socket(int ip, int portno){
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
			AtScopeEnd ase{[&](){
					if (!complete){
						close(sockfd);
						sockfd = 0;
					}
				}};
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
			return sockfd;
		}
	}
	
	Socket::Socket(int ip, int portno)
		:sockID(connect_socket(ip,portno)),
		 is_valid(sockID > 0),is_cleanly_disconnected(false){
			whendebug(if (!is_valid) this->why_not_valid = "initial connection failed");
			assert(is_valid || why_not_valid != "no reason set for invalidity of socket!");

		 }

	Socket::Socket(Socket&& o)
		:sockID(o.sockID),
		 is_valid(o.is_valid),
		 is_cleanly_disconnected(o.is_cleanly_disconnected)
	{
		o.is_valid = false;
		whendebug(o.why_not_valid = "Socket moved away");
		whendebug(if (is_valid==false) this->why_not_valid = "move-constructed from invalid socket" );
	}
	
	Socket Socket::connect(int ip, int portno){
		return Socket{ip,portno};
	}
	
	std::size_t Socket::drain(std::size_t size, void* target){
		assert(is_valid ? true : [&]{std::cerr << why_not_valid << std::endl; return false;}());
		auto ret = recv(sockID, target,size,0);
		if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)){
			throw Timeout{};
		}
                else if(ret == -1){
                    throw ProtocolException{std::strerror(errno)};
                }
		else return ret;
	}

	namespace {
		std::size_t receive_helper_socket_cpp(whendebug(std::string& why_not_valid,) bool& is_valid, bool& is_cleanly_disconnected, const int& sockID, std::size_t how_many, std::size_t const * const sizes, void ** bufs, bool peek){
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
			is_valid = false;
			is_cleanly_disconnected = true;
			whendebug(why_not_valid = "connection broken");
			throw BrokenSocketException(err.str());
		}
		else if (n < (int) total_size){
			throw ProtocolException("MSG_WAITALL is supposed to do something here, right?");
		}
		return total_size;
	}
	}

	std::size_t Socket::raw_receive(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
		assert(is_valid ? true : [&]{std::cerr << why_not_valid << std::endl; return false;}());
		return receive_helper_socket_cpp(whendebug(why_not_valid,) is_valid,is_cleanly_disconnected,sockID,how_many,sizes,bufs,false);
	}

	std::size_t Socket::raw_send(std::size_t how_many, std::size_t const * const sizes, void const * const * const bufs){
		if (is_cleanly_disconnected) throw BrokenSocketException("connection broken");
		else if (!is_valid) throw ProtocolException(std::strerror(errno));
		assert(is_valid ? true : [&]{std::cerr << why_not_valid << std::endl; return false;}());
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
			auto sent = ::sendmsg(sockID,&payload,MSG_NOSIGNAL);
			/*std::cout << "sent " << sent << " bytes in total" << std::endl; //*/
			bool complete = sent == (long) total_size;
			if (!complete) {
				if (sent == -1 && errno == EPIPE) {
					is_valid = false;
					whendebug(why_not_valid = "received epipe");
				}
				else if (sent == -1){
					std::stringstream err;
					err << "tried sending " << total_size << " bytes, achieved " << sent << " accompanying error: " << std::strerror(errno);
					throw ProtocolException(err.str());
				}
			}
		}
		else throw BrokenSocketException("attempt to send on broken connection!");
		return total_size;
	}

	std::size_t Socket::peek(std::size_t how_many, std::size_t const * const sizes, void ** bufs){
		assert(is_valid ? true : [&]{std::cerr << why_not_valid << std::endl; return false;}());
		return receive_helper_socket_cpp(whendebug(why_not_valid,) is_valid,is_cleanly_disconnected,sockID,how_many,sizes,bufs,true);
	}

	Socket Socket::set_timeout(Socket s, std::chrono::microseconds time){
		assert(s.is_valid);
		using namespace std::chrono;
		seconds _seconds = duration_cast<seconds>(time);
		microseconds _microseconds = time - _seconds;
		assert(_seconds + _microseconds == time);
		struct timeval tv{_seconds.count(),_microseconds.count()};
#ifndef NDEBUG
		auto success =
#endif
			setsockopt(s.sockID, SOL_SOCKET,SO_RCVTIMEO, (char*) &tv, sizeof(tv));
		assert(success == 0);
		return s;
	}
}
