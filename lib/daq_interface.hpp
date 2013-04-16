#ifndef _DAQ_INTERFACE_H
#define _DAQ_INTERFACE_H

#include <stdexcept>
#include <boost/noncopyable.hpp>

typedef unsigned int uint;
typedef unsigned long ulong;

struct daq_error : public std::runtime_error {
        daq_error(std::string const & w) : std::runtime_error(w) {}
        // error codes?
};

/**
 * ABC for a simple data acquisition system. The system may have any number of
 * input and output channels. This specifies methods for starting and stopping
 * the acquisition system and querying its capabilities.
 */
class daq_interface : boost::noncopyable {

public:

        virtual ~daq_interface() {}

        /**
         * Start data collection.
         *
         * @pre the interface is not already running
         *
         * @param max_frames   the number of frames of data to collect, or 0 to
         *                     run continuously
         */
        virtual void start(uint max_frames=0) = 0;

        /** Stop data collection */
        virtual void stop() = 0;

        /**
         * Wait until there are at least @a frames of data collected.
         *
         * @pre the interface is runing
         */
        virtual void wait(uint frames) = 0;

        // virtual void * read(uint frames) = 0;


        /** the current sampling rate */
        virtual uint sampling_rate() = 0;
        /** the number of active analog input channels */
        virtual uint adc_nchannels() = 0;
        /** the number of active analog output channels */
        // virtual uint dac_nchannels() = 0;

};

#endif
