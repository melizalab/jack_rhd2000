
#include <iostream>
#include <iomanip>
#include <boost/shared_ptr.hpp>
#include "rhd2000eval.hpp"

using namespace std;

static const size_t sampling_rate = 30000;
static const size_t period_size = 1024;
static const size_t nperiods = 20;
boost::shared_ptr<rhd2000eval> dev;


int
main(int, char**)
{
        char * buffer;
        float * values;  // [nchan][period_size]
        dev.reset(new rhd2000eval(sampling_rate));
        dev->configure_port(rhd2000eval::PortA, 100, 3000, 1, 0x0000ffff);
        dev->scan_amplifiers();

        cout << *dev << endl;

        // stream some data and convert it to floats
        size_t nchannels = dev->adc_channels();
        buffer = new char[dev->frame_size() * period_size];
        values = new float[nchannels * period_size];

        cout << "streaming with frame size " << dev->frame_size() << " bytes" << endl;
        dev->start();
        for (std::size_t period = 0; period < nperiods; ++period) {
                while (dev->nframes_ready() < period_size) {
                        usleep(1);
                }
                dev->read(buffer, period_size);
                cout << "period " << period << ": frame=" << *(uint*)(buffer+8) << endl;
        }
        dev->stop();
        size_t last = dev->nframes_ready();
        if (last) {
                dev->read(buffer, last);
                cout << "last period: frame: " << *(uint*)(buffer+8) << endl;
        }
        cout << "end of test" << endl;

}
