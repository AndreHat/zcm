#include <iostream>
#include <queue>
#include <sys/time.h>
#include <cassert>

#include "zcm/zcm-cpp.hpp"

#include "zcm/transport/generic_serial_transport.h"
#include "types/example_t.hpp"

using namespace std;
using namespace zcm;

#define PUBLISH_DT (1e6)/(5)
#define BUFFER_SIZE 200
#define MIN(A, B) ((A) < (B)) ? (A) : (B)

#define MAX_FIFO 12

queue<zuint8_t> fifo;

static zuint32_t get(zuint8_t* data, zuint32_t nData, void* usr)
{
    zuint32_t n = MIN(MAX_FIFO, nData);
    n = MIN(fifo.size(), n);

    for (zuint32_t i = 0; i < n; ++i) {
        data[i] = fifo.front();
        fifo.pop();
    }

    return n;
}

static zuint32_t put(const zuint8_t* data, zuint32_t nData, void* usr)
{
    zuint32_t n = MIN(MAX_FIFO - fifo.size(), nData);

    for (zuint32_t i = 0; i < n; ++i)
        fifo.push(data[i]);

    return n;
}

static zuint64_t utime(void* usr)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (zuint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

class Handler
{
    zint64_t lastHost = 0;

  public:
    void handle(const ReceiveBuffer* rbuf, const zstring_t& chan, const example_t* msg)
    {
        cout << "Message received" << endl;

        if (msg->timestamp <= lastHost || (zint64_t) rbuf->recv_utime < lastHost)  {
            assert("ERROR: utime mismatch. This should never happen");
        }
        lastHost = msg->timestamp;

    }
};

int main(int argc, const char *argv[])
{
    ZCM zcmLocal(zcm_trans_generic_serial_create(&get, &put, NULL,
                                                 &utime, NULL,
                                                 128, 128*5));

    example_t example = {};
    example.num_ranges = 1;
    example.ranges.resize(1);
    example.ranges.at(0) = 1;

    Handler handler;
    auto sub = zcmLocal.subscribe("EXAMPLE", &Handler::handle, &handler);

    zuint64_t nextPublish = 0;
    while (true)
    {
        zuint64_t now = utime(NULL);
        if (now > nextPublish) {
            cout << "Publishing" << endl;
            example.timestamp = now;
            zcmLocal.publish("EXAMPLE", &example);
            nextPublish = now + PUBLISH_DT;
        }

        zcmLocal.handleNonblock();
    }

    zcmLocal.unsubscribe(sub);

    return 0;
}
