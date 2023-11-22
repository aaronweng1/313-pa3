#include "FIFORequestChannel.h"
#include <mutex>

using namespace std;

/*--------------------------------------------------------------------------*/
/*		CONSTRUCTOR/DESTRUCTOR FOR CLASS	R e q u e s t C h a n n e l		*/
/*--------------------------------------------------------------------------*/

std::mutex pipeCreationMutex;

FIFORequestChannel::~FIFORequestChannel () { 
	close(wfd);
	close(rfd);

	remove(pipe1.c_str());
	remove(pipe2.c_str());
}

/*--------------------------------------------------------------------------*/
/*			MEMBER FUNCTIONS FOR CLASS	R e q u e s t C h a n n e l			*/
/*--------------------------------------------------------------------------*/

int FIFORequestChannel::open_pipe(std::string _pipe_name, int mode) {
    std::lock_guard<std::mutex> lock(pipeCreationMutex);  // Lock the mutex during pipe creation

    if (mode == CLIENT_SIDE) {
        mkfifo(_pipe_name.c_str(), 0666);
        int wfd = open(_pipe_name.c_str(), O_WRONLY);
        return wfd;
    } else if (mode == SERVER_SIDE) {
        int rfd = open(_pipe_name.c_str(), O_RDONLY);
        return rfd;
    }
    return -1;
}

int FIFORequestChannel::cread (void* msgbuf, int msgsize) {
	return read (rfd, msgbuf, msgsize); 
}

int FIFORequestChannel::cwrite (void* msgbuf, int msgsize) {
	return write (wfd, msgbuf, msgsize);
}

string FIFORequestChannel::name () {
	return my_name;
}
