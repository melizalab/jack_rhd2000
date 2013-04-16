#include <ostream>
#include "rhd2keval.hpp"
#include "okFrontPanelDLL.h"

#define MAX_NUM_DATA_STREAMS 8

enum RhythmEndPoints {
        WireInResetRun = 0x00,
        WireInMaxTimeStepLsb = 0x01,
        WireInMaxTimeStepMsb = 0x02,
        WireInDataFreqPll = 0x03,
        WireInMisoDelay = 0x04,
        WireInCmdRamAddr = 0x05,
        WireInCmdRamBank = 0x06,
        WireInCmdRamData = 0x07,
        WireInAuxCmdBank1 = 0x08,
        WireInAuxCmdBank2 = 0x09,
        WireInAuxCmdBank3 = 0x0a,
        WireInAuxCmdLength1 = 0x0b,
        WireInAuxCmdLength2 = 0x0c,
        WireInAuxCmdLength3 = 0x0d,
        WireInAuxCmdLoop1 = 0x0e,
        WireInAuxCmdLoop2 = 0x0f,
        WireInAuxCmdLoop3 = 0x10,
        WireInLedDisplay = 0x11,
        WireInDataStreamSel1234 = 0x12,
        WireInDataStreamSel5678 = 0x13,
        WireInDataStreamEn = 0x14,
        WireInTtlOut = 0x15,
        WireInDacSource1 = 0x16,
        WireInDacSource2 = 0x17,
        WireInDacSource3 = 0x18,
        WireInDacSource4 = 0x19,
        WireInDacSource5 = 0x1a,
        WireInDacSource6 = 0x1b,
        WireInDacSource7 = 0x1c,
        WireInDacSource8 = 0x1d,
        WireInDacManual1 = 0x1e,
        WireInDacManual2 = 0x1f,

        TrigInDcmProg = 0x40,
        TrigInSpiStart = 0x41,
        TrigInRamWrite = 0x42,

        WireOutNumWordsLsb = 0x20,
        WireOutNumWordsMsb = 0x21,
        WireOutSpiRunning = 0x22,
        WireOutTtlIn = 0x23,
        WireOutDataClkLocked = 0x24,
        WireOutBoardId = 0x3e,
        WireOutBoardVersion = 0x3f,

        PipeOutData = 0xa0
};

char const *
ok_error::what() const throw()
{
        static char buf[64];

        switch(_code) {
        case ok_DeviceNotOpen:
                return "device not open";
        case ok_FileError:
                return "FPGA configuration failed: file open error";
        case ok_InvalidBitstream:
                return "FPGA configuration failed: invalid bitstream";
        case ok_DoneNotHigh:
                return "FPGA configuration failed: FPGA DONE signal not received";
        case ok_TransferError:
                return "FPGA configuration failed: USB error occurred during download.";
        case ok_CommunicationError:
                return "FPGA configuration failed: Communication error with firmware.";
        case ok_UnsupportedFeature:
                return "FPGA configuration failed: Unsupported feature.";
        }
        sprintf(buf, "FPGA config failed: err code %d", _code);
        return buf;
}


rhd2keval::rhd2keval(uint sampling_rate, char const * serial, char const * firmware)
        : _dev(0), _pll(okPLL22393_Construct()), _sampling_rate(sampling_rate)
{
        ok_ErrorCode ec;
        if (okFrontPanelDLL_LoadLib(NULL) == false) {
                throw daq_error("Opal Kelly Front Panel DLL not found");
        }

        _dev = okFrontPanel_Construct();
        if (okFrontPanel_GetDeviceCount(_dev) == 0) {
                throw daq_error("No connected Opal Kelly devices");
        }
        if ((ec = okFrontPanel_OpenBySerial(_dev, serial)) != ok_NoError) {
                throw ok_error(ec);
        }

        // configure pll
        okFrontPanel_LoadDefaultPLLConfiguration(_dev);
        _pll = okPLL22393_Construct();
        okFrontPanel_GetEepromPLL22393Configuration(_dev, _pll);

        // load fpga firmware if not already configured
        if (okFrontPanel_IsFrontPanelEnabled(_dev)) {
                okFrontPanel_UpdateWireOuts(_dev);
                _board_id = okFrontPanel_GetWireOutValue(_dev, WireOutBoardId);
        }
        if (_board_id != RHYTHM_BOARD_ID) {
                // load the bitfile
                if (!firmware) firmware = "rhythm_130302.bit";
                if ((ec = okFrontPanel_ConfigureFPGA(_dev, firmware)) != ok_NoError) {
                        throw ok_error(ec);
                }
                okFrontPanel_UpdateWireOuts(_dev);
                _board_id = okFrontPanel_GetWireOutValue(_dev, WireOutBoardId);
        }
        _board_version = okFrontPanel_GetWireOutValue(_dev, WireOutBoardVersion);

        reset_board();
        set_sampling_rate();
}


rhd2keval::~rhd2keval()
{
        if (_pll) okPLL22393_Destruct(_pll);
        if (_dev) okFrontPanel_Destruct(_dev);
}

void
rhd2keval::start(uint max_frames)
{}

void
rhd2keval::stop()
{}


void
rhd2keval::wait(uint frames)
{}

uint
rhd2keval::adc_nchannels() { return 0;}

void
rhd2keval::reset_board()
{
        // reset
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, 0x0001, 0x0001);
        okFrontPanel_UpdateWireIns(_dev);
        // turn off reset, set some values
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, 0x0000, 0xffff);
        // SPI run continuous [bit 1]
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, 1 << 1, 1 << 1);
        // DSP settle [bit 2] = 1
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, 1 << 2, 1 << 2);
        // dac noise slice [12:6] = 0
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, 0 << 6, 0x1fc0);
        // dac gain [15:13] = 2^0
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, 0 << 13, 0xe000);
        okFrontPanel_UpdateWireIns(_dev);

        // wire amps to data streams
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel1234, PortA1 << 0, 0x0f << 0);
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel1234, PortA2 << 4, 0x0f << 4);
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel1234, PortB1 << 8, 0x0f << 8);
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel1234, PortB2 << 12, 0x0f << 12);
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel5678, PortC1 << 0, 0x0f << 0);
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel5678, PortC2 << 4, 0x0f << 4);
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel5678, PortD1 << 8, 0x0f << 8);
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel5678, PortD2 << 12, 0x0f << 12);
        // enable all data streams
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamEn, 0x000f , 0x000f);
        okFrontPanel_UpdateWireIns(_dev);

        // shut off pipes from input to dacs
        for (int i = 0; i < 8; ++i) {
                okFrontPanel_SetWireInValue(_dev, int(WireInDacSource1) + i, 0x0000, 0xffff);
        }
        // set manual values to 0 (mid-range)
        okFrontPanel_SetWireInValue(_dev, WireInDacManual1, 0x00ef, 0xffff);
        okFrontPanel_SetWireInValue(_dev, WireInDacManual2, 0x00ef, 0xffff);
        // TTL output zeroed
        okFrontPanel_SetWireInValue(_dev, WireInTtlOut, 0x0000, 0xffff);
        // set LED
        okFrontPanel_SetWireInValue(_dev, WireInLedDisplay, 0x0001, 0xffff);

        okFrontPanel_UpdateWireIns(_dev);

}

void
rhd2keval::set_sampling_rate()
{
        /* from the intan docs
         * Assuming a 100 MHz reference clock is provided to the FPGA, the programmable FPGA clock frequency
         * is given by:
         *
         *       FPGA internal clock frequency = 100 MHz * (M/D) / 2
         *
         * M and D are "multiply" and "divide" integers used in the FPGA's digital clock manager (DCM) phase-
         * locked loop (PLL) frequency synthesizer, and are subject to the following restrictions:
         *
         *                M must have a value in the range of 2 - 256
         *                D must have a value in the range of 1 - 256
         *                M/D must fall in the range of 0.05 - 3.33
         *
         * (See pages 85-86 of Xilinx document UG382 "Spartan-6 FPGA Clocking Resources" for more details.)
         *
         * This variable-frequency clock drives the state machine that controls all SPI communication
         * with the RHD2000 chips.  A complete SPI cycle (consisting of one CS pulse and 16 SCLK pulses)
         * takes 80 clock cycles.  The SCLK period is 4 clock cycles; the CS pulse is high for 14 clock
         * cycles between commands.
         *
         * Rhythm samples all 32 channels and then executes 3 "auxiliary" commands that can be used to read
         * and write from other registers on the chip, or to sample from the temperature sensor or auxiliary ADC
         * inputs, for example.  Therefore, a complete cycle that samples from each amplifier channel takes
         * 80 * (32 + 3) = 80 * 35 = 2800 clock cycles.
         *
         * So the per-channel sampling rate of each amplifier is 2800 times slower than the clock frequency.
         *
         * Based on these design choices, we can use the following values of M and D to generate the following
         * useful amplifier sampling rates for electrophsyiological applications:
         *
         *   M    D     clkout frequency    per-channel sample rate     per-channel sample period
         *  ---  ---    ----------------    -----------------------     -------------------------
         *    7  125          2.80 MHz               1.00 kS/s                 1000.0 usec = 1.0 msec
         *    7  100          3.50 MHz               1.25 kS/s                  800.0 usec
         *   21  250          4.20 MHz               1.50 kS/s                  666.7 usec
         *   14  125          5.60 MHz               2.00 kS/s                  500.0 usec
         *   35  250          7.00 MHz               2.50 kS/s                  400.0 usec
         *   21  125          8.40 MHz               3.00 kS/s                  333.3 usec
         *   14   75          9.33 MHz               3.33 kS/s                  300.0 usec
         *   28  125         11.20 MHz               4.00 kS/s                  250.0 usec
         *    7   25         14.00 MHz               5.00 kS/s                  200.0 usec
         *    7   20         17.50 MHz               6.25 kS/s                  160.0 usec
         *  112  250         22.40 MHz               8.00 kS/s                  125.0 usec
         *   14   25         28.00 MHz              10.00 kS/s                  100.0 usec
         *    7   10         35.00 MHz              12.50 kS/s                   80.0 usec
         *   21   25         42.00 MHz              15.00 kS/s                   66.7 usec
         *   28   25         56.00 MHz              20.00 kS/s                   50.0 usec
         *   35   25         70.00 MHz              25.00 kS/s                   40.0 usec
         *   42   25         84.00 MHz              30.00 kS/s                   33.3 usec
         *
         * To set a new clock frequency, assert new values for M and D (e.g., using okWireIn modules) and
         * pulse DCM_prog_trigger high (e.g., using an okTriggerIn module).  If this module is reset, it
         * reverts to a per-channel sampling rate of 30.0 kS/s.
         */

        ulong M, D;
        if (_sampling_rate < 1125) { // 1000 Hz
                M = 7;
                D = 125;
        } else if (_sampling_rate < 1375) { // 1250 Hz
                M = 7;
                D = 100;
        } else if (_sampling_rate < 1750) { // 1500 Hz
                M = 21;
                D = 250;
        } else if (_sampling_rate < 2250) { // 2000 Hz
                M = 14;
                D = 125;
        } else if (_sampling_rate < 2750) { // 2500
                M = 35;
                D = 250;
        }
        // I've disabled 3333 Hz because it's not an integral # of samples / sec
        else if (_sampling_rate < 3500) { // 3000
                M = 21;
                D = 125;
        } else if (_sampling_rate < 4500) { // 4000
                M = 28;
                D = 125;
        } else if (_sampling_rate < 5265) { // 5000
                M = 7;
                D = 25;
        } else if (_sampling_rate < 7125) { // 6250
                M = 7;
                D = 20;
        } else if (_sampling_rate < 9000) { // 8000
                M = 112;
                D = 250;
        } else if (_sampling_rate < 11250) { // 10k
                M = 14;
                D = 25;
        } else if (_sampling_rate < 13750) { // 12.5k
                M = 7;
                D = 10;
        } else if (_sampling_rate < 11250) { // 15k
                M = 21;
                D = 25;
        } else if (_sampling_rate < 22500) { // 20k
                M = 28;
                D = 25;
        } else if (_sampling_rate < 27500) { // 25k
                M = 35;
                D = 25;
        } else { // 30k
                M = 42;
                D = 25;
        }
        _sampling_rate = 1e5 * M / D / 2 / 2.8;

        // Wait for DcmProgDone = 1 before reprogramming clock synthesizer
        while (!dcm_done()) {}

        // Reprogram clock synthesizer
        okFrontPanel_SetWireInValue(_dev, WireInDataFreqPll, (256 * M + D), 0xffff);
        okFrontPanel_UpdateWireIns(_dev);
        okFrontPanel_ActivateTriggerIn(_dev, TrigInDcmProg, 0);

        // Wait for DataClkLocked = 1 before allowing data acquisition to continue
        while (!clock_locked()) {}
}

bool
rhd2keval::dcm_done() const
{
        ulong value;
        okFrontPanel_UpdateWireOuts(_dev);
        value = okFrontPanel_GetWireOutValue(_dev, WireOutDataClkLocked);
        return ((value & 0x0002) > 1);
}

bool
rhd2keval::clock_locked() const
{
        ulong value;
        okFrontPanel_UpdateWireOuts(_dev);
        value = okFrontPanel_GetWireOutValue(_dev, WireOutDataClkLocked);
        return ((value & 0x0001) > 0);
}

std::ostream &
operator<< (std::ostream & o, rhd2keval const & r)
{
        char buf1[256], buf2[256];

        okFrontPanelDLL_GetVersion(buf1, buf2);
        o << "RHD2000 Controller:"
          << "\n Opal Kelly Front Panel version: " << buf1 << " " << buf2;
        okFrontPanel_GetSerialNumber(r._dev, buf1);
        okFrontPanel_GetDeviceID(r._dev, buf2);
        o << "\n Opal Kelly device ID: " << buf2
          << "\n Opal Kelly device serial number: " << buf1
          << "\n Opal Kelly device firmware version: " << okFrontPanel_GetDeviceMajorVersion(r._dev)
          << '.' << okFrontPanel_GetDeviceMinorVersion(r._dev)
          << "\n FPGA frequency: " << okPLL22393_GetOutputFrequency(r._pll, 0) << " MHz"
          << "\n Rhythm version: " << r._board_version
          << "\n Sampling rate: " << r._sampling_rate << " Hz";
        return o;
}
