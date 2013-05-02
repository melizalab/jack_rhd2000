#include <cstdio>
#include <math.h>
#include <iostream>
#include <sstream>
#include <bitset>
#include "rhd2000eval.hpp"
#include "rhd2k.hpp"
#include "okFrontPanelDLL.h"

using std::size_t;
using std::string;
using namespace rhd2k;

static const float FEET_PER_METERS = 0.3048;
static const ulong RHYTHM_BOARD_ID = 500L;
static const size_t FIFO_CAPACITY_WORDS = 67108864;
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

evalboard::evalboard(size_t sampling_rate, char const * serial, char const * firmware)
        : _dev(0), _pll(okPLL22393_Construct()), _sampling_rate(sampling_rate),
          _board_version(0), _enabled_streams(0), _nactive_streams(0)
{
        // allocate storage for the amplifier wrappers. the first amplifier does
        // double duty for setting output
        for (size_t i = 0; i < nmiso; ++i) {
                _miso[i] = new rhd2000(_sampling_rate);
                if (i % 2 == 0) _mosi[i/2] = _miso[i];
        }

        ulong board_id;
        ok_ErrorCode ec;
        if (okFrontPanelDLL_LoadLib(NULL) == false) {
                throw daq_error("Opal Kelly Front Panel DLL not found");
        }

        _dev = okFrontPanel_Construct();
        if (okFrontPanel_GetDeviceCount(_dev) == 0) {
                throw daq_error("no connected Opal Kelly devices");
        }
        if ((ec = okFrontPanel_OpenBySerial(_dev, serial)) != ok_NoError) {
                throw ok_error(ec);
        }
        if (okFrontPanel_IsHighSpeed(_dev) == false) {
                throw daq_error("Opal Kelly device is connected on a low speed bus");
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
                        throw daq_error("uploaded FPGA code is not Rhythm");
                }
        }
        _board_version = okFrontPanel_GetWireOutValue(_dev, WireOutBoardVersion);

        stop();
        reset_board();
        set_sampling_rate();
}


evalboard::~evalboard()
{
        for (size_t i = 0; i < nmiso; ++i) {
                delete _miso[i];
        }
        if (_pll) okPLL22393_Destruct(_pll);
        if (_dev) okFrontPanel_Destruct(_dev);
}

void
evalboard::start(size_t max_frames)
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
evalboard::running() const
{
        okFrontPanel_UpdateWireOuts(_dev);
        return (okFrontPanel_GetWireOutValue(_dev, WireOutSpiRunning) & 0x01 == 1);
}


void
evalboard::stop()
{
        okFrontPanel_SetWireInValue(_dev, WireInMaxTimeStepLsb, 0, ulong_mask);
        okFrontPanel_SetWireInValue(_dev, WireInMaxTimeStepMsb, 0, ulong_mask);
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, 0x00, 0x02);
        okFrontPanel_UpdateWireIns(_dev);
}

size_t
evalboard::streams_enabled() const
{
        return _nactive_streams;
}

bool
evalboard::stream_enabled(size_t stream) const
{
        return ((_enabled_streams & (1 << stream)) != 0);
}

void
evalboard::enable_stream(size_t stream, bool enabled)
{
        // http://graphics.stanford.edu/~seander/bithacks.html#ConditionalSetOrClearBitsWithoutBranching
        ulong mask = 1 << stream;
        ulong arg = (_enabled_streams & ~mask) | (-enabled & mask);
        enable_streams(arg);
        assert (stream_enabled(stream) == enabled);
}

void
evalboard::enable_streams(ulong arg)
{
        assert (arg <= 0x00ff);
        _enabled_streams = arg;
        okFrontPanel_SetWireInValue(_dev, WireInDataStreamEn, _enabled_streams, ulong_mask);
        okFrontPanel_UpdateWireIns(_dev);
        _nactive_streams = std::bitset<nmiso>(arg).count();
        _adc_table.clear();     // invalidate table
}

size_t
evalboard::nframes() const
{
        return 2 * words_in_fifo() / frame_size();
}

size_t
evalboard::frame_size() const
{
        // 4 = magic number; 2 = time stamp;
        // 36 = (32 amp channels + 3 aux commands + 1 filler word);
        // 8 = ADCs; 2 = TTL in/out
        return 2 * (4 + 2 + _nactive_streams * 36 + 8 + 2 );
}

ulong
evalboard::words_in_fifo() const
{
        okFrontPanel_UpdateWireOuts(_dev);
        return (okFrontPanel_GetWireOutValue(_dev, WireOutNumWordsMsb) << 16) +
                okFrontPanel_GetWireOutValue(_dev, WireOutNumWordsLsb);
}

size_t
evalboard::read(void * arg, size_t nframes)
{
        size_t bytes = nframes * frame_size();
        long ret = okFrontPanel_ReadFromPipeOut(_dev, PipeOutData, bytes, static_cast<unsigned char *>(arg));
        if (ret <= 0) return 0; // error
        else return ret / frame_size();
}

size_t
evalboard::adc_channels() const
{
        return adc_table().size();
}

std::vector<evalboard::channel_info_t> const &
evalboard::adc_table() const
{
        if (_adc_table.empty()) make_adc_table();
        return _adc_table;
}

void
evalboard::make_adc_table() const
{
        const size_t base_offset = 6; // first words in frame
        _adc_table.resize(naux_adcs + _nactive_streams * rhd2000::max_amps);

        size_t chan_count = 0;
        size_t stream_count = 0;
        // miso lines
        for (size_t i = 0; i < evalboard::nmiso; ++i) {
                if (!stream_enabled(i)) continue;
                rhd2000 const * chip = _miso[i];
                for (size_t c = 0; c < rhd2000::max_amps; ++c) {
                        if (!chip->amp_power(c)) continue;

                        std::ostringstream name;
                        channel_info_t & chan = _adc_table[chan_count++];
                        chan.stream  = (miso_id)stream_count;
                        chan.channel = c;

                        name << chan.stream << '_' << c;
                        chan.name    = name.str();

                        // byte offset: base + (chan+3) * nstreams + stream (the
                        // 3-channel offset relates to the fact that the aux
                        // data from the previous time step comes first - see p
                        // 9 in manual)
                        chan.byte_offset = sizeof(data_type) * (base_offset + ((c+3) * streams_enabled() + stream_count));
                }
                stream_count += 1;
        }
        // eval board adcs
        for (size_t c = 0; c < naux_adcs; ++c) {
                std::ostringstream name;
                channel_info_t & chan = _adc_table[chan_count++];
                chan.stream = EvalADC;
                chan.channel = c;

                name << chan.stream << '_' << c;
                chan.name = name.str();
                chan.byte_offset = sizeof(data_type) * (base_offset + (36 * streams_enabled()) + c);
        }
        _adc_table.resize(chan_count);
}

void
evalboard::reset_board()
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
        // turn off LEDs
        okFrontPanel_SetWireInValue(_dev, WireInLedDisplay, 0, ulong_mask);
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
        okFrontPanel_UpdateWireIns(_dev);

        // set the basic command sequences for all ports
        rhd2000 * amp = _mosi[0];
        std::vector<short> commands;
        // slot 1: write 0's to dac
        std::vector<double> dac(60,0.0);
        amp->command_dac(commands, dac.begin(), dac.end());
        upload_auxcommand(AuxCmd1, 0, commands.begin(), commands.end());
        for (int port = (int)PortA; port <= (int)PortD; ++port) {
        }

        // slot 2: sample temp, Vdd, aux ADCs
        amp->command_auxsample(commands);
        upload_auxcommand(AuxCmd2, 0, commands.begin(), commands.end());

        // assign command sequences to ports with some shady enum casting
        for (int port = (int)PortA; port <= (int)PortD; ++port) {
                set_port_auxcommand((mosi_id)port, AuxCmd1, 0);
                set_port_auxcommand((mosi_id)port, AuxCmd2, 0);
        }

}

void
evalboard::set_sampling_rate()
{
        if (running()) {
                throw daq_error("can't set sampling rate on running interface");
        }

        ulong M, D;
        /* see intan docs for how sampling rate is set */
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
        for (size_t port = (size_t)PortA; port <= (size_t)PortD; ++port) {
                set_cable_feet((mosi_id)port, 3.0);
        }

}

void
evalboard::set_cable_delay(mosi_id port, uint delay)
{
        assert (delay < 16);
        int shift = (int)port * 4;
        okFrontPanel_SetWireInValue(_dev, WireInMisoDelay, delay << shift, 0x000f << shift);
        okFrontPanel_UpdateWireIns(_dev);
#if DEBUG == 2
        std::cout << port << ": MISO delay = " << delay << std::endl;
#endif
}

void
evalboard::set_cable_feet(mosi_id port, double feet)
{
        set_cable_meters(port,  FEET_PER_METERS * feet);
}

void
evalboard::set_cable_meters(mosi_id port, double len)
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

        _cable_lengths[(size_t)port] = len;                // store cable length
                                                           // if SR changes

        dt = 1.0 / (2800.0 * _sampling_rate); // data clock that samples MISO
                                              // has a rate 35 x 80 = 2800x higher than the sampling rate

        len *= 2.0;                           // round trip distance data must travel on cable
        timeDelay = len / cableVelocity + xilinxLvdsOutputDelay + rhd2000Delay + xilinxLvdsInputDelay + misoSettleTime;

        // delay of zero is too short (due to I/O delays), even for zero-length cables
        delay = std::max((uint)ceil(timeDelay / dt), 1U);
#if DEBUG == 2
        std::cout << port << ": delay = " << (1e9 * timeDelay) << " ns" << std::endl;
#endif
        set_cable_delay(port, delay);
}

void
evalboard::configure_port(mosi_id port, double lower, double upper,
                           double dsp, ulong amp_power)
{
        if (running()) {
                throw daq_error("can't configure port while system is running");
        }
        rhd2000 * amp = _mosi[(size_t)port];
        amp->set_lower_bandwidth(lower);
        amp->set_upper_bandwidth(upper);
        amp->set_dsp_cutoff(dsp);
        if (amp->amp_power() != amp_power) {
                amp->set_amp_power(amp_power);
                _adc_table.clear();
        }

        // upload new command sequence for this register
        std::vector<short> commands;
        amp->command_regset(commands, false);
        upload_auxcommand(AuxCmd3, (size_t)port, commands.begin(), commands.end());
        set_port_auxcommand(port, AuxCmd3, (size_t)port); // not really necessary

}

void
evalboard::scan_ports()
{
        if (running()) {
                throw daq_error("can't scan ports while system is running");
        }
        const size_t nframes = rhd2000::register_sequence_length;
        std::vector<short> commands(nframes);
        rhd2000 * port;
        size_t frame_bytes;
        char * buffer;

        // upload calibration sequences to slot 3 - diff bank for each port
        for (size_t i = (size_t)PortA; i <= (size_t)PortD; ++i) {
                port = _mosi[i];
                port->command_regset(commands, true);
                upload_auxcommand(AuxCmd3, i, commands.begin(), commands.end());
                set_port_auxcommand((mosi_id)i, AuxCmd3, i);
        }

        // enable all data streams
        enable_streams(0x00ff);
        frame_bytes = frame_size(); // need to cache as frame_size() will change

        // run calibration sequence
        start(nframes);
        buffer = new char[frame_bytes * nframes];
        while(running()) {
                usleep(1);
        }

        //  do some basic sanity checks
        assert(words_in_fifo() == nframes * frame_size() / 2);
        read(buffer, nframes);

        // assumes little-endian
        if (*(uint64_t*)buffer != frame_header) {
                throw daq_error("received data from board with the wrong header");
        }
        if (*(uint64_t*)(buffer+frame_bytes) != frame_header) {
                throw daq_error("received data with the wrong frame size");
        }

        // inspect a frame in gdb: p/x *(short*)(buffer+12)@(_nactive_streams*36)
        for (size_t stream = 0; stream < nmiso; ++stream) {
                size_t offset = 2 * (6 + 2 * nmiso + stream);
#if DEBUG == 2
                print_channel<short>(buffer, nframes, offset, frame_bytes);
#endif
                port = _miso[stream]; // pointer does double duty for amps
                port->update(buffer, offset, frame_bytes);
                enable_stream(stream, port->connected());
        }
        delete[] buffer;

        // indicate which ports are connected with LED
        set_leds(_enabled_streams);

        // turn off calibration sequence
        for (size_t i = (size_t)PortA; i <= (size_t)PortD; ++i) {
                port = _mosi[i];
                port->command_regset(commands, false);
                upload_auxcommand(AuxCmd3, i, commands.begin(), commands.end());
        }
}

void
evalboard::set_cmd_ram(auxcmd_slot slot, ulong bank, ulong index, ulong command)
{
        okFrontPanel_SetWireInValue(_dev, WireInCmdRamData, command, ulong_mask);
        okFrontPanel_SetWireInValue(_dev, WireInCmdRamAddr, index, ulong_mask);
        okFrontPanel_SetWireInValue(_dev, WireInCmdRamBank, bank, ulong_mask);
        okFrontPanel_UpdateWireIns(_dev);
        okFrontPanel_ActivateTriggerIn(_dev, TrigInRamWrite, (int)slot);
#if DEBUG == 2
        std::cout << slot << ":" << bank << " [" << index << "] = ";
        print_command(std::cout, command) << std::endl;
#endif
}

void
evalboard::set_auxcommand_length(auxcmd_slot slot, ulong length, ulong loop)
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
#if DEBUG == 2
        std::cout << slot << ": length=" << length << ", loop=" << loop << std::endl;
#endif

}


void
evalboard::set_port_auxcommand(mosi_id port, auxcmd_slot slot, ulong bank)
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
#if DEBUG == 2
        std::cout << "command sequence " << slot << ":" << bank << " -> " << port << std::endl;
#endif
}

bool
evalboard::dcm_done() const
{
        ulong value;
        okFrontPanel_UpdateWireOuts(_dev);
        value = okFrontPanel_GetWireOutValue(_dev, WireOutDataClkLocked);
        return ((value & 0x0002) > 1);
}

bool
evalboard::clock_locked() const
{
        ulong value;
        okFrontPanel_UpdateWireOuts(_dev);
        value = okFrontPanel_GetWireOutValue(_dev, WireOutDataClkLocked);
        return ((value & 0x0001) > 0);
}

void
evalboard::set_leds(ulong value, ulong mask)
{
        assert (value <= 0xff);
        okFrontPanel_SetWireInValue(_dev, WireInLedDisplay, value, mask);
        okFrontPanel_UpdateWireIns(_dev);
}

void
evalboard::ttl_out(ulong value, ulong mask)
{
        assert (value <= 0xffff);
        okFrontPanel_SetWireInValue(_dev, WireInTtlOut, value, mask);
        okFrontPanel_UpdateWireIns(_dev);
}

ulong
evalboard::ttl_in() const
{
        okFrontPanel_UpdateWireOuts(_dev);
        return okFrontPanel_GetWireOutValue(_dev, WireOutTtlIn);
}

void
evalboard::dac_monitor(uint dac, uint channel)
{
        channel_info_t & chan = _adc_table[channel];
        assert (chan.stream != EvalADC);
        assert (chan.channel < 32);
        // [4:0] - channel; [8:5] - stream; 9 - enable
        set_dac_source(dac, 0x0200 + ((int)chan.stream << 5) + chan.channel);
}

void
evalboard::dac_disable(uint dac)
{
        set_dac_source(dac, 0x0000);
}

void
evalboard::set_dac_source(uint dac, ulong arg)
{
        assert (dac < 8);
        if (arg != _dac_sources[dac]) {
#ifndef NDEBUG
                if (arg & 0x0200)
                        std::cerr << ((arg & 0x01e0) >> 5) << ':' << (arg & 0x001f) << " -> dac " << dac << std::endl;
                else
                        std::cerr << "dac " << dac << " disabled" << std::endl;
#endif
                okFrontPanel_SetWireInValue(_dev, int(WireInDacSource1) + dac,
                                            arg,
                                            ulong_mask);
                okFrontPanel_UpdateWireIns(_dev);
                _dac_sources[dac] = arg;
        }
}

void
evalboard::dac_configure(uint gain, uint clip)
{
        assert (gain < 8);
        assert (clip < 128);
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, gain << 13, 0xe000);
        okFrontPanel_SetWireInValue(_dev, WireInResetRun, clip << 6,  0x1fc0);
        okFrontPanel_UpdateWireIns(_dev);
}

namespace rhd2k {

std::ostream &
operator<< (std::ostream & o, evalboard const & r)
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
          << "\n FIFO data: " << r.words_in_fifo() << '/' << FIFO_CAPACITY_WORDS << " words ("
          << (100.0 * r.words_in_fifo() / FIFO_CAPACITY_WORDS) << "% full)"
          << "\n Analog inputs enabled: " << r.adc_channels()
          << "\n MISO lines: ";
        for (size_t i = 0; i < r.nmiso; ++i) {
                // this assumes the mapping established in reset_board
                o << "\n" << (evalboard::miso_id)i << ": "
                  <<  *(r._miso[i]);
                if (!r.stream_enabled(i)) o << " (off) ";
                else {
                        sprintf(buf1, " (cable %.3f m)", r._cable_lengths[i/2]);
                        o << buf1;
                }
        }
        return o;
}

std::ostream &
operator<< (std::ostream & o, evalboard::auxcmd_slot slot) {
        switch(slot) {
        case evalboard::AuxCmd1:
                return o << "Aux1";
        case evalboard::AuxCmd2:
                return o << "Aux2";
        case evalboard::AuxCmd3:
                return o << "Aux3";
        default:
                return o << "unknown slot";
        }
}

std::ostream &
operator<< (std::ostream & o, evalboard::mosi_id port) {
        switch(port) {
        case evalboard::PortA:
                return o << "A";
        case evalboard::PortB:
                return o << "B";
        case evalboard::PortC:
                return o << "C";
        case evalboard::PortD:
                return o << "D";
        default:
                return o << "unknown port";
        }
}

std::ostream &
operator<< (std::ostream & o, evalboard::miso_id source) {
        switch(source) {
        case evalboard::EvalADC:
                return o << "EV";
        case evalboard::PortA1:
                return o << "A1";
        case evalboard::PortB1:
                return o << "B1";
        case evalboard::PortC1:
                return o << "C1";
        case evalboard::PortD1:
                return o << "D1";
        case evalboard::PortA2:
                return o << "A2";
        case evalboard::PortB2:
                return o << "B2";
        case evalboard::PortC2:
                return o << "C2";
        case evalboard::PortD2:
                return o << "D2";
        default:
                return o << "unknown input";
        }
}

}
