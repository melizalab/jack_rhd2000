#include <stdint.h>
#include <cmath>
#include <ostream>
#include <sstream>
#include <iomanip>
#include <boost/utility/binary.hpp>
#include "daq_interface.hpp"
#include "rhd2k.hpp"

#define RH1_DAC1R 8
#define RH1_DAC1M 0x3f
#define RH1_DAC2R 9
#define RH1_DAC2M 0x1f
#define RH2_DAC1R 10
#define RH2_DAC1M 0x3f
#define RH2_DAC2R 11
#define RH2_DAC2M 0x1f
#define RL_DAC1R 12
#define RL_DAC1M 0x7f
#define RL_DAC2R 13
#define RL_DAC2M 0x3f
#define RL_DAC3R 13
#define RL_DAC3M 0x40
#define AMP_REGISTER 14

using std::size_t;
using namespace rhd2k;

static rhd2000::data_type ram_register_defaults[] =
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
static const size_t ram_register_count = sizeof(ram_register_defaults);

static const double RH1Base = 2200.0;
static const double RH1Dac1Unit = 600.0;
static const double RH1Dac2Unit = 29400.0;

static const double RH2Base = 8700.0;
static const double RH2Dac1Unit = 763.0;
static const double RH2Dac2Unit = 38400.0;

static const double RLBase = 3500.0;
static const double RLDac1Unit = 175.0;
static const double RLDac2Unit = 12700.0;
static const double RLDac3Unit = 3000000.0;
static const double Pi = 2*acos(0.0);


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

rhd2000::rhd2000(size_t sampling_rate)
        : _sampling_rate(sampling_rate)
{
        memset(_registers, 0, register_count * sizeof(data_type));
        memcpy(_registers, ram_register_defaults, ram_register_count * sizeof(data_type));
        set_sampling_rate_registers();
        set_dsp_cutoff(1.0);
        set_upper_bandwidth(10000);
        set_lower_bandwidth(1.0);
}

double
rhd2000::upper_bandwidth() const {
        int rh1dac1 = _registers[RH1_DAC1R] & RH1_DAC1M;
        int rh1dac2 = _registers[RH1_DAC2R] & RH1_DAC2M;
        int rh2dac1 = _registers[RH2_DAC1R] & RH2_DAC1M;
        int rh2dac2 = _registers[RH2_DAC2R] & RH2_DAC2M;

        double rh1 = RH1Base + RH1Dac1Unit * rh1dac1 + RH1Dac2Unit * rh1dac2;
        double rh2 = RH2Base + RH2Dac1Unit * rh2dac1 + RH2Dac2Unit * rh2dac2;
        return sqrt(upperBandwidthFromRH1(rh1) * upperBandwidthFromRH2(rh2));
}

void
rhd2000::set_upper_bandwidth(double hz)
{
        int rh1dac1, rh1dac2, rh2dac1, rh2dac2;
        double rh1target, rh2target;
        // Upper bandwidths higher than 30 kHz don't work well with the RHD2000 amplifiers
        if (hz > 30000.0) hz = 30000.0;


        rh1target = rH1FromUpperBandwidth(hz);
        rh1dac2 = floor((rh1target - RH1Base) / RH1Dac2Unit);
        rh1dac1 = round((rh1target - RH1Base - rh1dac2 * RH1Dac2Unit) / RH1Dac1Unit);
        assert(rh1dac1 >= 0 && rh1dac2 >= 0 && rh1dac1 <= RH1_DAC1M && rh1dac2 <= RH1_DAC2M);

        rh2target = rH2FromUpperBandwidth(hz);
        rh2dac2 = floor((rh2target - RH2Base) / RH2Dac2Unit);
        rh2dac1 = round((rh2target - RH2Base - rh2dac2 * RH2Dac2Unit) / RH2Dac1Unit);
        assert(rh2dac1 >= 0 && rh2dac2 >= 0 && rh2dac1 <= RH2_DAC1M && rh2dac2 <= RH2_DAC2M);

        _registers[RH1_DAC1R] = (_registers[RH1_DAC1R] & ~RH1_DAC1M) | (rh1dac1 & RH1_DAC1M);
        _registers[RH1_DAC2R] = (_registers[RH1_DAC2R] & ~RH1_DAC2M) | (rh1dac2 & RH1_DAC2M);
        _registers[RH2_DAC1R] = (_registers[RH2_DAC1R] & ~RH2_DAC1M) | (rh2dac1 & RH2_DAC1M);
        _registers[RH2_DAC2R] = (_registers[RH2_DAC2R] & ~RH2_DAC2M) | (rh2dac2 & RH2_DAC2M);

#ifndef NDEBUG
        // test bit math
        double expect1 = upperBandwidthFromRH1(RH1Base + RH1Dac1Unit * rh1dac1 + RH1Dac2Unit * rh1dac2);
        double expect2 = upperBandwidthFromRH2(RH2Base + RH2Dac1Unit * rh2dac1 + RH2Dac2Unit * rh2dac2);
        assert(upper_bandwidth() == sqrt(expect1 * expect2));
#endif

}

double
rhd2000::lower_bandwidth() const
{
        int rldac1 = _registers[RL_DAC1R] & RL_DAC1M;
        int rldac2 = _registers[RL_DAC2R] & RL_DAC2M;
        int rldac3 = (_registers[RL_DAC3R] & RL_DAC3M) > 0;
        double rl = RLBase + RLDac1Unit * rldac1 + RLDac2Unit * rldac2 + RLDac3Unit * rldac3;
        return lowerBandwidthFromRL(rl);
}

void
rhd2000::set_lower_bandwidth(double hz)
{
        int dac1, dac2, dac3;
        double target;

        // restrict range to published specs (upper=500 in the doc, but 1500 in code)
        if (hz < 0.1) hz = 0.1;
        else if (hz > 1500.0) hz = 1500.0;
        target = rLFromLowerBandwidth(hz);
        dac3 = (hz < 0.15);
        dac2 = floor((target - RLBase - dac3 * RLDac3Unit) / RLDac2Unit);
        dac1 = round((target - RLBase - dac3 * RLDac3Unit - dac2 * RLDac2Unit) / RLDac1Unit);
        assert(dac1 >= 0 && dac2 >= 0 && dac1 <= RL_DAC1M && dac2 <= RL_DAC2M);

        _registers[RL_DAC1R] = (_registers[RL_DAC1R] & 0x80) | (dac1 & RL_DAC1M);
        _registers[RL_DAC2R] = (_registers[RL_DAC2R] & 0x80) | (dac2 & RL_DAC2M);
        _registers[RL_DAC3R] = (_registers[RL_DAC2R] & ~RL_DAC3M) | (dac3 << 6);

#ifndef NDEBUG
        // test bit math
        double expected = lowerBandwidthFromRL(RLBase + RLDac1Unit * dac1 + RLDac2Unit * dac2 + RLDac3Unit * dac3);
        assert(lower_bandwidth() == expected);
#endif
}

bool
rhd2000::dsp_enabled() const
{
        return (_registers[4] & 0x10) > 0;
}

double
rhd2000::dsp_cutoff() const
{
        /* omega = log(2**n / (2**n - 1)) / 2pi */
        double x = _registers[4] & 0x0f;
        x = pow(2.0, x);
        return _sampling_rate * log(x / (x - 1.0)) / (2*Pi);
}

void
rhd2000::set_dsp_cutoff(double hz)
{
        if (hz <= 0) {
                _registers[4] &= ~0x10;
        }
        else {
                /* 2**n = exp(2 pi omega) / (exp (2 pi omega) - 1) */
                /* find closest value of n */
                double q = exp(2 * Pi * hz / _sampling_rate);
                double n = round(log2(q / (q - 1)));
                if (n < 1) n = 1;
                _registers[4] = (_registers[4] & 0xe0) | 0x10 | ((data_type)n & 0x0f);
        }

}

void
rhd2000::set_amp_power(size_t channel, bool powered)
{
        assert(channel <= max_amps);
        size_t reg = (channel / 8) + AMP_REGISTER;
        data_type mask = 1 << (channel % 8);
        if (powered) _registers[reg] |= mask;
        else _registers[reg] &= ~mask;
}

std::bitset<rhd2000::max_amps>
rhd2000::amp_power() const
{
        // TODO: read more than 32 bits if number of amps increases
        return std::bitset<max_amps>(*reinterpret_cast<uint32_t const *>(_registers+AMP_REGISTER));
}

size_t
rhd2000::namps_powered() const
{
        return amp_power().count();
}

bool
rhd2000::connected() const
{
        return (strncmp("INTAN", reinterpret_cast<char const *>(_registers + 40), 5) == 0);
}

int
rhd2000::revision() const
{
        return _registers[60];
}

int
rhd2000::namplifiers() const
{
        return _registers[62];
}

int
rhd2000::chip_id() const
{
        return _registers[63];
}

void
rhd2000::set_sampling_rate_registers()
{
        int muxBias, adcBufferBias;

        if (_sampling_rate < 3334) {
                muxBias = 40;
                adcBufferBias = 32;
        } else if (_sampling_rate < 4001) {
                muxBias = 40;
                adcBufferBias = 16;
        } else if (_sampling_rate < 5001) {
                muxBias = 40;
                adcBufferBias = 8;
        } else if (_sampling_rate < 6251) {
                muxBias = 32;
                adcBufferBias = 8;
        } else if (_sampling_rate < 8001) {
                muxBias = 26;
                adcBufferBias = 8;
        } else if (_sampling_rate < 10001) {
                muxBias = 18;
                adcBufferBias = 4;
        } else if (_sampling_rate < 12501) {
                muxBias = 16;
                adcBufferBias = 3;
        } else if (_sampling_rate < 15001) {
                muxBias = 7;
                adcBufferBias = 3;
        } else {
                muxBias = 4;
                adcBufferBias = 2;
        }

        _registers[1] = (adcBufferBias & 0x3f) | (_registers[1] & 0xe0);
        _registers[2] = muxBias;
}

void
rhd2000::update(void const * data, size_t offset, size_t stride)
{
        size_t t;
        size_t reg;
        // do the math with short pointers
        offset /= sizeof(short);
        stride /= sizeof(short);
        short const * ptr = reinterpret_cast<short const*>(data) + offset;

        // first check for INTAN - if it's not there, there's no amp or the
        // delay is wrong (check command_regset to make sure offset is correct)
        // NB: the aux data is delayed by a frame (see p. 9 in the manual)
        t = ram_register_count * 2 + 1;
        for (reg = 40; reg < 45; ++reg, ++t) {
                _registers[reg] = ptr[stride*t];
        }
        if (!connected()) return;

        // compare RAM
        t = ram_register_count + 1;
        for (reg = 0; reg < ram_register_count; ++reg, ++t) {
                // registers 3 and 6 are under control of other command seqs
                if (reg == 3 || reg == 6) continue;
                if (ptr[stride*t] != _registers[reg]) {
                        std::ostringstream s;
                        s << "register " << reg << ": expected " << _registers[reg]
                          << "; got " << ptr[stride*t];
                        throw daq_error(s.str());
                }
        }
        t += 5;
        // copy ROM registers
        for (reg = 48; reg < 56; ++reg, ++t) {
                _registers[reg] = ptr[stride*t];
        }
        for (reg = 59; reg < 64; ++reg, ++t) {
                _registers[reg] = ptr[stride*t];
        }
}


void
rhd2000::command_regset(std::vector<short> &out, bool do_calibrate)
{
        size_t reg;
        size_t c = 0;
        out.resize(register_sequence_length);

        // Start with a few dummy commands in case chip is still powering up
        out[c++] = reg_read(63);
        out[c++] = reg_read(63);

        // program RAM registers
        for (reg = 0; reg < ram_register_count; ++reg) {
                // skip register 3 and register 6 - set in a different command
                if (reg == 3 || reg == 6) continue;
                out[c++] = reg_write(reg, _registers[reg]);
        }

        // read back RAM registers
        for (reg = 0; reg < ram_register_count; ++reg) {
                out[c++] = reg_read(reg);
        }

        // read intan name
        for (reg = 40; reg < 45; ++reg) {
                out[c++] = reg_read(reg);
        }

        // read chip name
        for (reg = 48; reg < 56; ++reg) {
                out[c++] = reg_read(reg);
        }

        // read other rom registers
        for (reg = 59; reg < 64; ++reg) {
                out[c++] = reg_read(reg);
        }

        if (do_calibrate)
                out[c++] = calibrate();

        // fill out command list with dummies
        while (c < register_sequence_length) {
                out[c++] = reg_read(63);
        }

}

void
rhd2000::command_auxsample(std::vector<short> &out)
{
        const size_t reg = 3;
        size_t i;
        size_t c = 0;
        out.resize(register_sequence_length);
        data_type reg3 = _registers[reg] | (1 << 2); // enable temp sensor

        for (i = 32; i < 35; ++i) out[c++] = convert(i);          // sample aux1-aux3
        reg3 |= 1 << 3;                                           // sensor 1
        out[c++] = reg_write(reg, reg3);

        for (i = 32; i < 35; ++i) out[c++] = convert(i);          // sample aux1-aux3
        reg3 |= 1 << 4;                                           // sensor 1 + 2
        out[c++] = reg_write(reg, reg3);

        for (i = 32; i < 35; ++i) out[c++] = convert(i);          // sample aux1-aux3
        out[c++] = convert(49);                                   // sample temp

        for (i = 32; i < 35; ++i) out[c++] = convert(i);          // sample aux1-aux3
        reg3 &= ~(1 << 4);                                        // sensor 2
        out[c++] = reg_write(reg, reg3);

        for (i = 32; i < 35; ++i) out[c++] = convert(i);          // sample aux1-aux3
        out[c++] = convert(49);                                   // sample temp

        for (i = 32; i < 35; ++i) out[c++] = convert(i);          // sample aux1-aux3
        reg3 &= ~(1 << 3);                                        // sensors off
        out[c++] = reg_write(reg, reg3);

        for (i = 32; i < 35; ++i) out[c++] = convert(i);          // sample aux1-aux3
        out[c++] = convert(48);                                   // sample Vdd

        while (c < register_sequence_length) {
                for (i = 32; i < 35; ++i) out[c++] = convert(i); // sample aux1-aux3
                out[c++] = reg_read(63);                         // dummy
        }

        assert(out.size() == register_sequence_length);

}


namespace rhd2k {

std::ostream &
operator<< (std::ostream &o, rhd2000 const &r)
{
        if (r.connected()) {
                if (r.chip_id() == 1) o << "RHD2132";
                else o << "RHD2116";
                o << " (rev=" << r.revision() << ", amps=" << r.namplifiers() << "):"
                  << " bandwidth: " << r.lower_bandwidth() << " - " << r.upper_bandwidth() << " Hz"
                  << "; dsp cutoff: ";
                if (r.dsp_enabled())
                        o << r.dsp_cutoff() << " Hz";
                else
                        o << "disabled";
        }
        else {
                o << "no amplifier connected";
        }
        return o;
}

std::ostream &
print_command(std::ostream &o, short cmd)
{
        using namespace std;
        int channel, reg, data;
        if ((cmd & 0xc000) == 0x0000) {
                channel = (cmd & 0x3f00) >> 8;
                o << "CONVERT(" << channel << ")";
        } else if ((cmd & 0xc000) == 0xc000) {
                reg = (cmd & 0x3f00) >> 8;
                o << "READ(" << reg << ")";
        } else if ((cmd & 0xc000) == 0x8000) {
                reg = (cmd & 0x3f00) >> 8;
                data = (cmd & 0x00ff);
                o << "WRITE(" << reg << ",0x"
                  << hex << internal << setfill('0') << setw(2)
                  << data << ")";
        } else if (cmd == 0x5500) {
                o << "CALIBRATE";
        } else if (cmd == 0x6a00) {
                o << "CLEAR";
        } else {
                o << "INVALID COMMAND: 0x";
                o << hex << internal << setfill('0') << setw(4)
                  << cmd;
        }
        return o << dec;
}

}
