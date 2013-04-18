#ifndef _RHD2000EVAL_H
#define _RHD2000EVAL_H

#include <iosfwd>
#include <boost/shared_ptr.hpp>
#include "daq_interface.hpp"

#define RHYTHM_BOARD_ID 500L
typedef void* okFrontPanel_HANDLE;
typedef void* okPLL22393_HANDLE;

namespace rhd2k {
        class rhd2000;
}

struct ok_error : public daq_error {
        int _code;
        ok_error(int const & ec) : daq_error("Opal Kelly error"), _code(ec) {}
        char const * what() const throw();
};

class rhd2000eval : public daq_interface {

public:
        // number of output ports (= number of MOSI lines)
        static const std::size_t nports = 4;
        // number of inputs (= number of MISO lines)
        static const std::size_t ninputs = 8;

        enum port_id {
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

        enum input_id {
                PortA1 = 0,
                PortA2 = 1,
                PortB1 = 2,
                PortB2 = 3,
                PortC1 = 4,
                PortC2 = 5,
                PortD1 = 6,
                PortD2 = 7,
                PortA1Ddr = 8,
                PortA2Ddr = 9,
                PortB1Ddr = 10,
                PortB2Ddr = 11,
                PortC1Ddr = 12,
                PortC2Ddr = 13,
                PortD1Ddr = 14,
                PortD2Ddr = 15
        };

        rhd2000eval(std::size_t sampling_rate, char const * serial=0, char const * firmware=0);
        ~rhd2000eval();

        /* daq_interface virtual member functions */
        void start(std::size_t max_frames=0);
        bool running();
        void stop();
        std::size_t nframes_ready();
        std::size_t frame_size();
        /*
         * @overload daq_interface::read()
         *
         * @note if the RHD2000eval FIFO contains less than the requested number
         * of samples, the last sample in the buffer will be repeated.
         */
        std::size_t read(void *, std::size_t);

        std::size_t sampling_rate() const { return _sampling_rate; }
        std::size_t adc_nchannels() const;

        /* rhd2k eval specific */
        void set_cable_meters(port_id port, double meters);
        void set_cable_feet(port_id port, double feet) {
                set_cable_meters(port,  0.03048 * feet);
        }
        void set_filtering(port_id port, double lower, double upper, double dsp);
        void set_amp_power(port_id port, ulong mask);

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
        void set_port_auxcommand(port_id port, auxcmd_slot slot, ulong bank);

        std::size_t streams_enabled() const ;
        bool stream_enabled(std::size_t stream) const;
        void enable_stream(std::size_t stream, bool enabled);

        void set_leds(int value, int mask);
        void ttl_out(int value, int mask);
        int ttl_in();

        void dac_monitor_stream(int dac, int stream, int channel);
        void enable_dac_monitor(int dac, bool enabled);

        friend std::ostream & operator<< (std::ostream &, rhd2000eval const &);
        friend std::ostream & operator<< (std::ostream &, auxcmd_slot);
        friend std::ostream & operator<< (std::ostream &, port_id);
        friend std::ostream & operator<< (std::ostream &, input_id);


protected:
        bool dcm_done() const;
        bool clock_locked() const;

private:
        void reset_board();
        void set_sampling_rate();
        void set_cable_delay(port_id port, uint delay);
        void scan_amplifiers();

        void set_cmd_ram(auxcmd_slot slot, ulong bank, ulong index, ulong command);
        ulong words_in_fifo() const;
        void enable_streams(ulong);

        okFrontPanel_HANDLE _dev;
        okPLL22393_HANDLE _pll;
        rhd2k::rhd2000 * _ports[nports];
        rhd2k::rhd2000 * _amplifiers[ninputs];

        uint _sampling_rate;
        ulong _board_version;
        ulong _enabled_streams;
        std::size_t _nactive_streams;
};

#endif
