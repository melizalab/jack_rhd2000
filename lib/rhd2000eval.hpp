#ifndef _RHD2000EVAL_H
#define _RHD2000EVAL_H

#include <cassert>
#include <iosfwd>
#include <map>
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
                PortA1 = 0,
                PortA2 = 1,
                PortB1 = 2,
                PortB2 = 3,
                PortC1 = 4,
                PortC2 = 5,
                PortD1 = 6,
                PortD2 = 7,
        };

        evalboard(std::size_t sampling_rate, char const * serial=0, char const * firmware=0);
        ~evalboard();

        /* daq_interface virtual member functions */
        void start(std::size_t max_frames=0);
        bool running();
        void stop();
        void wait(std::size_t);
        std::size_t nframes();
        std::size_t frame_size() const;
        std::size_t sampling_rate() const { return _sampling_rate; }
        std::size_t adc_channels() const;

        /**
         * Returns a table of ADC channels. For each element of the table (it),
         * it.first is a name comprising the MISO port (or "EV" for the eval
         * board ADCs) and the channel number, and it.second gives the offset
         * into the frame buffer. Only enabled channels are included.
         */
        std::map<std::size_t, std::string> adc_table() const;

        /**
         * @overload daq_interface::read()
         *
         * @return the number of frames read, or 0 if there was an error. If the
         * RHD2000eval FIFO contains less than the requested number of samples,
         * the last sample in the buffer will be repeated, so it's important to
         * check whether enough frames are ready
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

        void dac_monitor_stream(int dac, int stream, int channel);
        void enable_dac_monitor(int dac, bool enabled);

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

        okFrontPanel_HANDLE _dev;
        okPLL22393_HANDLE _pll;
        rhd2000 * _mosi[nmosi];
        rhd2000 * _miso[nmiso];
        double _cable_lengths[nmosi];

        uint _sampling_rate;
        ulong _board_version;
        ulong _enabled_streams;
        std::size_t _nactive_streams;
};

std::ostream & operator<< (std::ostream &, evalboard::auxcmd_slot);
std::ostream & operator<< (std::ostream &, evalboard::mosi_id);
std::ostream & operator<< (std::ostream &, evalboard::miso_id);


} // namespace

#endif
