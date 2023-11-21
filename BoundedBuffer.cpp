#include "BoundedBuffer.h"

#include <cassert>
#include <cstring>
#include <iostream> // testing
using namespace std;


BoundedBuffer::BoundedBuffer (int _cap) : cap(_cap) {
    // modify as needed
}

BoundedBuffer::~BoundedBuffer () {
    // modify as needed
}

void BoundedBuffer::push (char* msg, int size) {
    // 1. Convert the incoming byte sequence given by msg and size into a vector<char>
    //      use one of the vector constructor's
    vector<char> data(msg, msg + size);
    // 2. Wait until there is room in the queue (i.e., queue lengh is less than cap)
    //      waiting on slot available
    unique_lock<mutex> lock(bufferMutex);
    pushCondition.wait(lock, [this] {return q.size() < static_cast<size_t>(cap);});
    // 3. Then push the vector at the end of the queue
    q.push(data);
    // 4. Wake up threads that were waiting for push
    //      notifying data available
    lock.unlock();
    popCondition.notify_one(); // notifying data available

}

int BoundedBuffer::pop(char* msg, int size) {
    // 1. Wait until the queue has at least 1 item
    std::unique_lock<std::mutex> lock(bufferMutex);
    popCondition.wait(lock, [this] { return !q.empty(); });
    
    // 2. Pop the front item of the queue. The popped item is a vector<char>
    std::vector<char> data = q.front();
    q.pop();
    
    // 3. Convert the popped vector<char> into a char*, copy that into msg
    size_t data_size = data.size();
    std::cout << "Popped data size: " << data_size << std::endl; // Add this line for debugging

    assert(data_size <= static_cast<size_t>(size));

    if (data_size > static_cast<size_t>(size)) {
        // If data size exceeds the specified size, truncate the data
        data_size = static_cast<size_t>(size);
    }

    memcpy(msg, data.data(), data_size);
    
    // 4. Wake up threads that were waiting for pop
    lock.unlock();
    pushCondition.notify_one(); // notifying slot available
    
    // 5. Return the actual size of data popped
    return static_cast<int>(data_size);
}


size_t BoundedBuffer::size () {
    return q.size();
}
