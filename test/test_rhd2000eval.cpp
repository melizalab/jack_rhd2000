
#include <signal.h>
#include <iostream>
#include <iomanip>
#include <boost/shared_ptr.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include "rhd2000eval.hpp"

using namespace rhd2k;
using namespace std;
using namespace boost::posix_time;

static const size_t sampling_rate = 20000;
static const size_t period_size = 1024;
boost::shared_ptr<evalboard> dev;
char * buffer;

volatile bool flag = true;

void
stats(std::ostream & o, double psum, double p2sum, double pmax, size_t n)
{
        psum /= n;
        p2sum /= n;
        o << "mean: " << psum << ", stdev: " << sqrt(p2sum - psum * psum) << ", max: " << pmax << endl;
}

void
signal_handler(int sig)
{
        flag = false;
}

void
test_one()
{
        // read a second of data and save it to disk
        FILE * fp = fopen("test.dat","wb");
        // FILE * debug = fopen("test.txt","wt");

        dev->start(sampling_rate);
        while(dev->running()) {
                usleep(10);
        }
        size_t nframes = dev->nframes();
        assert (nframes == sampling_rate);

        while (nframes > 0) {
                dev->read(buffer, period_size);
                assert(evalboard::frame_header == *(uint64_t*)buffer);

                for (size_t t = 0; t < nframes && t < period_size; ++t) {
                        char * b = buffer + dev->frame_size() * t;
                        // fprintf(debug,"%i\t%i\n",
                        //         *(uint32_t*)(b+sizeof(uint64_t)),
                        //         *(unsigned short*)(b+50));
                        fwrite(b + sizeof(uint64_t), 
			       sizeof(char), 
			       dev->frame_size() - sizeof(uint64_t), 
			       fp);
                }
                nframes = dev->nframes();
        }
        // fclose(debug);
        fclose(fp);

}

void
test_rate()
{
        // accumulators for calculating average delays
        double us;
        ptime start, stop;
        double pmax = 0;
        double psum = 0;
        double p2sum = 0;

        double rmax = 0, rsum = 0, r2sum = 0;

        // stream some data
        size_t period = 0;
        size_t frames;

        cout << "streaming with frame size " << dev->frame_size() << " bytes, transfer size "
             << dev->frame_size() * period_size << endl;
        signal(SIGINT,  signal_handler);
        signal(SIGTERM, signal_handler);
        signal(SIGHUP,  signal_handler);

        dev->start();
        while (flag) {
                // poll
                start = microsec_clock::universal_time();
                frames = dev->nframes();
                stop = microsec_clock::universal_time();

                if (frames < period_size) {
                        usleep(100);
                        continue;
                }

                us = (stop - start).total_microseconds();
                us /= 1e3;
                pmax = std::max(us,pmax);
                psum += us;
                p2sum += us * us;

                // read
                start = microsec_clock::universal_time();
                dev->read(buffer, period_size);
                stop = microsec_clock::universal_time();
                us = (stop - start).total_microseconds();
                us /= 1e3;
                cout << "\rperiod " << period << ": frame=" << *(uint*)(buffer+8) << flush;
                rmax = std::max(us,rmax);
                rsum += us;
                r2sum += us * us;

                period++;
                if (period % 100 == 0) {
                        stats(cout << "\npoll time: ", psum, p2sum, pmax, period);
                        stats(cout << "read time: ", rsum, r2sum, rmax, period);
                }
        }
        cout << endl;
        signal(SIGINT,  SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP,  SIG_DFL);

}


int
main(int, char**)
{
  dev.reset(new evalboard(sampling_rate,0,0,"driver"));
        dev->set_cable_meters(evalboard::PortA, 1.8);
        dev->configure_port(evalboard::PortA, 1.0, 7500, 10.0, 0xffffffff);
        dev->enable_stream(evalboard::PortA1);
        dev->calibrate_amplifiers();

        cout << *dev << endl;
        buffer = new char[dev->frame_size() * period_size];

        test_one();

        test_rate();
        cout << "end of test" << endl;

        delete[] buffer;
}
