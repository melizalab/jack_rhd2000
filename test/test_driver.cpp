
#include <iostream>
#include <boost/shared_ptr.hpp>
#include <okFrontPanelDLL.h>

using namespace std;

boost::shared_ptr<okCFrontPanel> dev;

#define RHYTHM_BOARD_ID 500

enum BoardDataSource {
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

enum OkEndPoint {
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

int
main(int argc, char** argv)
{
        char dll_date[32], dll_time[32];
        string serialNumber = "";
        int i, nDevices;
        int boardId = -1, boardVersion = -1;

        if (okFrontPanelDLL_LoadLib(NULL) == false) {
                cerr << "FrontPanel DLL could not be loaded.  " <<
                        "Make sure this DLL is in the application start directory." << endl;
                return -1;
        }
        okFrontPanelDLL_GetVersion(dll_date, dll_time);
        cout << endl << "FrontPanel DLL loaded.  Built: " << dll_date << "  " << dll_time << endl;

        dev.reset(new okCFrontPanel());

        cout << endl << "Scanning USB for Opal Kelly devices..." << endl << endl;
        nDevices = dev->GetDeviceCount();
        cout << "Found " << nDevices << " Opal Kelly device" << ((nDevices == 1) ? "" : "s") <<
                " connected:" << endl;
        for (i = 0; i < nDevices; ++i) {
                cout << "  Device #" << i + 1 << ": Opal Kelly "
                     << dev->GetDeviceListModel(i)
                     << " with serial number " << dev->GetDeviceListSerial(i) << endl;
        }
        cout << endl;

        // Find first device in list of type XEM6010LX45.
        for (i = 0; i < nDevices; ++i) {
                if (dev->GetDeviceListModel(i) == OK_PRODUCT_XEM6010LX45) {
                        serialNumber = dev->GetDeviceListSerial(i);
                        break;
                }
        }

        // Attempt to open device.
        if (dev->OpenBySerial(serialNumber) != okCFrontPanel::NoError) {
                cerr << "Device could not be opened.  Is one connected?" << endl;
                return -2;
        }

        // Configure the on-board PLL appropriately.
        dev->LoadDefaultPLLConfiguration();

        // Get some general information about the XEM.
        okCPLL22393 pll;
        dev->GetEepromPLL22393Configuration(pll);

        cout << "FPGA system clock: " << pll.GetOutputFrequency(0) << " MHz" << endl; // Should indicate 100 MHz
        cout << "Opal Kelly device firmware version: " << dev->GetDeviceMajorVersion() << "." <<
                dev->GetDeviceMinorVersion() << endl;
        cout << "Opal Kelly device serial number: " << dev->GetSerialNumber() << endl;
        cout << "Opal Kelly device ID string: " << dev->GetDeviceID() << endl << endl;

        if (dev->IsFrontPanelEnabled()) {
                dev->UpdateWireOuts();
                boardId = dev->GetWireOutValue(WireOutBoardId);
                boardVersion = dev->GetWireOutValue(WireOutBoardVersion);
        }
        if (boardId != RHYTHM_BOARD_ID) {
                // load the bitfile
                if (dev->ConfigureFPGA("rhythm_130302.bit") != okCFrontPanel::NoError) {
                        cout << "error loading bitfile" << endl;
                        return -1;
                }
                else {
                        cout << "loaded fpga firmware" << endl;
                }
                dev->UpdateWireOuts();
                boardId = dev->GetWireOutValue(WireOutBoardId);
                boardVersion = dev->GetWireOutValue(WireOutBoardVersion);
        }
        cout << "Board id: " << boardId << ", version: " << boardVersion << endl;

        // reset
        dev->SetWireInValue(WireInResetRun, 0x0001, 0x0001);
        dev->UpdateWireIns();
        // turn off reset, set some values
        dev->SetWireInValue(WireInResetRun, 0x0000);
        // SPI run continuous [bit 1]
        dev->SetWireInValue(WireInResetRun, 1 << 1, 1 << 1);
        // DSP settle [bit 2]
        dev->SetWireInValue(WireInResetRun, 1 << 2, 1 << 2);
        // dac noise slice [12:6]
        dev->SetWireInValue(WireInResetRun, 0 << 6, 0x1fc0);
        // dac gain [15:13]
        dev->SetWireInValue(WireInResetRun, 0 << 13, 0xe000);
        dev->UpdateWireIns();

        // do something fun with the leds
        dev->SetWireInValue(WireInLedDisplay, 0x01);
        dev->UpdateWireIns();

        // wire amps to data streams
        dev->SetWireInValue(WireInDataStreamSel1234, PortA1 << 0, 0x0f << 0);
        dev->SetWireInValue(WireInDataStreamSel1234, PortA2 << 4, 0x0f << 4);
        dev->SetWireInValue(WireInDataStreamSel1234, PortB1 << 8, 0x0f << 8);
        dev->SetWireInValue(WireInDataStreamSel1234, PortB2 << 12, 0x0f << 12);
        dev->SetWireInValue(WireInDataStreamSel5678, PortC1 << 0, 0x0f << 0);
        dev->SetWireInValue(WireInDataStreamSel5678, PortC2 << 4, 0x0f << 4);
        dev->SetWireInValue(WireInDataStreamSel5678, PortD1 << 8, 0x0f << 8);
        dev->SetWireInValue(WireInDataStreamSel5678, PortD2 << 12, 0x0f << 12);
        dev->SetWireInValue(WireInDataStreamEn, 0x000f , 0x000f);
        dev->SetWireInValue(WireInLedDisplay, 0x02, 0x02);
        dev->UpdateWireIns();

        // shut off pipes from input to dacs
        for (int i = 0; i < 8; ++i) {
                dev->SetWireInValue(int(WireInDacSource1) + i, 0x0000);
        }
        // set manual values to 0 (mid-range)
        dev->SetWireInValue(WireInDacManual1, 0xef);
        dev->SetWireInValue(WireInDacManual2, 0xef);
        // TTL output zeroed
        dev->SetWireInValue(WireInTtlOut, 0x00);

        sleep(1);
        dev->SetWireInValue(WireInLedDisplay, 0x00);
        dev->UpdateWireIns();

        // scan for amplifiers

        return 0;
}

