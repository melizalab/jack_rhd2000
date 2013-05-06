
#include <memory.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include <sstream>
#include <list>

#include "rhd2000eval.hpp"
#include "rhd2k.hpp"

#include <jack/types.h>
#include <jack/jslist.h>
#include <jack/jack.h>

extern "C"
{
#include "driver.h"
#include "internal.h"
#include "engine.h"
}

using std::size_t;
using std::string;
using namespace rhd2k;

struct rhd2k_driver_t {
        JACK_DRIVER_NT_DECL;

        evalboard * dev;
        void * buffer;
        uint32_t last_frame;    // the timestamp in the RHD data stream

	jack_nframes_t  period_size;
        jack_nframes_t  fifo_latency; // extra fifo buffering, in frames

	jack_client_t  * client;
        std::vector<jack_port_t*> capture_ports;
        long eval_adc_enabled;
};

struct rhd2k_amp_settings_t {
        unsigned long amp_power;
        double lowpass;
        double highpass;
        double dsp;
        double cable_m;
};

struct rhd2k_jack_settings_t {
        jack_nframes_t period_size;
        jack_nframes_t sample_rate;

        jack_nframes_t capture_frame_latency;

        rhd2k_amp_settings_t amplifiers[evalboard::nmosi];
        long eval_adc_enabled;  // 8 bit mask

};

static char const * default_firmware = "rhythm_130302.bit";

static const rhd2k_amp_settings_t default_amp_config = {0xffffffff, 100, 3000, 1, 0};
static const rhd2k_jack_settings_t default_settings = {1024U, 30000U, 0U,
                                                       {default_amp_config,
                                                        default_amp_config,
                                                        default_amp_config,
                                                        default_amp_config},
                                                       0};

extern const char driver_client_name[] = "rhd2000";

static void
parse_port_config(char pchar, char const * arg, rhd2k_jack_settings_t & s)
{
        rhd2k_amp_settings_t * pptr;
        evalboard::mosi_id port;

        switch(pchar) {
        case 'A':
                port = evalboard::PortA;
                break;
        case 'B':
                port = evalboard::PortB;
                break;
        case 'C':
                port = evalboard::PortC;
                break;
        case 'D':
                port = evalboard::PortD;
                break;
        default:
                return;
        }
        pptr = &s.amplifiers[(size_t)port];
        // should really do some more validation
        sscanf(arg, "%li,%lf,%lf,%lf,%lf", &pptr->amp_power, &pptr->lowpass,
               &pptr->highpass, &pptr->dsp, &pptr->cable_m);
}

static void
rhd2k_latency_callback (jack_latency_callback_mode_t mode, void* arg)
{
        rhd2k_driver_t* driver = (rhd2k_driver_t*) arg;
        jack_latency_range_t range;

        // TODO get upper range of latency by polling FIFO at times
        if (mode == JackCaptureLatency) {
                range.min = range.max = driver->period_size + driver->fifo_latency;
        }

        std::vector<jack_port_t*>::const_iterator it;
	for (it = driver->capture_ports.begin(); it != driver->capture_ports.end(); ++it) {
                jack_port_set_latency_range (*it, mode, &range);
	}
}


static int
rhd2k_driver_attach (rhd2k_driver_t *driver)
{
#ifndef NDEBUG
        jack_info("RHD2K: attaching driver");
#endif
	jack_port_t *port = 0;
        // inform the engine of sample rate and buffer size
	if (driver->engine->set_buffer_size (driver->engine, driver->period_size)) {
		jack_error ("RHD2K: cannot set engine buffer size to %d", driver->period_size);
		return -1;
	}
        driver->engine->set_sample_rate (driver->engine, driver->dev->sampling_rate());

        // allocate scratch buffer
        driver->buffer = malloc(driver->dev->frame_size() * driver->period_size);
        if (driver->buffer == 0) {
                jack_error ("RHD2K: unable to allocate buffer");
                return -1;
        }
#ifndef NDEBUG
        jack_info("RHD2K: scratch buffer size=%ld bytes", driver->dev->frame_size() * driver->period_size);
#endif

        // create ports
        std::vector<evalboard::channel_info_t>::const_iterator it;
        for (it = driver->dev->adc_table().begin(); it != driver->dev->adc_table().end(); ++it) {
                int port_flags = JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal;
                char const * name = it->name.c_str();
                // filter eval board ADC channels
                if (it->stream == evalboard::EvalADC) {
                        if (!(driver->eval_adc_enabled & 1 << it->channel)) continue;
                }
                else {
                        port_flags |= JackPortCanMonitor;
                }
#ifndef NDEBUG
                jack_info("RHD2K: registering capture port %s (offset = %zd)", name, it->byte_offset);
#endif
                if ((port = jack_port_register (driver->client, name,
                                                JACK_DEFAULT_AUDIO_TYPE,
                                                port_flags, 0)) == NULL) {
                        jack_error ("RHD2K: cannot register port for %s", name);
                        break;
                }
                driver->capture_ports.push_back(port);
        }

	return jack_activate (driver->client);

}

static int
rhd2k_driver_detach (rhd2k_driver_t *driver)
{
#ifndef NDEBUG
        jack_info("RHD2K: detaching driver");
#endif
	if (driver->engine == NULL) {
		return 0;
	}

        std::vector<jack_port_t*>::const_iterator it;
	for (it = driver->capture_ports.begin(); it != driver->capture_ports.end(); ++it) {
#ifndef NDEBUG
                jack_info("RHD2K: unregistering capture port %s", jack_port_name(*it));
#endif
                jack_port_unregister (driver->client, *it);
	}
        driver->capture_ports.clear();

        // release scratch buffer
        free(driver->buffer);

        return 0;
}

static int
rhd2k_driver_start (rhd2k_driver_t *driver)
{
#ifndef NDEBUG
        jack_info("RHD2K: starting acquisition");
#endif
        // flush FIFO
        while (driver->dev->nframes()) {
                driver->dev->read (driver->buffer, driver->period_size);
        }
        driver->dev->start();
        if (!driver->dev->running()) {
                jack_error("RHD2K: failed to start acquisition");
                return -1;
        }
        // add any additional latency to the fifo
        usleep(driver->fifo_latency * 1e6 / driver->dev->sampling_rate());
        driver->last_wait_ust = driver->engine->get_microseconds();
        driver->last_frame = 0U;
        return 0;
}

static int
rhd2k_driver_stop (rhd2k_driver_t *driver)
{
#ifndef NDEBUG
        jack_info("RHD2K: stopping acquisition");
#endif
        // TODO silence output ports
        driver->dev->stop();
        if (driver->dev->running()) {
                jack_error("RHD2K: failed to stop acquisition");
                return -1;
        }
        return 0;
}

/* this function copies data from the scratch buffer into the port buffers */
static int
rhd2k_driver_read (rhd2k_driver_t * driver, jack_nframes_t nframes)
{
        evalboard::data_type * p;
        if (driver->engine->freewheeling) {
                return 0;
        }

        // the port list
        std::vector<evalboard::channel_info_t>::const_iterator chan = driver->dev->adc_table().begin();
        std::vector<jack_port_t*>::const_iterator port = driver->capture_ports.begin();
	for (; port != driver->capture_ports.end(); ++port, ++chan) {
                jack_default_audio_sample_t * buf =
                        reinterpret_cast<jack_default_audio_sample_t *>(jack_port_get_buffer (*port, nframes));
                int nconnections = jack_port_connected (*port);
                if (nconnections) {
                        // copy the data, converting to floats
                        for (jack_nframes_t t = 0; t < nframes; ++t) {
                                p = reinterpret_cast<evalboard::data_type*>(
                                        (char*)driver->buffer + chan->byte_offset + t * driver->dev->frame_size());
                                buf[t] = *p / evalboard::data_type_max;
                        }
                }
                else {
                        // silence the port buffer
                        memset(buf, 0, nframes * sizeof(jack_default_audio_sample_t));
                }
        }
        return 0;
}

/* this function sets up hardware monitoring. no data is actually copied */
static int
rhd2k_driver_write (rhd2k_driver_t * driver, jack_nframes_t nframes)
{
        if (driver->engine->freewheeling) {
                return 0;
        }

        // the port list
        const size_t available_dacs = driver->dev->dac_nchannels();
        size_t dac = 0;
        std::vector<jack_port_t*>::const_iterator port = driver->capture_ports.begin();
	for (size_t chan = 0; port != driver->capture_ports.end() && dac < available_dacs; ++port, ++chan) {
                if (jack_port_monitoring_input(*port)) {
                        driver->dev->dac_monitor(dac++, chan);
                }
        }
        while (dac < available_dacs) {
                driver->dev->dac_disable(dac++);
        }
        return 0;
}

static int
rhd2k_driver_run_cycle (rhd2k_driver_t *driver)
{
        uint64_t const * magic;
        uint32_t const * timestamp;
	jack_engine_t *engine = driver->engine;
        jack_time_t wait_enter;

        /* trust, but verify. the opal kelly fpga should be deterministic  */
        driver->last_wait_ust += driver->period_usecs;
        wait_enter = engine->get_microseconds();

        // wait long enough to ensure enough data is in the FIFO
        if (wait_enter < driver->last_wait_ust) {
                usleep(driver->last_wait_ust - wait_enter);
        }
        else {
                // overrun due to delay in previous cycle - need to flush some data?
                driver->last_wait_ust = wait_enter;
        }
        engine->transport_cycle_start (engine, driver->last_wait_ust);

        // read the data. this is relatively slow and eats into the process cycle
        if (driver->dev->read (driver->buffer, driver->period_size) == 0) {
                jack_error ("RHD2K: fatal error reading data from device");
                return -1;
        }

#if DEBUG_CYCLE
        timestamp = (uint32_t const *)((char*)driver->buffer + sizeof(uint64_t));
        std::cerr << "frame " << *timestamp << ": time=" << driver->last_wait_ust
                  << ", fifo=" << driver->dev->nframes() << std::endl;
#endif

        // verify that the last frame is correct
        magic = (uint64_t const *)((char*)driver->buffer + driver->dev->frame_size() * (driver->period_size - 1));
        timestamp = (uint32_t const *)(magic + 1);
        if (*magic == evalboard::frame_header && *timestamp == (driver->last_frame + driver->period_size - 1)) {
                driver->last_frame += driver->period_size;
                return engine->run_cycle(engine, driver->period_size, 0.0);
        }
        // deal with errors:
        // is the device running? - may have been disconnected
        else if (!driver->dev->running()) {
                jack_error ("RHD2K: device is not running or was disconnected");
                return -1;
        }
        else {
                // attempting to read an underfull FIFO corrupts it. could try
                // to recover by finding the next valid frame, but it's simpler
                // to just restart acquisition. the xruns will be fairly large
                // with this method
                float delayed_usecs = -1.0f * driver->last_wait_ust;
                rhd2k_driver_stop(driver);
                rhd2k_driver_start(driver); // sets new ust
                delayed_usecs += driver->last_wait_ust;
                jack_error("xrun of %.3f usec", delayed_usecs);
                engine->delay (engine, delayed_usecs);
                return 0;
        }

}

static int
rhd2k_driver_null_cycle (rhd2k_driver_t* driver, jack_nframes_t nframes)
{
#ifndef NDEBUG
        jack_info("RHD2K: null cycle");
#endif
        /* null cycle - read and discard input data */
	if (driver->engine->freewheeling) {
		return 0;
	}

        driver->dev->read (driver->buffer, driver->period_size);
        return 0;
}

static int
rhd2k_driver_bufsize (rhd2k_driver_t* driver, jack_nframes_t nframes)
{
#ifndef NDEBUG
        jack_info("RHD2K: resizing buffer to %ld", nframes);
#endif

        // update variables
	driver->period_size = nframes;
        driver->period_usecs =
                (jack_time_t) floor ((((float) driver->period_size) * 1000000.0f) / driver->dev->sampling_rate());
	if (driver->engine->set_buffer_size (driver->engine, nframes)) {
		jack_error ("RHD2K: cannot set engine buffer size to %d", nframes);
		return -1;
	}

        // realloc buffer
        free (driver->buffer);
        driver->buffer = malloc (driver->dev->frame_size() * driver->period_size);
        if (driver->buffer == 0) {
                jack_error ("RHD2K: unable to allocate buffer");
                return -1;
        }

        return 0;
}

static rhd2k_driver_t *
rhd2k_driver_new(jack_client_t * client, char const * serial, char const * firmware,
                 rhd2k_jack_settings_t &settings)
{
#ifndef NDEBUG
        jack_info("RHD2K: initializing driver");
#endif
        rhd2k_driver_t * driver = new rhd2k_driver_t;

	/* Setup the jack interfaces */
	jack_driver_nt_init ((jack_driver_nt_t *) driver);

	driver->nt_attach    = (JackDriverNTAttachFunction)   rhd2k_driver_attach;
	driver->nt_detach    = (JackDriverNTDetachFunction)   rhd2k_driver_detach;
	driver->nt_start     = (JackDriverNTStartFunction)    rhd2k_driver_start;
	driver->nt_stop      = (JackDriverNTStopFunction)     rhd2k_driver_stop;
	driver->nt_run_cycle = (JackDriverNTRunCycleFunction) rhd2k_driver_run_cycle;
	driver->null_cycle   = (JackDriverNullCycleFunction)  rhd2k_driver_null_cycle;
	driver->read         = (JackDriverReadFunction)       rhd2k_driver_read;
	driver->write        = (JackDriverWriteFunction)      rhd2k_driver_write;
	driver->nt_bufsize   = (JackDriverNTBufSizeFunction)  rhd2k_driver_bufsize;

	driver->client = client;
	driver->engine = NULL;
	driver->period_size = settings.period_size;
        driver->eval_adc_enabled = settings.eval_adc_enabled;
	driver->last_wait_ust = 0;

        jack_set_latency_callback (client, rhd2k_latency_callback, driver);

        try {
                driver->dev = new evalboard(settings.sample_rate, serial, firmware);
                // configure ports
                for (size_t i = 0; i < evalboard::nmosi; ++i) {
                        rhd2k_amp_settings_t * a = &settings.amplifiers[i];
                        driver->dev->configure_port((evalboard::mosi_id)i, a->lowpass, a->highpass, a->dsp, a->amp_power);
                }
                // scan ports
                jack_info("RHD2K: scanning SPI ports");
                driver->dev->scan_ports();
                // disable streams where the user sets power off to all amps -
                // note that it's not possible to only enable one MISO line on a
                // port (a rare unsupported use case)
                for (size_t i = 0; i < evalboard::nmosi; ++i) {
                        rhd2k_amp_settings_t * a = &settings.amplifiers[i];
                        if (a->amp_power == 0x0) {
                                driver->dev->enable_stream(evalboard::miso_id(i*2), false);
                                driver->dev->enable_stream(evalboard::miso_id(i*2+1), false);
                        }
                        // manually specified cable length
                        if (a->cable_m > 0) {
                                driver->dev->set_cable_meters((evalboard::mosi_id)i, a->cable_m);
                        }
                }

                driver->period_usecs =
                        (jack_time_t) floor ((((float) driver->period_size) * 1000000.0f) / driver->dev->sampling_rate());
                driver->fifo_latency = settings.capture_frame_latency;

                std::cout << *driver->dev
                          << "\nperiod = " << driver->period_size
                          << " frames (" << (driver->period_usecs / 1000.0f) << " ms)"
                          << "\nFIFO buffering = " << settings.capture_frame_latency
                          << " frames (" << (driver->fifo_latency * 1e3f / driver->dev->sampling_rate()) << " ms)" << std::endl;
                return driver;
        }
        catch (std::runtime_error const & e) {
                jack_error("fatal error: %s", e.what());
        }
        if (driver->dev) delete driver->dev;
        delete driver;
        return 0;
}

static void
rhd2k_driver_delete(rhd2k_driver_t * driver)
{
#ifndef NDEBUG
        jack_info("RHD2K: deleting driver");
#endif
        if (driver == 0) return;
        jack_driver_nt_finish ((jack_driver_nt_t *) driver);
        if (driver->dev) delete driver->dev;
        delete driver;
}

extern "C"
{

const jack_driver_desc_t *
driver_get_descriptor ()
{
        jack_driver_desc_t * desc;
	jack_driver_param_desc_t * param;

	desc = (jack_driver_desc_t *) calloc (1, sizeof (jack_driver_desc_t));
	strcpy (desc->name, "rhd2000");
	desc->nparams = 6 + evalboard::nmosi;
	desc->params = (jack_driver_param_desc_t *) calloc (desc->nparams,
                                                                    sizeof (jack_driver_param_desc_t));
        param = desc->params;
	strcpy (param->name, "device");
	param->character  = 'd';
	param->type       = JackDriverParamString;
	strcpy (param->value.str,  "first connected device");
	strcpy (param->short_desc, "serial number of RHD2000 Opal Kelly");

        param++;
        strcpy(param->name, "firmware");
        param->character = 'F';
	param->type       = JackDriverParamString;
        strcpy(param->value.str, default_firmware);
        strcpy(param->short_desc, "firmware file for RHD2000 eval board");
        strcpy(param->long_desc, param->short_desc);

        param++;
        strcpy(param->name, "rate");
        param->character = 'r';
        param->type = JackDriverParamUInt;
        param->value.ui = default_settings.sample_rate;
        strcpy(param->short_desc, "sampling rate (Hz) ");
        strcpy(param->long_desc, param->short_desc);

        param++;
        strcpy(param->name, "period");
        param->character = 'p';
        param->type = JackDriverParamUInt;
        param->value.ui = default_settings.period_size;
        strcpy(param->short_desc, "frames per period");
        strcpy(param->long_desc, param->short_desc);

        for (size_t p = 0; p < evalboard::nmosi; ++p) {
                param++;
                param->character = 'A' + p;
                sprintf(param->name, "port-%c", param->character);
                param->type = JackDriverParamString;
                strcpy(param->value.str,  "0xffffffff,100,3000,1,0");
                sprintf(param->short_desc, "configure port %c", param->character);
                sprintf(param->long_desc,
                        "configure port %c: channels[,lopass[,hipass[,dsp-hipass[,cable-meters]]]]",
                        param->character);
        }

        param++;
        strcpy(param->name, "port-eval");
        param->character = 'X';
        param->type = JackDriverParamString;
	sprintf(param->value.str,"%lx", default_settings.eval_adc_enabled);
        strcpy(param->short_desc, "configure eval board ADC");
        strcpy(param->long_desc, "configure eval board ADC: channels (8-bit mask)");

        param++;
        strcpy(param->name, "input-latency");
        param->character = 'I';
        param->type = JackDriverParamUInt;
        param->value.ui = default_settings.capture_frame_latency;
        strcpy(param->short_desc, "extra fifo latency (frames) ");
        strcpy(param->long_desc, param->short_desc);

        return desc;
}

jack_driver_t *
driver_initialize(jack_client_t * client, JSList * params)
{
	jack_driver_t *driver;
        JSList const * node;
        jack_driver_param_t const * param;

        rhd2k_jack_settings_t cmlparams;
        memcpy(&cmlparams, &default_settings, sizeof(cmlparams));

        char const * dev_serial = 0;
        char const * firmware = default_firmware;

        for (node = params; node; node = jack_slist_next (node)) {
                param = (jack_driver_param_t *) node->data;

                switch (param->character) {
                case 'd':
                        dev_serial = param->value.str;
                        break;
                case 'F':
                        firmware = param->value.str;
                        break;
                case 'r':
                        cmlparams.sample_rate = param->value.ui;
                        break;
                case 'p':
                        cmlparams.period_size = param->value.ui;
                        break;
                case 'X':
                        sscanf(param->value.str, "%li", &cmlparams.eval_adc_enabled);
                        break;
                case 'I':
                        cmlparams.capture_frame_latency = param->value.ui;
                        break;
                default:        // any other valid option refers to a port
                        parse_port_config(param->character, param->value.str, cmlparams);
                }
        }

        driver = (jack_driver_t*) rhd2k_driver_new(client, dev_serial, firmware, cmlparams);

        return driver;
}

void
driver_finish (jack_driver_t *driver)
{
	rhd2k_driver_t * drv = (rhd2k_driver_t *) driver;
	// If jack hasn't called the detach method, do it now.  As of jack 0.101.1
	// the detach method was not being called explicitly on closedown, and
	// we need it to at least deallocate the iso resources.
	if (drv->dev != NULL)
		rhd2k_driver_detach(drv);
	rhd2k_driver_delete (drv);
}


}
