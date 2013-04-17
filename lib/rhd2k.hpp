#ifndef _RHD2K_H
#define _RHD2K_H

#include <cassert>
#include <vector>
#include <boost/noncopyable.hpp>

namespace rhd2k {

inline short calibrate() { return 0x5500; } // 0101010100000000
inline short cal_clear() { return 0x6a00; } // 0110101000000000
inline short convert(unsigned char chan) {
        assert (chan < 64);
        return (0x0000 | chan << 8); // 00cccccc00000000
}
inline short reg_read(unsigned char reg) {
        assert (reg < 64);
        return (0xc000 | reg << 8);
}
inline short reg_write(unsigned char reg, char val) {
        assert (reg < 64);
        return (0x8000 | reg << 8 | val);
}

/**
 * Represents a single RHD2000 amplifer.
 */
class rhd2000 : boost::noncopyable {

public:
        static const unsigned int regset_command_length = 60;
        static const unsigned int max_command_length = 1024; // this is really
                                                             // determined by
                                                             // the FPGA
        rhd2000(unsigned int sampling_rate);
        ~rhd2000() {}

        /* public methods to update register values */
        double upper_bandwidth() const;
        void set_upper_bandwidth(double);

        double lower_bandwidth() const;
        void set_lower_bandwidth(double);

        bool dsp_enabled() const;
        double dsp_cutoff() const;
        /** if arg is <= 0, turn off dsp */
        void set_dsp_cutoff(double);

        void set_amp_power(unsigned short channel, bool powered);
        unsigned long amp_power() const;

        /** Update internal state with results from regset command. */
        void update(unsigned char const * data);

        /** True if an amplifier is connected to this port */
        bool connected() const;
        /** The revision number for the RHD2000 die */
        short revision() const;
        /** The number of amplifiers on the chip */
        short namplifiers() const;
        /** The chip ID */
        short chip_id() const;

        void command_regset(std::vector<short> &out, bool calibrate);
        void command_auxsample(std::vector<short> &out);
        void command_zcheck(std::vector<short> &out, double frequency, double amplitude);

private:
        unsigned int _sampling_rate;
        unsigned char _registers[regset_command_length];

        // RHD2000 Register 0 variables
        // ADC reference generator bandwidth (0 [highest BW] - 3 [lowest BW]); always set to 3
        // static const int adcReferenceBw = 3;
        // // amplifier fast settle (off = normal operation)
        // int ampFastSettle;
        // // enable amplifier voltage references (0 = power down; 1 = enable); 1 = normal operation
        // static const int ampVrefEnable = 1;
        // // ADC comparator preamp bias current (0 [lowest] - 3 [highest], only
        // // valid for comparator select = 2,3); always set to 3
        // static const int adcComparatorBias = 3;
        // // ADC comparator select; always set to 2
        // static const int adcComparatorSelect = 2;

        // // RHD2000 Register 1 variables
        // // supply voltage sensor enable (0 = disable; 1 = enable)
        // static const int vddSenseEnable = 1;
        // // ADC reference buffer bias current (0 [highest current] - 63 [lowest current]);
        // int adcBufferBias;

        // // RHD2000 Register 2 variables
        // // ADC input MUX bias current (0 [highest current] - 63 [lowest current]);
        // int muxBias;

        // // RHD2000 Register 3 variables
        // // MUX capacitance load at ADC input (0 [min CL] - 7 [max CL]); LSB = 3 pF
        // int muxLoad;
        // // temperature sensor S1 (0-1); 0 = power saving mode
        // int tempS1;
        // // temperature sensor S2 (0-1); 0 = power saving mode
        // int tempS2;
        // // temperature sensor enable (0 = disable; 1 = enable)
        // int tempEn;
        // // auxiliary digital output state
        // static const int digOutHiZ = 1;
        // static const int digOut = 0;

        // // RHD2000 Register 4 variables
        // // weak MISO (0 = MISO line is HiZ when CS is inactive; 1 = MISO line is
        // // weakly driven when CS is inactive)
        // static const int weakMiso = 1;
        // // two's complement ADC results (0 = unsigned offset representation; 1 =
        // // signed representation)
        // static const int twosComp = 0;
        // // absolute value mode (0 = normal output; 1 = output passed through
        // // abs(x) function)
        // static const int absMode = 0;
        // int dspEn;              // DSP offset removal enable/disable
        // int dspCutoffFreq;      // DSP offset removal HPF cutoff freqeuncy

        // // RHD2000 Register 5 variables
        // // impedance testing DAC power-up (0 = power down; 1 = power up)
        // static const int zcheckDacPower = 0;
        // // impedance testing dummy load (0 = normal operation; 1 = insert 60 pF to ground)
        // static const int zcheckLoad = 0;
        // // impedance testing scale factor:
        // // 0x00 = 100 fF
        // // 0x01 = 1.0 pF
        // // 0x03 = 10.0 pF
        // static const int zcheckScale = 0;
        // // impedance testing connect all (0 = normal operation; 1 = connect all electrodes together)
        // static const int zcheckConnAll = 0;
        // // impedance testing polarity select (RHD2216 only) (0 = test positive
        // // inputs; 1 = test negative inputs)
        // static const int zcheckSelPol = 0;
        // // enable Z check
        // static const int zcheckEn = 0;

        // // RHD2000 Register 6 variables
        // // this gets written to by aux commands
        // static const int zcheckDac = 128;

        // // RHD2000 Register 7 variables
        // // impedance testing amplifier select (0-63, but MSB is ignored, so 0-31 in practice)
        // static int zcheckSelect;

        // // RHD2000 Register 8-13 variables
        // int offChipRH1;         // bandwidth resistor RH1 on/off chip (0 = on chip; 1 = off chip)
        // int offChipRH2;         // bandwidth resistor RH2 on/off chip (0 = on chip; 1 = off chip)
        // int offChipRL;          // bandwidth resistor RL on/off chip (0 = on chip; 1 = off chip)
        // int adcAux1En;          // enable ADC aux1 input (when RH1 is on chip) (0 = disable; 1 = enable)
        // int adcAux2En;          // enable ADC aux2 input (when RH2 is on chip) (0 = disable; 1 = enable)
        // int adcAux3En;          // enable ADC aux3 input (when RL is on chip) (0 = disable; 1 = enable)

        // // int rH1Dac1;
        // // int rH1Dac2;
        // // int rH2Dac1;
        // // int rH2Dac2;
        // // int rLDac1;
        // // int rLDac2;
        // // int rLDac3;

        // // RHD2000 Register 14-17 variables: amplifier power
        // short apwr[4];

        // // double rH1FromUpperBandwidth(double upperBandwidth) const;
        // // double rH2FromUpperBandwidth(double upperBandwidth) const;
        // // double rLFromLowerBandwidth(double lowerBandwidth) const;
        // // double upperBandwidthFromRH1(double rH1) const;
        // // double upperBandwidthFromRH2(double rH2) const;
        // // double lowerBandwidthFromRL(double rL) const;


};


} // namespace

#endif
