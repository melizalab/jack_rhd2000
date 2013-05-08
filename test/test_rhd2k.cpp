
#include <iostream>
#include <iomanip>
#include <boost/shared_ptr.hpp>
#include "rhd2k.hpp"

using namespace std;

static const unsigned int sampling_rate = 30000;
boost::shared_ptr<rhd2k::rhd2000> amp;

template <typename It>
void
print_commands(std::ostream & o, It begin, It end)
{
        int i = 0;
        It it;
        int channel, reg, data;

        for (it = begin; it != end; ++it) {
                if ((*it & 0xc000) == 0x0000) {
                        channel = (*it & 0x3f00) >> 8;
                        o << "  command[" << i << "] = CONVERT(" << channel << ")" << endl;
                } else if ((*it & 0xc000) == 0xc000) {
                        reg = (*it & 0x3f00) >> 8;
                        o << "  command[" << i << "] = READ(" << reg << ")" << endl;
                } else if ((*it & 0xc000) == 0x8000) {
                        reg = (*it & 0x3f00) >> 8;
                        data = (*it & 0x00ff);
                        o << "  command[" << i << "] = WRITE(" << reg << ",0x";
                        o << hex << internal << setfill('0') << setw(2)
                          << data
                          << dec;
                        o << ")" << endl;
                } else if (*it == 0x5500) {
                        o << "  command[" << i << "] = CALIBRATE" << endl;
                } else if (*it == 0x6a00) {
                        o << "  command[" << i << "] = CLEAR" << endl;
                } else {
                        o << "  command[" << i << "] = INVALID COMMAND: 0x";
                        o << hex << internal << setfill('0') << setw(4)
                          << *it
                          << dec << endl;
                }
                ++i;
        }
        o << endl;
}


void
test_base_state()
{
        assert(!amp->connected());
}

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
test_lowercutoff()
{
        const double targets[] = {0.1, 0.5, 1.0, 5.0, 10.0, 50.0, 100.0, 200.0, 500};
        for (size_t i = 0; i < sizeof(targets) / sizeof(double); ++i) {
                amp->set_lower_cutoff(targets[i]);
                cout << "lower cutoff: requested=" << targets[i] << " Hz"
                     << ", got=" << amp->lower_cutoff() << endl;
        }
}

void
test_uppercutoff()
{
        const double targets[] = {100, 150, 200, 250, 300, 500, 750, 1000,
                                  2000, 2500, 3000, 5000, 7500, 10000, 15000, 20000};
        for (size_t i = 0; i < sizeof(targets) / sizeof(double); ++i) {
                amp->set_upper_cutoff(targets[i]);
                cout << "upper cutoff: requested=" << targets[i] << " Hz"
                     << ", got=" << amp->upper_cutoff() << endl;
        }
}
int
main(int, char**)
{
        amp.reset(new rhd2k::rhd2000(sampling_rate));
        test_base_state();
        test_dspcutoff();
        test_lowercutoff();
        test_uppercutoff();
        std::cout << *amp << endl;

        std::vector<short> commands;
        amp->command_regset(commands, true);
        print_commands(cout, commands.begin(), commands.end());

        amp->command_auxsample(commands);
        print_commands(cout, commands.begin(), commands.end());

        std::vector<double> dac(100,0.0);
        amp->command_dac(commands, dac.begin(), dac.end());
        print_commands(cout, commands.begin(), commands.end());
}
