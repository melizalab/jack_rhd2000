#include "rhd2k.hpp"

using namespace rhd2k;

rhd2000::rhd2000(unsigned int sampling_rate)
        : _sampling_rate(sampling_rate),
          ampFastSettle(1),
          tempS1(0),
          tempS2(0),
          tempEn(0),

{
        dsp_enabled() = true;
        set_dsp_cutoff(1.0);
}

double const &
rhd2000::upper_bandwidth() const;

double
rhd2000::set_upper_bandwidth(double);

double const &
rhd2000::lower_bandwidth() const;

double
rhd2000::set_lower_bandwidth(double);

bool &
rhd2000::dsp_enabled();

bool const &
rhd2000::dsp_enabled() const;

double const &
rhd2000::dsp_cutoff() const;

double
rhd2000::set_dsp_cutoff(double);

void
rhd2000::set_amp_power(short channel, bool powered);

std::vector<int>
rhd2000::amp_power() const;

void
rhd2000::update(short const * data);


bool
rhd2000::connected() const;

short
rhd2000::revision() const;

short
rhd2000::namplifiers() const;


short
rhd2000::chip_id() const;

void
rhd2000::command_regset(std::vector<short> &out, bool calibrate);

void
rhd2000::command_auxsample(std::vector<short> &out);

void
rhd2000::command_zcheck(std::vector<short> &out, double frequency, double amplitude);

