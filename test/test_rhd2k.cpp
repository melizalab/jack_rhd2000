
#include <iostream>
#include <boost/shared_ptr.hpp>
#include "rhd2k.hpp"

using namespace std;

static const unsigned int sampling_rate = 30000;
boost::shared_ptr<rhd2k::rhd2000> amp;

void
test_dspcutoff()
{
        double test_omega = 0.1103;
        amp->set_dsp_cutoff(0.0);
        assert( !amp->dsp_enabled());

        for (int i = 1; i < 16; ++i) {
                double req = test_omega * sampling_rate;
                amp->set_dsp_cutoff(req);
                assert (amp->dsp_enabled());
                cout << "dsp cutoff: requested=" << req << " Hz"
                     << ", got=" << amp->dsp_cutoff() << " Hz (ω=" << amp->dsp_cutoff() / sampling_rate << ')'
                     <<endl;
                test_omega /= 2;
        }

}

void
test_lowerbandwidth()
{
        const double targets[] = {0.1, 0.5, 1.0, 5.0, 10.0, 50.0, 100.0, 200.0, 500};
        for (size_t i = 0; i < sizeof(targets) / sizeof(double); ++i) {
                amp->set_lower_bandwidth(targets[i]);
                cout << "lower bandwidth: requested=" << targets[i] << " Hz"
                     << ", got=" << amp->lower_bandwidth() << endl;
        }
}

void
test_upperbandwidth()
{
        const double targets[] = {100, 150, 200, 250, 300, 500, 750, 1000,
                                  2000, 2500, 3000, 5000, 7500, 10000, 15000, 20000};
        for (size_t i = 0; i < sizeof(targets) / sizeof(double); ++i) {
                amp->set_upper_bandwidth(targets[i]);
                cout << "upper bandwidth: requested=" << targets[i] << " Hz"
                     << ", got=" << amp->upper_bandwidth() << endl;
        }
}
int
main(int, char**)
{
        amp.reset(new rhd2k::rhd2000(sampling_rate));
        test_dspcutoff();
        test_lowerbandwidth();
        test_upperbandwidth();
}
