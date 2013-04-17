#ifndef _RHD2K_H
#define _RHD2K_H

#include <iosfwd>
#include <cassert>
#include <vector>
#include <bitset>
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
inline short reg_write(unsigned char reg, unsigned char val) {
        assert (reg < 64);
        return (0x8000 | reg << 8 | val);
}


/**
 * Represents a single RHD2000 amplifer.
 */
class rhd2000 : boost::noncopyable {

public:
        /** the data type for the rhd2000 registers */
        typedef unsigned char data_type;
        /** the total number of registers on the chip */
        static const std::size_t register_count = 64;
        /** the maximum number of available amps */
        static const std::size_t max_amps = 32;
        /** the number of commands in the register programming sequence */
        static const std::size_t register_sequence_length = 60;

        static const std::size_t max_command_length = 1024; // this is really
                                                             // determined by
                                                             // the FPGA
        rhd2000(std::size_t sampling_rate);
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

        void set_amp_power(std::size_t channel, bool powered);
        std::bitset<max_amps> amp_power() const;
        std::size_t namps_powered() const;

        /** Update internal state with results from regset command. */
        void update(data_type const * data);

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

        template <typename InputIterator>
        void command_dac(std::vector<short> & out, InputIterator first, InputIterator last);

        friend std::ostream & operator<< (std::ostream &, rhd2000 const &);

private:
        void set_sampling_rate_registers();

        const std::size_t _sampling_rate;
        data_type _registers[register_count];

};


template <typename InputIterator>
void
rhd2000::command_dac(std::vector<short> & out, InputIterator first, InputIterator last)
{
        out.clear();
        for (InputIterator it = first; it != last; ++it) {
                /* scale to 8 bit */
                data_type value = *it + 128.5;
                out.push_back(reg_write(6, value));
        }
}

} // namespace

#endif
