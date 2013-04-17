
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

int
main(int, char**)
{
        amp.reset(new rhd2k::rhd2000(sampling_rate));
        test_dspcutoff();
}
