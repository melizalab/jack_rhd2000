#ifndef _RHD2K_H
#define _RHD2K_H

#include <stdint.h>
#include <iosfwd>
#include <cassert>
#include <vector>

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

std::ostream &
print_command(std::ostream &, short);

/**
 * Represents a single RHD2000 amplifer.
 */
class rhd2000 {

public:
        /** the data type for the rhd2000 registers */
        typedef unsigned char data_type;
        /** the total number of registers on the chip */
        static const std::size_t register_count = 64;
        /** the maximum number of available amps */
        static const std::size_t max_amps = 32;
        /** the number of commands in the register programming sequence */
        static const std::size_t register_sequence_length = 60;
        /** the data type for the amp power mask */
        typedef uint32_t power_mask_type;

        explicit rhd2000(std::size_t sampling_rate);
        ~rhd2000() {}

        /* public methods to update register values */
        double upper_cutoff() const;
        void set_upper_cutoff(double);

        double lower_cutoff() const;
        void set_lower_cutoff(double);

        bool dsp_enabled() const;
        double dsp_cutoff() const;
        /** if arg is <= 0, turn off dsp */
        void set_dsp_cutoff(double);

        void set_amp_power(std::size_t channel, bool powered);
        void set_amp_power(power_mask_type mask);
        bool amp_power(std::size_t chan) const;
        power_mask_type amp_power() const;
        std::size_t amps_powered() const;

        /**
         * Update internal state with results from regset command. Parses
         * through a block of data recevied from the RHD2000 eval
         * board.
         *
         * @param data    the data block received from the RHD2000 eval board.
         *                Must be at least register_sequence_length frames long
         *
         * @param offset the offset (in words) into the frame to the channel
         *               containing the results from the regset command
         *               sequence. If the sequence is loaded in aux command slot
         *               3, this will be (6 + nstreams * 35 + stream).
         *
         * @param size   the size of the frame, in bytes
         */
        void update(void const * data, std::size_t offset, std::size_t size);

        /** True if an amplifier is connected to this port */
        bool connected() const;
        /** The revision number for the RHD2000 die */
        int revision() const;
        /** The number of amplifiers on the chip */
        int amps() const;
        /** The chip ID */
        int chip_id() const;

        void command_regset(std::vector<short> &out, bool calibrate) const ;
        void command_auxsample(std::vector<short> &out) const;
        template <typename InputIterator>
        void command_dac(std::vector<short> & out, InputIterator first, InputIterator last) const;

        friend std::ostream & operator<< (std::ostream &, rhd2000 const &);

private:
        void set_sampling_rate_registers();

        const std::size_t _sampling_rate;
        data_type _registers[register_count];

};


template <typename InputIterator>
void
rhd2000::command_dac(std::vector<short> & out, InputIterator first, InputIterator last) const
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
