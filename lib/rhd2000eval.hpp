#ifndef _RHD2000EVAL_H
#define _RHD2000EVAL_H

#include <cassert>
#include <iosfwd>
#include <vector>
#include <string>
#include "daq_interface.hpp"

#define FEET_PER_METERS 0.3048f

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
 * RHD2000 chips can be detected automatically or manually. For manual
 * configuration, set the cable length, enable the stream, and run calibrate().
 * For automatic configuration, run scan_ports()
 *
 */
class evalboard : public daq_interface {

public:
        /// how the digitized samples are stored
        typedef unsigned short data_type;
        /// number of output ports (= number of MOSI lines)
        static const std::size_t nmosi = 4;
        /// number of inputs (= number of MISO lines)
        static const std::size_t nmiso = 8;
        /// number of auxiliary ADCs on the board
        static const std::size_t naux_adcs = 8;
        /// number of auxiliary DACs on the board
        static const std::size_t naux_dacs = 8;
        /// all returned frames should start with this value
        static const unsigned long long frame_header = 0xc691199927021942ULL;

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

        evalboard(std::size_t sampling_rate,
                  char const * serial=0,
                  char const * firmware=0,
                  char const * libdir=0);
        ~evalboard();

        /* daq_interface virtual member functions */
        void start(std::size_t max_frames=0);
        bool running() const;
        void stop();
        std::size_t nframes() const;
        std::size_t frame_size() const;
        std::size_t sampling_rate() const { return _sampling_rate; }
        std::size_t adc_channels() const { return _adc_table.size(); }
        std::size_t dac_nchannels() const { return naux_dacs; }

        /**
         * Returns a vector of the defined ADC channels. Only enabled channels
         * are included.
         */
        std::vector<channel_info_t> const & adc_table() const { return _adc_table; }

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

        /** Run the calibration sequence on all connected amplifiers */
        void calibrate_amplifiers();

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
        bool stream_enabled(miso_id stream) const;
        /** enable or disable a stream for data collection */
        void enable_stream(miso_id stream, bool enabled=true);

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
        void set_cmd_ram(auxcmd_slot slot, ulong bank, ulong index, ulong command);

        bool dcm_done() const;
        bool clock_locked() const;
        ulong words_in_fifo() const;

        /**
         * Enable/disable streams using a bitmask. In constrast to
         * enable_stream, this does not regenerate the adc table.
         */
        void enable_streams(ulong);
        void set_cable_delay(mosi_id port, uint delay);
        void set_dac_source(uint dac, ulong arg);
        void update_adc_table();

private:
        /* object is non-copyable */
        evalboard(evalboard const &);
        evalboard& operator=(evalboard const &);

        void reset_board();
        void set_sampling_rate(uint rate);
        /// convert cable length (m) to FPGA delay (ticks) for current sampling rate
        uint cable_meters_to_delay(double) const;
        /// convert FPGA delay to cable length (m) for current sampling rate
        double cable_delay_to_meters(uint) const;

        okFrontPanel_HANDLE _dev;
        okPLL22393_HANDLE _pll;
        rhd2000 * _mosi[nmosi];
        rhd2000 * _miso[nmiso];
        std::vector<double> _cable_lengths;

        uint _sampling_rate;
        ulong _board_version;
        ulong _enabled_streams;
        std::size_t _nactive_streams;
        std::vector<channel_info_t> _adc_table;

        ulong _dac_sources[naux_dacs];
};

std::ostream & operator<< (std::ostream &, evalboard::auxcmd_slot);
std::ostream & operator<< (std::ostream &, evalboard::mosi_id);
std::ostream & operator<< (std::ostream &, evalboard::miso_id);


} // namespace

#endif
