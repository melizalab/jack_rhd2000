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
        virtual void start(std::size_t max_frames=0) = 0;

        /** true if data acqusition is in process */
        virtual bool running() = 0;

        /** Stop data collection */
        virtual void stop() = 0;

        /** the number of frames available in the interface's buffer */
        virtual std::size_t nframes_ready() = 0;

        /** the number of bytes per frame */
        virtual std::size_t frame_size() = 0;

        /**
         * Copy data from the interface into memory
         *
         * @param tgt       the target memory. must have at least nframes times
         *                  bytes_per_frame() allocated
         * @param nframes   the requested number of frames.
         *
         * @return the number of frames written to @tgt
         *
         * @note implementing classes may handle underruns (i.e. fewer frames
         * available than requested) as determined by the underlying hardware.
         */
        virtual std::size_t read(void * tgt, std::size_t nframes) = 0;

        /** the current sampling rate */
        virtual std::size_t sampling_rate() const = 0;
        /** the number of active analog input channels */
        virtual std::size_t adc_channels() const = 0;
        /** the number of active analog output channels */
        // virtual std::size_t dac_nchannels() = 0;

};

#endif
