// xmi2mid.cpp

/*

    XMI2MID: XMIDI to MIDI converter

    Converts XMI sequences to MIDI Format 0 files.

    Author: Matt Seabrook
    Email: info@mattseabrook.net
    GitHub: https://github.com/mattseabrook

    Copyright (c) 2026 Markus Hein, Matt Seabrook, Kimio Ito

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

*/

#include "config.hpp"

int main(int argc, char* argv[])
{
    try
    {
        if (cfg::program_args(argc, argv) && cfg::seq)
        {
            if (!cfg::dst.empty()) create_directories(cfg::dst);
            if (!cfg::sequences_convert()) exit(EXIT_FAILURE);
        }
        cfg::print_sequence_summary();
    }
    catch (const std::exception& error)
    {
        fprintf(stderr, "Error: %s\n", std::string{error.what()}.c_str());
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}

