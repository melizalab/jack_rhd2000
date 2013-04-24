
#include <memory.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>

#include "rhd2000eval.hpp"

#include <jack/types.h>
#include <jack/jslist.h>
#include <jack/jack.h>

extern "C"
{
#include "driver.h"
#include "internal.h"
#include "engine.h"
}

using namespace rhd2k;

struct rhd2k_driver_t {
        JACK_DRIVER_NT_DECL;

        evalboard * dev;

	jack_nframes_t  period_size;

	jack_client_t  * client;
        JSList * playback_ports;
};

struct rhd2k_amp_settings_t {
        unsigned long amp_power;
        double lowpass;
        double highpass;
        double dsp;
        double cable_ft;
};

struct rhd2k_jack_settings_t {
        jack_nframes_t period_size;
        jack_nframes_t sample_rate;

        jack_nframes_t capture_frame_latency;

        rhd2k_amp_settings_t amplifiers[evalboard::nports];
        long eval_adc_enabled;

};

static char const * default_firmware = "rhythm_130302.bit";

static const rhd2k_amp_settings_t default_amp_config = {0xffffffff, 100, 3000, 1, 3};
static const rhd2k_jack_settings_t default_settings = {1024U, 30000U, 0U,
                                                       {default_amp_config,
                                                        default_amp_config,
                                                        default_amp_config,
                                                        default_amp_config},
                                                       0};

extern const char driver_client_name[] = "rhd2000_pcm";

static void
parse_port_config(char pchar, char const * arg, rhd2k_jack_settings_t & s)
{
        rhd2k_amp_settings_t * pptr;
        evalboard::port_id port;

        switch(pchar) {
        case 'A':
                port = evalboard::PortA;
                break;
        case 'B':
                port = evalboard::PortB;
                break;
        case 'C':
                port = evalboard::PortB;
                break;
        case 'D':
                port = evalboard::PortB;
                break;
        default:
                return;
        }
        pptr = &s.amplifiers[(size_t)port];
        // should really do some more validation
        sscanf(arg, "%li,%lf,%lf,%lf,%lf", &pptr->amp_power, &pptr->lowpass,
                       &pptr->highpass, &pptr->dsp, &pptr->cable_ft);
}

static int
rhd2k_driver_attach (rhd2k_driver_t *driver)
{}

static int
rhd2k_driver_detach (rhd2k_driver_t *driver)
{}

static int
rhd2k_driver_read (rhd2k_driver_t * driver, jack_nframes_t nframes)
{}

static jack_nframes_t
rhd2k_driver_wait (rhd2k_driver_t *driver, int extra_fd, int *status,
		   float *delayed_usecs)
{}

static int
rhd2k_driver_run_cycle (rhd2k_driver_t *driver)
{}

static int
rhd2k_driver_null_cycle (rhd2k_driver_t* driver, jack_nframes_t nframes)
{}

static int
rhd2k_driver_start (rhd2k_driver_t *driver)
{}

static int
rhd2k_driver_stop (rhd2k_driver_t *driver)
{}

static int
rhd2k_driver_bufsize (rhd2k_driver_t* driver, jack_nframes_t nframes)
{}

static rhd2k_driver_t *
rhd2k_driver_new(jack_client_t * client, char const * serial, char const * firmware,
                 rhd2k_jack_settings_t &settings)
{
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
	driver->nt_bufsize   = (JackDriverNTBufSizeFunction)  rhd2k_driver_bufsize;

	driver->client = client;
	driver->engine = NULL;

	driver->period_size = settings.period_size;

        try {
                driver->dev = new evalboard(settings.sample_rate, serial, firmware);
                // configure ports
                for (std::size_t i = 0; i < evalboard::nports; ++i) {
                        rhd2k_amp_settings_t * a = &settings.amplifiers[i];
                        driver->dev->configure_port((evalboard::port_id)i, a->lowpass, a->highpass, a->dsp, a->amp_power);
                }
                // scan ports
                driver->dev->scan_ports();
                driver->period_usecs =
                        (jack_time_t) floor ((((float) driver->period_size) * 1000000.0f) / driver->dev->sampling_rate());

                std::cout << *driver->dev
                          << "\nperiod = " << driver->period_size
                          << " frames (" << (driver->period_usecs / 1000.0f) << " ms)" << std::endl;
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
	desc->nparams = 6 + evalboard::nports;
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

        param++;
        strcpy(param->name, "port-A");
        param->character = 'A';
        param->type = JackDriverParamString;
	strcpy(param->value.str,  "0xffffffff,100,3000,1,3.0");
        strcpy(param->short_desc, "configure port A");
        strcpy(param->long_desc, "configure port A: channels[,lopass[,hipass[,dsp-hipass[,cable-length]]]]");

        for (std::size_t p = 1; p < evalboard::nports; ++p) {
                param++;
                param->character = 'A' + p;
                sprintf(param->name, "port-%c", param->character);
                strcpy(param->value.str, "0");
                sprintf(param->short_desc, "configure port %c", param->character);
                sprintf(param->long_desc,
                        "configure port %c: channels[,lopass[,hipass[,dsp-hipass[,cable-length]]]]",
                        param->character);
        }

        param++;
        strcpy(param->name, "port-eval");
        param->character = 'X';
        param->type = JackDriverParamUInt;
	param->value.ui =  default_settings.eval_adc_enabled;
        strcpy(param->short_desc, "configure eval board ADC");
        strcpy(param->long_desc, "configure eval board ADC: channels (8-bit mask)");

        param++;
        strcpy(param->name, "input-latency");
        param->character = 'I';
        param->type = JackDriverParamUInt;
        param->value.ui = default_settings.capture_frame_latency;
        strcpy(param->short_desc, "extra input latency (frames) ");
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
                        cmlparams.eval_adc_enabled = param->value.ui;
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