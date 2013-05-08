#ifndef _RHD_DEBUG_H
#define _RHD_DEBUG_H

#ifndef NDEBUG

#include <iostream>
#include <iomanip>

template <typename It>
std::ostream &
print_commands(std::ostream & o, It begin, It end)
{
        using namespace std;

        int i = 0;
        It it;
        int channel, reg, data;

        for (it = begin; it != end; ++it) {
                if ((*it & 0xc000) == 0x0000) {
                        channel = (*it & 0x3f00) >> 8;
                        o << "  command[" << i << "] = CONVERT(" << channel << ")" << endl;
                } else if ((*it & 0xc000) == 0xc000) {
                        reg = (*it & 0x3f00) >> 8;
                        o << "  command[" << i << "] = READ(" << reg << ")" << endl;
                } else if ((*it & 0xc000) == 0x8000) {
                        reg = (*it & 0x3f00) >> 8;
                        data = (*it & 0x00ff);
                        o << "  command[" << i << "] = WRITE(" << reg << ",0x";
                        o << hex << internal << setfill('0') << setw(2)
                          << data
                          << dec;
                        o << ")" << endl;
                } else if (*it == 0x5500) {
                        o << "  command[" << i << "] = CALIBRATE" << endl;
                } else if (*it == 0x6a00) {
                        o << "  command[" << i << "] = CLEAR" << endl;
                } else {
                        o << "  command[" << i << "] = INVALID COMMAND: 0x";
                        o << hex << internal << setfill('0') << setw(4)
                          << *it
                          << dec << endl;
                }
                ++i;
        }
        return o << endl;
}

// template <typename T>
// std::ostream &
// print_channel(std::ostream & o, char const * data, size_t nframes, size_t offset, size_t stride)
// {
//         T const * ptr;
//         o << offset << ":" << std::hex;
//         for (size_t i = 0; i < nframes; ++i) {
//                 ptr = reinterpret_cast<T const *>(data + offset + stride*i);
//                 std::cout << " 0x" << *ptr;
//                 // printf("%zd: %#hx\n", i, ptr[stride*i]);
//         }
//         return o << std::dec << std::endl;
// }

// template <typename T>
// std::ostream &
// print_frame(std::ostream & o, char const * data, size_t nstreams, size_t stride)
// {
//         T const * ptr;
//         o << offset << ":" << std::hex;
//         for (size_t i = 0; i < nframes; ++i) {
//                 ptr = reinterpret_cast<T const *>(data + offset + stride*i);
//                 std::cout << " 0x" << *ptr;
//                 // printf("%zd: %#hx\n", i, ptr[stride*i]);
//         }
//         return o << std::dec << std::endl;
// }


#endif

#endif
