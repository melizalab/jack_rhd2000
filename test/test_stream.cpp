
#include <iostream>
#include <boost/shared_ptr.hpp>
#include <rhd2000evalboard.h>
#include <rhd2000registers.h>
#include <rhd2000datablock.h>

using namespace std;

boost::shared_ptr<Rhd2000EvalBoard> dev;
boost::shared_ptr<Rhd2000DataBlock> data;

void
set_sampling_rate()
{
        int commandSequenceLength;
        vector<int> commandList;

        // enable all data streams
        for (int i = 0; i < MAX_NUM_DATA_STREAMS; i++) {
                dev->enableDataStream(i, true);
        }

        // get sampling rate by resetting board
        dev->resetBoard();
        double sampling_rate = dev->getSampleRate();
        Rhd2000Registers chipRegisters(sampling_rate);

        // assume cable length of 3.0 feet

        commandSequenceLength = chipRegisters.createCommandListZcheckDac(commandList, 250.0, 0.0);
        dev->uploadCommandList(commandList, Rhd2000EvalBoard::AuxCmd1, 0);
        dev->selectAuxCommandLength(Rhd2000EvalBoard::AuxCmd1, 0, commandSequenceLength - 1);
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortA, Rhd2000EvalBoard::AuxCmd1, 0);
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortB, Rhd2000EvalBoard::AuxCmd1, 0);
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortC, Rhd2000EvalBoard::AuxCmd1, 0);
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortD, Rhd2000EvalBoard::AuxCmd1, 0);
        // read sensors in slot 2
        commandSequenceLength = chipRegisters.createCommandListTempSensor(commandList);
        dev->uploadCommandList(commandList, Rhd2000EvalBoard::AuxCmd2, 0);
        dev->selectAuxCommandLength(Rhd2000EvalBoard::AuxCmd2, 0, commandSequenceLength - 1);
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortA, Rhd2000EvalBoard::AuxCmd2, 0);
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortB, Rhd2000EvalBoard::AuxCmd2, 0);
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortC, Rhd2000EvalBoard::AuxCmd2, 0);
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortD, Rhd2000EvalBoard::AuxCmd2, 0);
        // read and write registers with slot 3
        chipRegisters.setDspCutoffFreq(1.0);
        chipRegisters.setLowerBandwidth(100);
        chipRegisters.setUpperBandwidth(3000);
        chipRegisters.enableDsp(true);
        // Upload version with ADC calibration to AuxCmd3 RAM Bank 0.
        commandSequenceLength = chipRegisters.createCommandListRegisterConfig(commandList, true);
        dev->uploadCommandList(commandList, Rhd2000EvalBoard::AuxCmd3, 0);
        dev->selectAuxCommandLength(Rhd2000EvalBoard::AuxCmd3, 0,
                                          commandSequenceLength - 1);
        // Upload version with no ADC calibration to AuxCmd3 RAM Bank 1.
        commandSequenceLength = chipRegisters.createCommandListRegisterConfig(commandList, false);
        dev->uploadCommandList(commandList, Rhd2000EvalBoard::AuxCmd3, 1);
        dev->selectAuxCommandLength(Rhd2000EvalBoard::AuxCmd3, 0,
                                          commandSequenceLength - 1);
}

void
scan_amplifiers()
{
        int nstreams = dev->getNumEnabledDataStreams();
        data.reset(new Rhd2000DataBlock(nstreams));
        // use calibration command
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortA, Rhd2000EvalBoard::AuxCmd3, 0);
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortB, Rhd2000EvalBoard::AuxCmd3, 0);
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortC, Rhd2000EvalBoard::AuxCmd3, 0);
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortD, Rhd2000EvalBoard::AuxCmd3, 0);

        // Since our longest command sequence is 60 commands, we run the SPI
        // interface for 60 samples.
        dev->setMaxTimeStep(60);
        dev->setContinuousRunMode(false);

        // Start SPI interface.
        dev->run();

        // Wait for the 60-sample run to complete.
        while (dev->isRunning()) {
                usleep(1000);
        }
        dev->readDataBlock(data.get());

        for (int i = 0; i < nstreams; ++i) {
                cout << "Port " << i << ":";
                bool intanChipPresent = ((char) data->auxiliaryData[i][2][32] == 'I' &&
                                         (char) data->auxiliaryData[i][2][33] == 'N' &&
                                         (char) data->auxiliaryData[i][2][34] == 'T' &&
                                         (char) data->auxiliaryData[i][2][35] == 'A' &&
                                         (char) data->auxiliaryData[i][2][36] == 'N');

                if (intanChipPresent) {
                        data->print(i);
                }
                else {
                        cout << " no amplifier" << endl;
                        dev->enableDataStream(i, false);
                }
        }
}

int
main(int argc, char ** argv)
{

        dev.reset(new Rhd2000EvalBoard);
        if (dev->open() < 0) {
                return EXIT_FAILURE;
        }

        // should only do this if the device isn't already configured
        if (!dev->uploadFpgaBitfile("fpga/main.bit")) {
                return EXIT_FAILURE;
        }

        // Initialize interface board.
        dev->initialize();

        // set sampling rate (which updates command banks)
        set_sampling_rate();
        // scan for connected amps
        scan_amplifiers();

        // switch to non-calibration sequence
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortA, Rhd2000EvalBoard::AuxCmd3, 1);
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortB, Rhd2000EvalBoard::AuxCmd3, 1);
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortC, Rhd2000EvalBoard::AuxCmd3, 1);
        dev->selectAuxCommandBank(Rhd2000EvalBoard::PortD, Rhd2000EvalBoard::AuxCmd3, 1);
        dev->setContinuousRunMode(true);

        int nstreams = dev->getNumEnabledDataStreams();
        data.reset(new Rhd2000DataBlock(nstreams));
        unsigned int blocksize = data->calculateDataBlockSizeInWords(nstreams);
        dev->run();
        cout << "device started; blocksize=" << blocksize << endl;
        while (dev->isRunning()) {
                unsigned int read_space = dev->numWordsInFifo();
                cout << "\rfifo fill: " << read_space << flush;
                if (read_space >= blocksize) {
                        dev->readDataBlock(data.get());
                }
        }


        return 0;

}

