#include <fstream>
#include <iostream>
#include <thread>
#include <sys/time.h>
#include <sys/wait.h>
#include <mutex> //
#include <atomic> //

#include "BoundedBuffer.h"
#include "common.h"
#include "Histogram.h"
#include "HistogramCollection.h"
#include "FIFORequestChannel.h"

// ecgno to use for datamsgs
#define ECGNO 1
using namespace std;
std::mutex msg_buffer_mutex;  // Declare a mutex
std::atomic<bool> terminate_workers(false);

void patient_thread_function (BoundedBuffer& request_buffer, int n, int p_num) {
    // functionality of the patient threads

    // take a patient p_num
    // for n requests, produce a datamsg (p_num, time, ECGNO) and push to request_buffer
    //      - time dependent on current requests:
    //      - at 0 -> time = 0.000; at 1 -> time = 0.004, at 2 -> time = 0.008; ...
    for (int i = 0; i < n; i++) {

        double time = i * 0.004;
        //std::cout << "i= " << i << " n= " << n << std::endl;
        //std::cout << "patient_thread function_running with p_num= " << p_num << " time= " << time << " ecgno= " << ECGNO << std::endl;
        datamsg dmsg(p_num, time, ECGNO);
        request_buffer.push((char*)&dmsg, sizeof(datamsg));
    }

    std::cout << "Producer thread " << p_num << " completed." << std::endl;
}

void file_thread_function (BoundedBuffer& request_buffer, const string& file_name, int file_size) {
    // functionality of the file thread

    // file 
    // open output file; allocate the memory fseek; close the file
    // while offset < file_size, produce a filemsg(offset, m)+filename and push to request_buffer
    //      - incrementing offset; and be careful with the final message
    ifstream file(file_name, ios::binary | ios::ate);
    int offset = 0;
    
    while (offset < file_size) {
        int remaining_size = min(MAX_MESSAGE, file_size - offset);
        filemsg fmsg(offset, remaining_size);
        std::cout << "file_thread function running with offset= " << offset << " remaining_size= " << remaining_size << std::endl;

        request_buffer.push((char*)&fmsg, sizeof(filemsg));
        
        offset += remaining_size;
    }
    file.close();
}

void worker_thread_function(BoundedBuffer& request_buffer, BoundedBuffer& response_buffer, FIFORequestChannel* chan) {
    while (true) {
        char msg_buffer[MAX_MESSAGE];

        {
            std::unique_lock<std::mutex> lock(msg_buffer_mutex);
            
            // Wait for a signal or terminate condition
            request_buffer.wait(lock, [&]() {
                return !request_buffer.isEmpty() || terminate_workers.load();
            });

            // Check termination signal
            if (terminate_workers.load()) {
                break;
            }

            // Pop message from the request_buffer
            request_buffer.pop((char*)msg_buffer, sizeof(datamsg));
        }

        MESSAGE_TYPE* msg_type = (MESSAGE_TYPE*)msg_buffer;

        if (*msg_type == DATA_MSG) {
            datamsg* dmsg = (datamsg*)msg_buffer;
            // ... (omitted for brevity)
        } else if (*msg_type == FILE_MSG) {
            filemsg* fmsg = (filemsg*)msg_buffer;
            // ... (omitted for brevity)
        } else if (*msg_type == QUIT_MSG) {
            break;
        }
    }
}



void histogram_thread_function (BoundedBuffer& response_buffer, HistogramCollection& hc) {
    // functionality of the histogram threads

    // forever loop
    // pop response from the response_buffer
    // call HC::update(resp->p_no, resp->double)

    //std::cout << "histogram_thread function_running" << std::endl;

    while (true) {
        char msg_buffer[MAX_MESSAGE];
        response_buffer.pop((char*)msg_buffer, sizeof(std::pair<int, double>));
        //request_buffer.pop((char*)&msg_buffer, sizeof(datamsg));
        std::pair<int, double>* dmsg = (std::pair<int, double>*)msg_buffer;
        //std::cout << "updating histogram with person= " << dmsg->first << " double= " << dmsg->second << std::endl;
        hc.update(dmsg->first,  dmsg->second);
    }
}


int main (int argc, char* argv[]) {
    int n = 1000;	// default number of requests per "patient"
    int p = 10;		// number of patients [1,15]
    int w = 100;	// default number of worker threads
	int h = 20;		// default number of histogram threads
    int b = 20;		// default capacity of the request buffer (should be changed)
	int m = MAX_MESSAGE;	// default capacity of the message buffer
	string f = "";	// name of file to be transferred
    
    // read arguments
    int opt;
	while ((opt = getopt(argc, argv, "n:p:w:h:b:m:f:")) != -1) {
		switch (opt) {
			case 'n':
				n = atoi(optarg);
                break;
			case 'p':
				p = atoi(optarg);
                break;
			case 'w':
				w = atoi(optarg);
                break;
			case 'h':
				h = atoi(optarg);
				break;
			case 'b':
				b = atoi(optarg);
                break;
			case 'm':
				m = atoi(optarg);
                break;
			case 'f':
				f = optarg;
                break;
		}
	}
    
	// fork and exec the server
    int pid = fork();
    if (pid == 0) {
        execl("./server", "./server", "-m", (char*) to_string(m).c_str(), nullptr);
    } 
    else {

    //this_thread::sleep_for(chrono::seconds(2));
    
	// initialize overhead (including the control channel)
	FIFORequestChannel* chan = new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE);
    BoundedBuffer request_buffer(b);
    BoundedBuffer response_buffer(b);
	HistogramCollection hc;

    // array of producer threads (if data, p elements; if file, 1 element)
    // array of FIFOs (w elements)
    // array of worker threads (w elements)
    // array of histogram threads (if data, h elements; if files, zero elements)
    vector<thread> producerThreads;
    vector<FIFORequestChannel*> channels;
    vector<thread> workerThreads;
    vector<thread> histogramThreads;

    // making histograms and adding to collection
    for (int i = 0; i < p; i++) {
        Histogram* h = new Histogram(10, -2.0, 2.0);
        hc.add(h);
    }
	
	// record start time
    struct timeval start, end;
    gettimeofday(&start, 0);

    /* create all threads here */
    // Method 1
    // if data:
    //      - create p patient_threads (store producer array)
    //      - create w workers_threads (store worker array)
    //          -> create channel (store FIFO array)
    //      - create h histogram_threads (store histogram array)
    // if file:
    //      - create 1 file_thread (store producer array)
    //      - create w workers_threads (store worker array)
    //          -> create channel (store FIFO array)
    
    int file_size = get_file_size(f.c_str()); 
    if (f == "") {
        for (int i = 0; i < p; i++) {
            //std::cout << "producerThreads.push_back(thread(patient_thread_function, ref(request_buffer), n, i + 1));" << std::endl;
            producerThreads.push_back(thread(patient_thread_function, ref(request_buffer), n, i + 1));
        }

        for (int i = 0; i < w; i++) {
            channels.push_back(new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE));
            //std::cout << "worker channel i= " << i << " w= " << w << std::endl;
            workerThreads.push_back(thread(worker_thread_function, ref(request_buffer), ref(response_buffer), channels[i]));
        }

        for (int i = 0; i < h; i++) {
            //std::cout << "histogram channel i= " << i << std::endl;
            histogramThreads.push_back(thread(histogram_thread_function, ref(response_buffer), ref(hc)));        }
    }
    else {
        producerThreads.push_back(thread(file_thread_function, ref(request_buffer), f, file_size));

        for (int i = 0; i < w; i++) {
            //std::cout << "worker channel2 i= " << i << " w= " << w << std::endl;
            channels.push_back(new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE));
            workerThreads.push_back(thread(worker_thread_function, ref(request_buffer), ref(response_buffer), channels[i]));
        }
    }

    // Method 2
    // if data:
    //      - create p patient_threads
    // if file:
    //      - create 1 file_thread
    //
    // create w worker_threads
    //      - create w channels
    //
    // if data:
    //      - create h hist_threads

	/* join all threads here */
    // iterate over all thread arrays, calling join
    //      - order is very important; producers before consumers
    for (auto& thread : producerThreads) {
        cout << "joining producer threads" << std::endl;
        thread.join();
    }

    terminate_workers.store(true);
    for (auto& thread : workerThreads) {
        cout << "joining worker threads" << std::endl;
        thread.join();
    }

    for (auto& thread : histogramThreads) {
        cout << "joining hist threads" << std::endl;
        thread.join();
    }

	// record end time
    gettimeofday(&end, 0);

    // print the results
	if (f == "") {
		hc.print();
	}
    int secs = ((1e6*end.tv_sec - 1e6*start.tv_sec) + (end.tv_usec - start.tv_usec)) / ((int) 1e6);
    int usecs = (int) ((1e6*end.tv_sec - 1e6*start.tv_sec) + (end.tv_usec - start.tv_usec)) % ((int) 1e6);
    cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;

    // quit and close all channels in FIFO array

	// quit and close control channel
    MESSAGE_TYPE q = QUIT_MSG;
    chan->cwrite ((char *) &q, sizeof (MESSAGE_TYPE));
    cout << "All Done!" << endl;
    delete chan;

	// wait for server to exit
	wait(nullptr);
    }
}
