#include <thread>
#include "FIFORequestChannel.h"

using namespace std;


int buffercapacity = MAX_MESSAGE;
char* buffer = NULL; // buffer used by the server, allocated in the main

int nchannels = 0;
vector<string> all_data[NUM_PERSONS];


// pre-declared because function signature required call in process_newchannel_request
void handle_process_loop (FIFORequestChannel* _channel);

void process_newchannel_request (FIFORequestChannel* _channel) {
	nchannels++;
	string new_channel_name = "data" + to_string(nchannels) + "_";
	char buf[30];
	strcpy(buf, new_channel_name.c_str());
	_channel->cwrite(buf, new_channel_name.size()+1);

	FIFORequestChannel* data_channel = new FIFORequestChannel(new_channel_name, FIFORequestChannel::SERVER_SIDE);
	thread thread_for_client(handle_process_loop, data_channel);
	thread_for_client.detach();
}


void populate_file_data (int person) {
	//cout << "populating for person " << person << endl;
	string filename = "BIMDC/" + to_string(person) + ".csv";
	char line[100];
	ifstream ifs(filename.c_str());
	if (ifs.fail()){
		EXITONERROR("Data file: " + filename + " does not exist in the BIMDC/ directory");
	}
	
	while (!ifs.eof()) {
		line[0] = 0;
		ifs.getline(line, 100);
		if (ifs.eof()) {
			break;
		}
		
		if (line[0]) {
			all_data[person-1].push_back(string(line));
		}
	}
}

double get_data_from_memory(int person, double seconds, int ecgno) {
    std::cout << "DEBUG: get_data_from_memory - person: " << person << " seconds: " << seconds << " ecgno: " << ecgno << std::endl;

    int index = (int)round(seconds / 0.004);

    if (person < 1 || person > NUM_PERSONS) {
        std::cerr << "ERROR: Invalid person number: " << person << std::endl;
        return 0.0;  // Return a default value in case of an error
    }

    if (index < 0 || index >= all_data[person - 1].size()) {
        std::cerr << "ERROR: Invalid index: " << index << std::endl;
        return 0.0;  // Return a default value in case of an error
    }

    std::cout << "DEBUG: Inside valid range - index: " << index << std::endl;

    std::string line = all_data[person - 1][index];
    std::vector<std::string> parts = split(line, ',');

    double ecg1 = std::stod(parts[1]);
    double ecg2 = std::stod(parts[2]);

    if (ecgno == 1) {
        return ecg1;
    } else {
        return ecg2;
    }
}


void process_file_request (FIFORequestChannel* rc, char* request) {
	filemsg f = *((filemsg*) request);
	string filename = request + sizeof(filemsg);
	filename = "BIMDC/" + filename; // adding the path prefix to the requested file name
	//cout << "Server received request for file " << filename << endl;

	if (f.offset == 0 && f.length == 0) { // means that the client is asking for file size
		__int64_t fs = get_file_size (filename);
		rc->cwrite ((char*) &fs, sizeof(__int64_t));
		return;
	}

	/* request buffer can be used for response buffer, because everything necessary have
	been copied over to filemsg f and filename*/
	char* response = request; 

	// make sure that client is not requesting too big a chunk
	if (f.length > buffercapacity) {
		cerr << "Client is requesting a chunk bigger than server's capacity" << endl;
		cerr << "Returning nothing (i.e., 0 bytes) in response" << endl;
		rc->cwrite(response, 0);
	}

	FILE* fp = fopen(filename.c_str(), "rb");
	if (!fp) {
		cerr << "Server received request for file: " << filename << " which cannot be opened" << endl;
		rc->cwrite(buffer, 0);
		return;
	}
	fseek(fp, f.offset, SEEK_SET);
	int nbytes = fread(response, 1, f.length, fp);

	/* making sure that the client is asking for the right # of bytes,
	this is especially imp for the last chunk of a file when the 
	remaining lenght is < buffercap of the client*/
	assert(nbytes == f.length); 

	rc->cwrite(response, nbytes);
	fclose(fp);
}

void process_data_request (FIFORequestChannel* rc, char* request) {
	std::cout << "process_data_request" << std::endl;
	datamsg* d = (datamsg*) request;
	std::cout << "person: " <<  d->person << " seconds: " << d->seconds << " ecgno: " << d->ecgno << std::endl;
	double data = get_data_from_memory(d->person, d->seconds, d->ecgno);
	rc->cwrite(&data, sizeof(double));
}

void process_unknown_request (FIFORequestChannel* rc) {
	char a = 0;
	rc->cwrite(&a, sizeof(char));
}


void process_request (FIFORequestChannel* rc, char* _request) {
	std::cout << "process_request" << std::endl;
	datamsg* d = (datamsg*) _request;
	std::cout << "person: " <<  d->person << " seconds: " << d->seconds << " ecgno: " << d->ecgno << std::endl;
	MESSAGE_TYPE m = *((MESSAGE_TYPE*) _request);
	if (m == DATA_MSG) {
		usleep(rand() % 5000);
		process_data_request(rc, _request);
	}
	else if (m == FILE_MSG) {
		process_file_request(rc, _request);
	}
	else if (m == NEWCHANNEL_MSG) {
		process_newchannel_request(rc);
	}
	else {
		process_unknown_request(rc);
	}
}

void handle_process_loop (FIFORequestChannel* channel) {
	/* creating a buffer per client to process incoming requests
	and prepare a response */
	char* buffer = new char[buffercapacity];
	if (!buffer) {
		EXITONERROR ("Cannot allocate memory for server buffer");
	}

while (true) {
    int nbytes = channel->cread(buffer, buffercapacity);
    if (nbytes < 0) {
        cerr << "Client-side terminated abnormally" << endl;
        break;
    } else if (nbytes == 0) {
        cout << "Server could not read anything... Terminating" << endl;
        break;
    }

    MESSAGE_TYPE m = *((MESSAGE_TYPE*) buffer);
    if (m == QUIT_MSG) {
        cout << "Client-side is done and exited" << endl;
        break;
    }

    // Ensure the buffer is interpreted as a datamsg
    if (m == DATA_MSG) {
        datamsg* d = (datamsg*) buffer;

		d->ecgno = 1; // WHY DO I HAVE TO DO THIS
        cout << "process_request: person=" << d->person << " seconds=" << d->seconds << " ecgno=" << d->ecgno << endl;

        // Debug prints to check the datamsg values before get_data_from_memory
        cout << "before get_data_from_memory: person=" << d->person << " seconds=" << d->seconds << " ecgno=" << d->ecgno << endl;

        process_request(channel, buffer);

        // Debug prints after get_data_from_memory
        cout << "after get_data_from_memory: person=" << d->person << " seconds=" << d->seconds << " ecgno=" << d->ecgno << endl;
    }
    // ...
}

	delete[] buffer;
	delete channel;
}

int main (int argc, char* argv[]) {
	buffercapacity = MAX_MESSAGE;
	int opt;
	while ((opt = getopt(argc, argv, "m:")) != -1) {
		switch (opt) {
			case 'm':
				buffercapacity = atoi(optarg);
				break;
		}
	}

	srand(time_t(NULL));
	for (int i = 0; i < NUM_PERSONS; i++) {
		populate_file_data(i+1);
	}
	
	FIFORequestChannel* control_channel = new FIFORequestChannel("control", FIFORequestChannel::SERVER_SIDE);
	handle_process_loop(control_channel);
	cout << "Server terminated" << endl;
}
