#ifndef _RHD2KEVAL_H
#define _RHD2KEVAL_H

#include <iosfwd>
#include "daq_interface.hpp"

#define RHYTHM_BOARD_ID 500L
typedef void* okFrontPanel_HANDLE;
typedef void* okPLL22393_HANDLE;

struct ok_error : public daq_error {
        int _code;
        ok_error(int const & ec) : daq_error("Opal Kelly error"), _code(ec) {}
        char const * what() const throw();
};

class rhd2keval : public daq_interface {

public:
        enum port_id {
                PortA,
                PortB,
                PortC,
                PortD
        };

        enum datasource_id {
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

        rhd2keval(uint sampling_rate, char const * serial=0, char const * firmware=0);
        ~rhd2keval();

        /* daq_interface virtual member functions */

        void start(uint max_frames=0);
        void stop();
        void wait(uint frames);

        uint sampling_rate() { return _sampling_rate; }
        uint adc_nchannels();

        /* rhd2k eval specific */

        int nstreams_enabled();
        void enable_adc_stream(int stream, bool enabled);

        void set_leds(int value, int mask);
        void ttl_out(int value, int mask);
        int ttl_in();

        void dac_monitor_stream(int dac, int stream, int channel);
        void enable_dac_monitor(int dac, bool enabled);

        friend std::ostream & operator<< (std::ostream &, rhd2keval const &);

protected:
        bool dcm_done() const;
        bool clock_locked() const;


private:
        void reset_board();
        void set_sampling_rate();

        okFrontPanel_HANDLE _dev;
        okPLL22393_HANDLE _pll;

        uint _sampling_rate;
        ulong _board_id;
        ulong _board_version;
};

#endif
