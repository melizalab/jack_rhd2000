#include <cstdio>
#include <math.h>
#include <iostream>
#include "rhd2000eval.hpp"
#include "rhd2k.hpp"
#include "okFrontPanelDLL.h"

#define MAX_NUM_DATA_STREAMS 8
static const ulong ulong_mask = 0xffffffff;

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

template <typename T>
static void
print_channel(void const * data, size_t nframes, size_t offset, size_t stride)
{
        T const * ptr;
        offset /= sizeof(T);
        stride /= sizeof(T);
        std::cout << offset << ":" << std::hex;
        for (size_t i = 0; i < nframes; ++i) {
                ptr = static_cast<T const *>(data) + offset + stride*i;
                std::cout << " 0x" << *ptr;
                // printf("%zd: %#hx\n", i, ptr[stride*i]);
        }
        std::cout << std::dec << std::endl;
}


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

rhd2000eval::rhd2000eval(uint sampling_rate, char const * serial, char const * firmware)
        : _dev(0), _pll(okPLL22393_Construct()), _sampling_rate(sampling_rate),
          _board_version(0), _nactive_streams(0)
{
        // allocate storage for the amplifier wrappers. the first amplifier does
        // double duty for setting output
        for (std::size_t i = 0; i < ninputs; ++i) {
                _amplifiers[i] = new rhd2k::rhd2000(_sampling_rate);
                if (i % 2 == 0) _ports[i/2] = _amplifiers[i];
        }

        ulong board_id;
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
                board_id = okFrontPanel_GetWireOutValue(_dev, WireOutBoardId);
        }
        if (board_id != RHYTHM_BOARD_ID) {
                // load the bitfile
                if (!firmware) firmware = "rhythm_130302.bit";
                if ((ec = okFrontPanel_ConfigureFPGA(_dev, firmware)) != ok_NoError) {
                        throw ok_error(ec);
                }
                okFrontPanel_UpdateWireOuts(_dev);
                board_id = okFrontPanel_GetWireOutValue(_dev, WireOutBoardId);
                if (board_id != RHYTHM_BOARD_ID) {
                        throw daq_error("Uploaded FPGA code is not Rhythm");
                }
        }
        _board_version = okFrontPanel_GetWireOutValue(_dev, WireOutBoardVersion);

        reset_board();
        set_sampling_rate();
        scan_amplifiers();
}


rhd2000eval::~rhd2000eval()
{
        for (std::size_t i = 0; i < ninputs; ++i) {
                delete _amplifiers[i];
        }
        if (_pll) okPLL22393_Destruct(_pll);
        if (_dev) okFrontPanel_Destruct(_dev);
}

void
rhd2000eval::start(uint max_frames)
{
        // configure acquisition duration
        if (max_frames == 0) {
                okFrontPanel_SetWireInValue(_dev, WireInResetRun, 0x02, 0x02);
        }
        else {
                okFrontPanel_SetWireInValue(_dev, WireInResetRun, 0x00, 0x02);
                okFrontPanel_SetWireInValue(_dev, WireInMaxTimeStepLsb, max_frames & 0x0000ffff, ulong_mask);
                okFrontPanel_SetWireInValue(_dev, WireInMaxTimeStepMsb, (max_frames & 0xffff0000) >> 16, ulong_mask);
        }
        okFrontPanel_UpdateWireIns(_dev);

        okFrontPanel_ActivateTriggerIn(_dev, TrigInSpiStart, 0);
}

bool
rhd2000eval::running()
{
        okFrontPanel_UpdateWireOuts(_dev);
        return (okFrontPanel_GetWireOutValue(_dev, WireOutSpiRunning) & 0x01 == 1);
}


void
rhd2000eval::stop()
{
        okFrontPanel_SetWireInValue(_dev, WireInMaxTimeStepLsb, 0, ulong_mask);
        okFrontPanel_SetWireInValue(_dev, WireInMaxTimeStepMsb, 0, ulong_mask);
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, 0x00, 0x02);
        okFrontPanel_UpdateWireIns(_dev);
}

size_t
rhd2000eval::nstreams_enabled()
{
        return _nactive_streams;
}

void
rhd2000eval::enable_adc_stream(size_t stream, bool enabled)
{
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamEn, enabled << stream, 1 << stream);
        okFrontPanel_UpdateWireIns(_dev);
        _nactive_streams += (enabled) ? 1 : -1;
}


std::size_t
rhd2000eval::nframes_ready()
{
        return 2 * words_in_fifo() / frame_size();
}

std::size_t
rhd2000eval::frame_size()
{
        // 4 = magic number; 2 = time stamp;
        // 36 = (32 amp channels + 3 aux commands + 1 filler word);
        // 8 = ADCs; 2 = TTL in/out
        return 2 * (4 + 2 + _nactive_streams * 36 + 8 + 2 );
}

ulong
rhd2000eval::words_in_fifo() const
{
        okFrontPanel_UpdateWireOuts(_dev);
        return (okFrontPanel_GetWireOutValue(_dev, WireOutNumWordsMsb) << 16) +
                okFrontPanel_GetWireOutValue(_dev, WireOutNumWordsLsb);
}

std::size_t
rhd2000eval::read(void * arg, std::size_t nframes)
{
        std::size_t bytes = nframes * frame_size();
        okFrontPanel_ReadFromPipeOut(_dev, PipeOutData, bytes, static_cast<unsigned char *>(arg));
        return nframes;
}

uint
rhd2000eval::adc_nchannels()
{
        // this depends on the number of streams and amps per stream
        return _nactive_streams;
}

void
rhd2000eval::reset_board()
{
        // reset
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, 0x0001, 0x0001);
        okFrontPanel_UpdateWireIns(_dev);
        // turn off reset, set some values
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, 0x0000, ulong_mask);
        // SPI run continuous [bit 1]
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, 1 << 1, 1 << 1);
        // DSP settle [bit 2] = 1
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, 1 << 2, 1 << 2);
        // dac noise slice [12:6] = 0
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, 0 << 6, 0x1fc0);
        // dac gain [15:13] = 2^0
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, 0 << 13, 0xe000);
        okFrontPanel_UpdateWireIns(_dev);

        // wire each amp to its own data stream
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel1234, PortA1 << 0, 0x0f << 0);
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel1234, PortA2 << 4, 0x0f << 4);
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel1234, PortB1 << 8, 0x0f << 8);
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel1234, PortB2 << 12, 0x0f << 12);
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel5678, PortC1 << 0, 0x0f << 0);
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel5678, PortC2 << 4, 0x0f << 4);
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel5678, PortD1 << 8, 0x0f << 8);
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamSel5678, PortD2 << 12, 0x0f << 12);
        okFrontPanel_UpdateWireIns(_dev);

        // shut off pipes from input to dacs
        for (int i = 0; i < 8; ++i) {
                okFrontPanel_SetWireInValue(_dev, int(WireInDacSource1) + i, 0x0000, ulong_mask);
        }
        // set manual values to 0 (mid-range)
        okFrontPanel_SetWireInValue(_dev, WireInDacManual1, 0x00ef, ulong_mask);
        okFrontPanel_SetWireInValue(_dev, WireInDacManual2, 0x00ef, ulong_mask);
        // TTL output zeroed
        okFrontPanel_SetWireInValue(_dev, WireInTtlOut, 0x0000, ulong_mask);
        // set LED
        okFrontPanel_SetWireInValue(_dev, WireInLedDisplay, 0x0001, ulong_mask);
        okFrontPanel_UpdateWireIns(_dev);
}

void
rhd2000eval::set_sampling_rate()
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
        okFrontPanel_SetWireInValue(_dev, WireInDataFreqPll, (256 * M + D), ulong_mask);
        okFrontPanel_UpdateWireIns(_dev);
        okFrontPanel_ActivateTriggerIn(_dev, TrigInDcmProg, 0);

        // Wait for DataClkLocked = 1 before allowing data acquisition to continue
        while (!clock_locked()) {}

        // set delay - which depends on sampling rate
        for (int port = (int)PortA; port <= (int)PortD; ++port) {
                set_cable_feet((port_id)port, 3.0);
        }

}

void
rhd2000eval::set_cable_delay(port_id port, uint delay)
{
        assert (delay < 16);
        int shift = (int)port * 4;
        okFrontPanel_SetWireInValue(_dev, WireInMisoDelay, delay << shift, 0x000f << shift);
        okFrontPanel_UpdateWireIns(_dev);
}

void
rhd2000eval::set_cable_meters(port_id port, double len)
{
        assert (len > 0);
        uint delay;
        double dt, timeDelay;
        const double speedOfLight = 299792458.0;           // [m/s]
        const double cableVelocity = 0.67 * speedOfLight;  // propogation velocity on cable is rougly 2/3 the speed of light
        const double xilinxLvdsOutputDelay = 1.9e-9;       // 1.9 ns Xilinx LVDS output pin delay
        const double xilinxLvdsInputDelay = 1.4e-9;        // 1.4 ns Xilinx LVDS input pin delay
        const double rhd2000Delay = 9.0e-9;                // 9.0 ns RHD2000 SCLK-to-MISO delay
        const double misoSettleTime = 10.0e-9;             // 10.0 ns delay after MISO changes, before we sample it

        dt = 1.0 / (2800.0 * _sampling_rate); // data clock that samples MISO
                                              // has a rate 35 x 80 = 2800x higher than the sampling rate

        len *= 2.0;                           // round trip distance data must travel on cable
        timeDelay = len / cableVelocity + xilinxLvdsOutputDelay + rhd2000Delay + xilinxLvdsInputDelay + misoSettleTime;

        delay = std::max((uint)ceil(timeDelay / dt), 1U);
#ifndef NDEBUG
        std::cout << "Total delay = " << (1e9 * timeDelay) << " ns"
                  << " -> MISO delay = " << delay << std::endl;
#endif
        // delay of zero is too short (due to I/O delays), even for zero-length cables
        set_cable_delay(port, delay);
}

void
rhd2000eval::scan_amplifiers()
{
        const std::size_t nframes = rhd2k::rhd2000::register_sequence_length;
        char * buffer;

        // set the basic command sequences for all ports
        rhd2k::rhd2000 * amp = _ports[0];
        std::vector<short> commands;
        // slot 1: write 0's to dac
        std::vector<double> dac(60,0.0);
        amp->command_dac(commands, dac.begin(), dac.end());
        upload_auxcommand(AuxCmd1, 0, commands.begin(), commands.end());

        // slot 2: sample temp, Vdd, aux ADCs
        amp->command_auxsample(commands);
        upload_auxcommand(AuxCmd2, 0, commands.begin(), commands.end());

        // slot 3: set registers and calibrate
        amp->command_regset(commands, true);
        upload_auxcommand(AuxCmd3, 0, commands.begin(), commands.end());
        amp->command_regset(commands, false);
        upload_auxcommand(AuxCmd3, 1, commands.begin(), commands.end());
        assert(commands.size() == nframes);

        // assign command sequences to ports - uses some shady enum casting
        for (int port = (int)PortA; port <= (int)PortD; ++port) {
                for (int slot = (int)AuxCmd1; slot <= (int)AuxCmd3; ++slot) {
                        set_port_auxcommand((port_id)port, (auxcmd_slot)slot, 0);
                }
        }
        // enable all data streams
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamEn, 0x00ff, ulong_mask);
        okFrontPanel_UpdateWireIns(_dev);
        _nactive_streams = ninputs;

        // run calibration sequence
        start(nframes);
        buffer = new char[frame_size() * nframes];
        while(running()) {
                usleep(1);
        }

        //  do some basic sanity checks
        assert(nframes_ready() == nframes);
        read(buffer, nframes);

        // assumes little-endian
        if (*(uint64_t*)buffer != 0xC691199927021942LL) {
                throw daq_error("received data from board with the wrong header");
        }
        if (*(uint64_t*)(buffer+frame_size()) != 0xC691199927021942LL) {
                throw daq_error("received data with the wrong frame size");
        }

        // inspect a frame in gdb: p/x *(short*)(buffer+12)@(_nactive_streams*36)
        for (size_t stream = 0; stream < ninputs; ++stream) {
                size_t offset = 2 * (6 + 2 * ninputs + stream);
#ifndef NDEBUG
                print_channel<short>(buffer, nframes, offset, frame_size());
#endif
                amp = _amplifiers[stream];
                amp->update(buffer, offset, frame_size());
                enable_adc_stream(stream, amp->connected());
        }
        delete[] buffer;

        // turn off calibration sequence
        for (int port = (int)PortA; port <= (int)PortD; ++port) {
                set_port_auxcommand((port_id)port, AuxCmd3, 1);
        }

}

void
rhd2000eval::set_cmd_ram(auxcmd_slot slot, ulong bank, ulong index, ulong command)
{
        okFrontPanel_SetWireInValue(_dev, WireInCmdRamData, command, ulong_mask);
        okFrontPanel_SetWireInValue(_dev, WireInCmdRamAddr, index, ulong_mask);
        okFrontPanel_SetWireInValue(_dev, WireInCmdRamBank, bank, ulong_mask);
        okFrontPanel_UpdateWireIns(_dev);
        okFrontPanel_ActivateTriggerIn(_dev, TrigInRamWrite, (int)slot);
#ifndef NDEBUG
        std::cout << slot << ":" << bank << " [" << index << "] = ";
        rhd2k::print_command(std::cout, command) << std::endl;
#endif
}

void
rhd2000eval::set_auxcommand_length(auxcmd_slot slot, ulong length, ulong loop)
{
        int wire1, wire2;
        assert (length > 0 && length < 1024);
        assert (loop < 1023);
        switch(slot) {
        case AuxCmd1:
                wire1 = WireInAuxCmdLoop1;
                wire2 = WireInAuxCmdLength1;
                break;
        case AuxCmd2:
                wire1 = WireInAuxCmdLoop2;
                wire2 = WireInAuxCmdLength2;
                break;
        case AuxCmd3:
                wire1 = WireInAuxCmdLoop3;
                wire2 = WireInAuxCmdLength3;
                break;
        }
        okFrontPanel_SetWireInValue(_dev, wire1, loop, ulong_mask);
        // rhythm expects an index, not a length
        okFrontPanel_SetWireInValue(_dev, wire2, length-1, ulong_mask);
        okFrontPanel_UpdateWireIns(_dev);
#ifndef NDEBUG
        std::cout << slot << ": length=" << length << ", loop=" << loop << std::endl;
#endif

}


void
rhd2000eval::set_port_auxcommand(port_id port, auxcmd_slot slot, ulong bank)
{
        int shift, wire;
        assert (bank < 16);

        switch (port) {
        case PortA:
                shift = 0;
                break;
        case PortB:
                shift = 4;
                break;
        case PortC:
                shift = 8;
                break;
        case PortD:
                shift = 12;
                break;
        }

        switch (slot) {
        case AuxCmd1:
                wire = WireInAuxCmdBank1;
                break;
        case AuxCmd2:
                wire = WireInAuxCmdBank2;
                break;
        case AuxCmd3:
                wire = WireInAuxCmdBank3;
                break;
        }
        okFrontPanel_SetWireInValue(_dev, wire, bank << shift, 0x000f << shift);
        okFrontPanel_UpdateWireIns(_dev);
#ifndef NDEBUG
        std::cout << "command sequence " << slot << ":" << bank << " -> " << port << std::endl;
#endif
}

bool
rhd2000eval::dcm_done() const
{
        ulong value;
        okFrontPanel_UpdateWireOuts(_dev);
        value = okFrontPanel_GetWireOutValue(_dev, WireOutDataClkLocked);
        return ((value & 0x0002) > 1);
}

bool
rhd2000eval::clock_locked() const
{
        ulong value;
        okFrontPanel_UpdateWireOuts(_dev);
        value = okFrontPanel_GetWireOutValue(_dev, WireOutDataClkLocked);
        return ((value & 0x0001) > 0);
}

std::ostream &
operator<< (std::ostream & o, rhd2000eval const & r)
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
          << "\n Sampling rate: " << r._sampling_rate << " Hz"
          << "\n FIFO data: " << r.words_in_fifo() << " words"
          << "\nAmplifiers: ";
        for (std::size_t i = 0; i < r.ninputs; ++i) {
                o << "\n" << i << ": " <<  *(r._amplifiers[i]);
        }
        return o;
}

std::ostream &
operator<< (std::ostream & o, rhd2000eval::auxcmd_slot slot) {
        switch(slot) {
        case rhd2000eval::AuxCmd1:
                return o << "Aux1";
        case rhd2000eval::AuxCmd2:
                return o << "Aux2";
        case rhd2000eval::AuxCmd3:
                return o << "Aux3";
        default:
                return o << "unknown slot";
        }
}

std::ostream &
operator<< (std::ostream & o, rhd2000eval::port_id port) {
        switch(port) {
        case rhd2000eval::PortA:
                return o << "A";
        case rhd2000eval::PortB:
                return o << "B";
        case rhd2000eval::PortC:
                return o << "C";
        case rhd2000eval::PortD:
                return o << "D";
        default:
                return o << "unknown port";
        }
}
