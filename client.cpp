#include <fstream>
#include <iostream>
#include <thread>
#include <sys/time.h>
#include <sys/wait.h>

#include "BoundedBuffer.h"
#include "common.h"
#include "Histogram.h"
#include "HistogramCollection.h"
#include "FIFORequestChannel.h"

// ecgno to use for datamsgs
#define ECCNO 1

using namespace std;

void patient_thread_function(BoundedBuffer& request_buffer, int n, int p_num) {
    for (int i = 0; i < n; i++) {
        double time = i * 0.004;
        datamsg dmsg(p_num, time, ECCNO);
        request_buffer.push((char*)&dmsg, sizeof(datamsg));
    }
}

void file_thread_function(BoundedBuffer& request_buffer, const string& file_name, int file_size) {
    ifstream file(file_name, ios::binary | ios::ate);
    int offset = 0;

    while (offset < file_size) {
        int remaining_size = min(MAX_MESSAGE, file_size - offset);
        filemsg fmsg(offset, remaining_size);
        request_buffer.push((char*)&fmsg, sizeof(filemsg));
        offset += remaining_size;
    }
    file.close();
}

void worker_thread_function(BoundedBuffer& request_buffer, BoundedBuffer& response_buffer, FIFORequestChannel* chan) {
    while (true) {
        char msg_buffer[MAX_MESSAGE];
        request_buffer.pop(msg_buffer, sizeof(char));
        MESSAGE_TYPE* msg_type = (MESSAGE_TYPE*)msg_buffer;

        if (*msg_type == DATA_MSG) {
            chan->cwrite(msg_buffer, sizeof(datamsg));
            chan->cread(msg_buffer, MAX_MESSAGE);
            response_buffer.push(msg_buffer, sizeof(datamsg));
        } else if (*msg_type == FILE_MSG) {
            filemsg* fmsg = (filemsg*)msg_buffer;
            chan->cwrite(msg_buffer, sizeof(filemsg) + fmsg->length);
            chan->cread(msg_buffer, MAX_MESSAGE);
            response_buffer.push(msg_buffer, sizeof(filemsg) + fmsg->length);
        } else if (*msg_type == QUIT_MSG) {
            break;
        }
    }
}

void histogram_thread_function(BoundedBuffer& response_buffer, HistogramCollection& hc) {
    while (true) {
        char msg_buffer[MAX_MESSAGE];
        response_buffer.pop(msg_buffer, sizeof(char));
        datamsg* dmsg = (datamsg*)msg_buffer;
        hc.update(dmsg->person, *(double*)(msg_buffer + sizeof(datamsg)));
    }
}

int main(int argc, char* argv[]) {
    int n = 1000;   // default number of requests per "patient"
    int p = 10;     // number of patients [1,15]
    int w = 100;    // default number of worker threads
    int h = 20;     // default number of histogram threads
    int b = 20;     // default capacity of the request buffer (should be changed)
    int m = MAX_MESSAGE;    // default capacity of the message buffer
    string f = "";  // name of file to be transferred

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
        execl("./server", "./server", "-m", (char*)to_string(m).c_str(), nullptr);
    } else {
        BoundedBuffer request_buffer(b);
        BoundedBuffer response_buffer(b);
        HistogramCollection hc;

        vector<thread> producerThreads;
        vector<FIFORequestChannel*> channels;
        vector<thread> workerThreads;
        vector<thread> histogramThreads;

        for (int i = 0; i < p; i++) {
            Histogram* h = new Histogram(10, -2.0, 2.0);
            hc.add(h);
        }

        struct timeval start, end;
        gettimeofday(&start, 0);

        int file_size = get_file_size(f.c_str());
        if (f == "") {
            for (int i = 0; i < p; i++) {
                producerThreads.push_back(thread(patient_thread_function, ref(request_buffer), n, i + 1));
            }

            for (int i = 0; i < w; i++) {
                channels.push_back(new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE));
                workerThreads.push_back(thread(worker_thread_function, ref(request_buffer), ref(response_buffer), channels[i]));
            }

            for (int i = 0; i < h; i++) {
                histogramThreads.push_back(thread(histogram_thread_function, ref(response_buffer), ref(hc)));
            }
        } else {
            producerThreads.push_back(thread(file_thread_function, ref(request_buffer), f, file_size));

            for (int i = 0; i < w; i++) {
                channels.push_back(new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE));
                workerThreads.push_back(thread(worker_thread_function, ref(request_buffer), ref(response_buffer), channels[i]));
            }
        }

        for (auto& thread : producerThreads) {
            thread.join();
        }

        for (auto& thread : workerThreads) {
            thread.join();
        }

        for (auto& thread : histogramThreads) {
            thread.join();
        }

        gettimeofday(&end, 0);

        if (f == "") {
            hc.print();
        }

        int secs = ((1e6 * end.tv_sec - 1e6 * start.tv_sec) + (end.tv_usec - start.tv_usec)) / ((int)1e6);
        int usecs = (int)((1e6 * end.tv_sec - 1e6 * start.tv_sec) + (end.tv_usec - start.tv_usec)) % ((int)1e6);
        cout << "Took " << secs << " seconds and " << usecs << " microseconds" << endl;

        MESSAGE_TYPE q = QUIT_MSG;
        for (auto& channel : channels) {
            channel->cwrite((char*)&q, sizeof(MESSAGE_TYPE));
            delete channel;
        }

        delete channels[0]; // control channel

        wait(nullptr);
    }
}
