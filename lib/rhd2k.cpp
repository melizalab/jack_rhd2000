#include <boost/utility/binary.hpp>
#include <cmath>
#include <bitset>
#include "rhd2k.hpp"

static unsigned char[] default_registers =
{ BOOST_BINARY(11011110),      // 0: mostly fixed values; fast settle [5] off
  BOOST_BINARY(01000000),      // 1: set ADC buffer bias [5:0] based on sample rate
  BOOST_BINARY(00000000),      // 2: set mux bias [5:0] based on sample rate
  BOOST_BINARY(00000010),      // 3: digout HiZ [1] = 1
  BOOST_BINARY(10000000),      // 4: set DSP enable [4] and cutoff [3:0] in class
  BOOST_BINARY(00000000),      // 5: impedance check DAC disabled
  BOOST_BINARY(10000000),      // 6: DAC set to 0
  BOOST_BINARY(00000000),      // 7: zcheck DAC disconnected
  BOOST_BINARY(00000000),      // 8: offchip rh1 [7] disabled; set rh1 value [5:0] in class
  BOOST_BINARY(10000000),      // 9: adc1 enabled [7]; set rh1 value [4:0] in class
  BOOST_BINARY(00000000),      // 10: offchip rh2 [7] disabled; set rh2 value [5:0] in class
  BOOST_BINARY(10000000),      // 11: adc2 enabled [7]; set rh2 value [4:0] in class
  BOOST_BINARY(00000000),      // 12: offchip rl [7] disabled; set rl dac1 [6:0] in class
  BOOST_BINARY(10000000),      // 13: adc3 enabled [7]; set rl dac3 [6] and dac2 [5:0] in class
  BOOST_BINARY(11111111),      // 14: amps 0-7 enabled
  BOOST_BINARY(11111111),      // 15: amps 8-15 enabled
  BOOST_BINARY(11111111),      // 16: amps 16-23 enabled
  BOOST_BINARY(11111111)       // 17: amps 23-31 enabled
};

static const double RH1Base = 2200.0;
static const double RH1Dac1Unit = 600.0;
static const double RH1Dac2Unit = 29400.0;
static const int RH1Dac1Steps = 63;
static const int RH1Dac2Steps = 31;

static const double RH2Base = 8700.0;
static const double RH2Dac1Unit = 763.0;
static const double RH2Dac2Unit = 38400.0;
static const int RH2Dac1Steps = 63;
static const int RH2Dac2Steps = 31;

// Returns the value of the RH1 resistor (in ohms) corresponding to a particular upper
// bandwidth value (in Hz).
static double
rH1FromUpperBandwidth(double upperBandwidth)
{
    double log10f = log10(upperBandwidth);

    return 0.9730 * pow(10.0, (8.0968 - 1.1892 * log10f + 0.04767 * log10f * log10f));
}

// Returns the value of the RH2 resistor (in ohms) corresponding to a particular upper
// bandwidth value (in Hz).
static double
rH2FromUpperBandwidth(double upperBandwidth)
{
    double log10f = log10(upperBandwidth);

    return 1.0191 * pow(10.0, (8.1009 - 1.0821 * log10f + 0.03383 * log10f * log10f));
}

// Returns the value of the RL resistor (in ohms) corresponding to a particular lower
// bandwidth value (in Hz).
static double
rLFromLowerBandwidth(double lowerBandwidth)
{
    double log10f = log10(lowerBandwidth);

    if (lowerBandwidth < 4.0) {
        return 1.0061 * pow(10.0, (4.9391 - 1.2088 * log10f + 0.5698 * log10f * log10f +
                                   0.1442 * log10f * log10f * log10f));
    } else {
        return 1.0061 * pow(10.0, (4.7351 - 0.5916 * log10f + 0.08482 * log10f * log10f));
    }
}

// Returns the amplifier upper bandwidth (in Hz) corresponding to a particular value
// of the resistor RH1 (in ohms).
static double
upperBandwidthFromRH1(double rH1)
{
    double a, b, c;

    a = 0.04767;
    b = -1.1892;
    c = 8.0968 - log10(rH1/0.9730);

    return pow(10.0, ((-b - sqrt(b * b - 4 * a * c))/(2 * a)));
}

// Returns the amplifier upper bandwidth (in Hz) corresponding to a particular value
// of the resistor RH2 (in ohms).
static double
upperBandwidthFromRH2(double rH2)
{
    double a, b, c;

    a = 0.03383;
    b = -1.0821;
    c = 8.1009 - log10(rH2/1.0191);

    return pow(10.0, ((-b - sqrt(b * b - 4 * a * c))/(2 * a)));
}

// Returns the amplifier lower bandwidth (in Hz) corresponding to a particular value
// of the resistor RL (in ohms).
static double
lowerBandwidthFromRL(double rL)
{
    double a, b, c;

    // Quadratic fit below is invalid for values of RL less than 5.1 kOhm
    if (rL < 5100.0) {
        rL = 5100.0;
    }

    if (rL < 30000.0) {
        a = 0.08482;
        b = -0.5916;
        c = 4.7351 - log10(rL/1.0061);
    } else {
        a = 0.3303;
        b = -1.2100;
        c = 4.9873 - log10(rL/1.0061);
    }

    return pow(10.0, ((-b - sqrt(b * b - 4 * a * c))/(2 * a)));
}


using namespace rhd2k;

rhd2000::rhd2000(unsigned int sampling_rate)
        : _sampling_rate(sampling_rate),
          _registers({0})
{
        memcpy(_registers, default_registers, sizeof(default_registers));
        set_dsp_cutoff(1.0);
        set_upper_bandwidth(10000);
        set_lower_bandwidth(1.0);
}

double
rhd2000::upper_bandwidth() const {
        int rh1dac1 = _registers[8] & 0x1f;
        int rh1dac2 = _registers[9] & 0x0f;
        int rh2dac1 = _registers[10] & 0x1f;
        int rh2dac2 = _registers[11] & 0x0f;

        double rh1 = RH1Base + RH1Dac1Unit * rh1dac1 + RH1Dac2Unit * rh1dac2;
        double rh2 = RH2Base + RH2Dac1Unit * rh2dac1 + RH2Dac2Unit * rh2dac2;
        return sqrt(upperBandwidthFromRH1(rh1) * upperBandwidthFromRH2(rh2));
}

void
rhd2000::set_upper_bandwidth(double)
{}

double
rhd2000::lower_bandwidth() const
{
        int rldac1 = _registers[12] & 0x3f;
        int rldac2 = _registers[13] & 0x1f;
        int rldac3 = _registers[13] & 0x20;
        double rl = RLBase + RLDac1Unit * rldac1 + RLDac2Unit * rldac2 + RLDac3Unit * rldac3;
        return lowerBandwidthFromRL(rl);
}

void
rhd2000::set_lower_bandwidth(double)
{}

bool
rhd2000::dsp_enabled() const
{
        return (_registers[4] & 0x08) == 1;
}

double
rhd2000::dsp_cutoff() const
{
        const double Pi = 2*acos(0.0);
        double x = _registers[4] & 0x07;
        x = pow(2.0, x);
        return _sampling_rate * log(x / (x - 1.0)) / (2*Pi);
}

void
rhd2000::set_dsp_cutoff(double)
{}

void
rhd2000::set_amp_power(unsigned short channel, bool powered)
{
        assert(channel <= 32);
        std::size_t reg = (channel / 8) + 14;
        unsigned char mask = 1 << (channel % 8);
        if (powered) _registers[reg] |= mask;
        else _registers[reg] &= ~mask;
}


// std::vector<int>
// rhd2000::amp_power() const;

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

