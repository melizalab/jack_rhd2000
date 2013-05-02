#ifndef _RHD2000EVAL_H
#define _RHD2000EVAL_H

#include <cassert>
#include <iosfwd>
#include <vector>
#include <string>
#include "daq_interface.hpp"


typedef void* okFrontPanel_HANDLE;
typedef void* okPLL22393_HANDLE;

namespace rhd2k {
        class rhd2000;

struct ok_error : public daq_error {
        int _code;
        ok_error(int const & ec) : daq_error("Opal Kelly error"), _code(ec) {}
        char const * what() const throw();
};

/**
 * Represents an RHD2000 eval board data acquisition system.
 *
 */
class evalboard : public daq_interface {

public:
        /// how the digitized samples are stored
        typedef short data_type;
        /// the maximum value of the data type
        static const float data_type_max = 32768.0f;
        /// number of output ports (= number of MOSI lines)
        static const std::size_t nmosi = 4;
        /// number of inputs (= number of MISO lines)
        static const std::size_t nmiso = 8;
        /// number of auxiliary ADCs on the board
        static const std::size_t naux_adcs = 8;
        /// number of auxiliary DACs on the board
        static const std::size_t naux_dacs = 8;
        /// all returned frames should start with this value
        static const uint64_t frame_header = 0xc691199927021942LL;

        enum mosi_id {
                PortA = 0,
                PortB = 1,
                PortC = 2,
                PortD = 3
        };

        enum auxcmd_slot {
                AuxCmd1 = 0,
                AuxCmd2 = 1,
                AuxCmd3 = 2
        };

        enum miso_id {
                EvalADC = -1,
                PortA1 = 0,
                PortA2 = 1,
                PortB1 = 2,
                PortB2 = 3,
                PortC1 = 4,
                PortC2 = 5,
                PortD1 = 6,
                PortD2 = 7,
        };

        struct channel_info_t {
                miso_id stream;
                uint channel;
                std::size_t byte_offset;
                std::string name;
        };

        evalboard(std::size_t sampling_rate, char const * serial=0, char const * firmware=0);
        ~evalboard();

        /* daq_interface virtual member functions */
        void start(std::size_t max_frames=0);
        bool running() const;
        void stop();
        std::size_t nframes() const;
        std::size_t frame_size() const;
        std::size_t sampling_rate() const { return _sampling_rate; }
        std::size_t adc_channels() const;
        std::size_t dac_nchannels() const { return naux_dacs; }

        /**
         * Returns a vector of the defined ADC channels. Only enabled channels
         * are included.
         */
        std::vector<channel_info_t> const & adc_table() const;

        /**
         * @overload daq_interface::read()
         *
         * @return the number of frames read, or 0 if there was an error. If the
         * RHD2000eval FIFO contains less than the requested number of samples,
         * the last sample in the buffer will be repeated, so it's important to
         * check whether enough frames are ready, or else check the returned
         * buffer for validity.
         */
        std::size_t read(void *, std::size_t);

        /* rhd2k eval specific: */

        /** set the cable length in meters for a port */
        void set_cable_meters(mosi_id port, double meters);

        /** set the cable length in feet for a port */
        void set_cable_feet(mosi_id port, double feet);

        /**
         * Configure the RHD2000 chips on a port. This command will only take
         * effect the next time the amplifier is used.
         *
         * @pre  !running()
         *
         * @param port        the port to modify
         * @param lower       the requested lower cutoff frequency
         * @param upper       the requested upper cutoff frequency
         * @param dsp         the requested cutoff freq. for the dsp,
         *                    or 0 to disable
         * @param amp_power   a bit mask setting the power state for the
         *                    amplifiers (e.g. 0x0000ffff will turn on the
         *                    16 channels and turn off the last 16.
         */
        void configure_port(mosi_id port, double lower, double upper,
                            double dsp, ulong amp_power=0xffffffff);

        /**
         * Scan ports for connected RHD2000 chips. The amplifiers will be
         * calibrated and progammed with the values set in configure_port().
         *
         * @pre !running()
         */
        void scan_ports();

        /** the number of streams that have been enabled */
        std::size_t streams_enabled() const;
        /** true if a stream is enabled */
        bool stream_enabled(std::size_t stream) const;
        /** enable or disable a stream for data collection */
        void enable_stream(std::size_t stream, bool enabled);

        void set_leds(ulong value, ulong mask=0xffffffff);
        void ttl_out(ulong value, ulong mask=0xffffffff);
        ulong ttl_in() const;

        /**
         * Set one of the DACs on the eval board to monitor an input channel.
         *
         * @param dac     the DAC to configure (values 0-8)
         * @param channel the channel to monitor (corresponding to the indices
         *                from adc_table() - however, on-board ADCs can't be monitored
         */
        void dac_monitor(uint dac, uint channel);

        /** disable a DAC */
        void dac_disable(uint dac);

        /**
         * Configure the DACs.
         *
         * @param gain    set the gain of all the DACs to 2**gain (V/V; values 0-8)
         * @param clip    set the noise clip for DACs 0 and 1. zeroes the LSBs
         *                of the signals (values 0-127, corresponding to 0-396 uV)
         */
        void dac_configure(uint gain, uint clip=0);


        friend std::ostream & operator<< (std::ostream &, evalboard const &);

protected:
        template <typename It>
        void upload_auxcommand(auxcmd_slot slot, ulong bank, It first, It last) {
                assert (bank < 16);
                ulong idx = 0;
                for (It it = first; it != last; ++it, ++idx) {
                        set_cmd_ram(slot, bank, idx, *it);
                }
                set_auxcommand_length(slot, idx);
        }
        void set_auxcommand_length(auxcmd_slot slot, ulong length, ulong loop=0);
        void set_port_auxcommand(mosi_id port, auxcmd_slot slot, ulong bank);

        bool dcm_done() const;
        bool clock_locked() const;
        void set_cmd_ram(auxcmd_slot slot, ulong bank, ulong index, ulong command);
        ulong words_in_fifo() const;
        void enable_streams(ulong);

private:
        /* object is non-copyable */
        evalboard(evalboard const &);
        evalboard& operator=(evalboard const &);

        void reset_board();
        void set_sampling_rate();
        void set_cable_delay(mosi_id port, uint delay);
        void set_dac_source(uint dac, ulong arg);
        void make_adc_table() const;

        okFrontPanel_HANDLE _dev;
        okPLL22393_HANDLE _pll;
        rhd2000 * _mosi[nmosi];
        rhd2000 * _miso[nmiso];
        double _cable_lengths[nmosi];

        uint _sampling_rate;
        ulong _board_version;
        ulong _enabled_streams;
        std::size_t _nactive_streams;
        mutable std::vector<channel_info_t> _adc_table;

        ulong _dac_sources[naux_dacs];
};

std::ostream & operator<< (std::ostream &, evalboard::auxcmd_slot);
std::ostream & operator<< (std::ostream &, evalboard::mosi_id);
std::ostream & operator<< (std::ostream &, evalboard::miso_id);


} // namespace

#endif
