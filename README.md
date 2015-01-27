
jack_rhd2000 is a driver that lets you use an Intan RHD2000 eval board as a data
source for the JACK realtime audio toolkit. It is currently under development.

## Requirements

- POSIX-compliant operating system
- JACK audio connection kit, version `0.124` (<http://jackaudio.org>)
- An Intan RHD2000 eval board (<http://intantech.com>)
- From Intan's download page, the Rhythm C++ API source code (specifically the
  `main.bit` file) and the USB drivers for your operating system (specifically
  `libokFrontPanel.so`). The last version of the FPGA code tested with this
  driver was the 130302 release.
- Scons (<http://scons.org>)

## Compilation

To compile the driver, you will need a copy of the JACK source code, specifically the engine headers, which **are not** installed as part of the JACK development package. Thus, **in addition** to installing the JACK client headers and libraries, you need to download `jack-audio-connection-kit-0.124.1.tar.gz` from the JACK website and unpack the tarball in this directory, and create a symbolic link with `ln -s jack-audio-connection-kit-0.124.1 jack`.

To compile, you'll then run `scons -Q`

## Installation

Rename `main.bit` to `rhythm_130302.bit`. Put `jack_rhd2000.so`,
`libokFrontPanel.so`, and `rhythm_130302.bit` in the same directory. This can be
the directory where the other JACK drivers live.

## Usage

```bash
JACK_DRIVER_DIR=<driver dir> jackd -d rhd2000
```

Additional options can be appended to this command to configure the driver:

-   **`-d`:** specify the serial number of the Opal Kelly device to connect to. By
    default, the driver will connect to the first device.

-   **`-F`:** specify the path to the Intan RHD2000 eval board firmware. By
    default, the driver will look for a file called "rhythm_130302.bit" in the
    same directory as the driver

-   **`-r`:** specify the sampling rate, in Hz. Default is 30000 Hz. Only some
    values are supported by the hardware, so if the requested value is
    not available the driver will choose the nearest one

-   **`-p`:** specify the period size, in frames. This option sets the amount of
    time the driver will wait before pulling data off the driver. Larger
    values produce more stable behavior, but increase latency.

-   **`-I`:** configure the driver to leave additional samples in the eval board's
    FIFO. This increases latency, but may help to reduce buffer overruns.

RHD2000 chips on each of the four SPI ports can be configured with the `-A`,
`-B`, `-C`, and `-D` options. The arguments to these options are a
comma-delimited list of up to 5 values. If less than 5 values are supplied, the
defaults are used. The values are as follows:

1.  A hexadecimal number controlling which amplifiers to power up. According to
    the Intan documentation, turning off amplifiers can reduce thermal noise.
    Default is 0xffffffff, which corresponds to 32 1's and turns on all
    amplifiers. 0xffff would only turn on the first 16.

2.  A floating point number to set the highpass filter cutoff frequency, in Hz.
    Only some values are supported by the hardware, so the driver will choose
    the closest one if the requested value isn't available. Default is 100 Hz.

3.  A floating point number to set the lowpass filter cutoff frequency, in Hz.
    Default is 3000 Hz.

4.  A floating point number to set the highpass filter cutoff of the DSP
    integrated into the RHD2000 chip. Default is 1 Hz.

5.  The SPI cable length, in meters, or 0 to auto-detect any connected chips.
    Default is to auto-detect.

On startup, the driver will attempt to connect to the Opal Kelly board, upload
the firmware, and detect/configure the connected amplifier chips. If no chip is
connected to an SPI port, or if '0' is used as the argument to a port's
configuration option, the port will be disabled. If these steps are successful,
the driver will create JACK ports for each enabled RHD2000 channel and for the
eight analog inputs on the eval board.

## Building from source

To build the driver from source, you need

-   a modern c++ compiler
-   the JACK source tree for the currently installed JACK version

The test programs require boost (<http://http://www.boost.org>; >= 1.42)

## License and Warranty

Copyright (c) 2013 C Daniel Meliza.  See COPYING for license information.

This Software is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License, version 2,
along with this program; if not, see <http://www.gnu.org/licenses>.

## Additional notes

The driver achieves low latency by reading data from the eval board's FIFO as
soon as a complete period is available. Due to the long and variable latency of
the USB bus, this may take a substantial proportion of the period. Using a linux
kernel patched for realtime preemption can greatly reduce the variance of the
time it takes to get data off the USB bus, but it's still rate-limited so that a
single RHD2000 amp takes 4.2 out of the available 33 ms in a 1024 frame period.
