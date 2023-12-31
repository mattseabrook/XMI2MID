// xmi2mid.cpp

/*

    XMI2MID: XMIDI to MIDI converter

    Converts the *.XMI supplied as argv[1] to a Standard MIDI format file.

    Author: Matt Seabrook
    Email: info@mattseabrook.net
    GitHub: https://github.com/mattseabrook

    Copyright (c) 2023 Markus Hein, Matt Seabrook, Kimio Ito

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

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <vector>
#include <array>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iomanip>

//==============================================================================

std::array<unsigned char, 18> midiheader = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 60, 'M', 'T', 'r', 'k'};

constexpr unsigned long DEFAULT_TEMPO = 120UL;
constexpr unsigned long XMI_FREQ = 120UL;
constexpr unsigned long DEFAULT_TIMEBASE = (XMI_FREQ * 60UL / DEFAULT_TEMPO);
constexpr unsigned long DEFAULT_QN = (60UL * 1000000UL / DEFAULT_TEMPO);

unsigned short timebase = 960;
unsigned long qnlen = DEFAULT_QN;

struct NOEVENTS
{
    unsigned delta;
    std::array<unsigned char, 3> off;
};
std::array<NOEVENTS, 1000> off_events{{{0xFFFFFFFFL, {0, 0, 0}}}};

auto comp_events = [](const NOEVENTS& a, const NOEVENTS& b)
{
    return a.delta < b.delta;
};

//
// Main Entrypoint
//
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " infile\n";
        std::exit(EXIT_FAILURE);
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file)
    {
        std::cerr << "File " << argv[1] << " cannot open\n";
        exit(EXIT_FAILURE);
    }

    file.seekg(0, std::ios::end);
    std::streamsize fsize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> midi_data(fsize);
    if (!file.read(reinterpret_cast<char *>(midi_data.data()), fsize))
    {
        std::cerr << "Error reading file\n";
        exit(EXIT_FAILURE);
    }

    unsigned char *cur = midi_data.data();

    if (!std::equal(cur, cur + 4, "FORM"))
    {
        std::cerr << "Not XMIDI file (FORM)\n";
    }
    cur += 4;

    unsigned lFORM = _byteswap_ulong(*reinterpret_cast<unsigned *>(cur));
    cur += 4;

    if (!std::equal(cur, cur + 4, "XDIR"))
        std::cerr << "Not XMIDI file (XDIR)\n";
    cur += 4;

    if (!std::equal(cur, cur + 4, "INFO"))
        std::cerr << "Not XMIDI file (INFO)\n";
    cur += 4;

    unsigned lINFO = _byteswap_ulong(*reinterpret_cast<unsigned *>(cur));
    cur += 4;

    unsigned short seqCount = *reinterpret_cast<unsigned short *>(cur);
    cur += 2;

    std::cout << "seqCount: " << seqCount << '\n';

    if (!std::equal(cur, cur + 4, "CAT "))
        std::cerr << "Not XMIDI file (CAT )\n";
    cur += 4;

    unsigned lCAT = _byteswap_ulong(*reinterpret_cast<unsigned *>(cur));
    cur += 4;

    if (!std::equal(cur, cur + 4, "XMID"))
        std::cerr << "Not XMIDI file (XMID)\n";
    cur += 4;

    if (!std::equal(cur, cur + 4, "FORM"))
        std::cerr << "Not XMIDI file (FORM)\n";
    cur += 4;

    unsigned lFORM2 = _byteswap_ulong(*reinterpret_cast<unsigned *>(cur));
    cur += 4;

    if (!std::equal(cur, cur + 4, "XMID"))
        std::cerr << "Not XMIDI file (XMID)\n";
    cur += 4;

    if (!std::equal(cur, cur + 4, "TIMB"))
        std::cerr << "Not XMIDI file (TIMB)\n";
    cur += 4;

    unsigned lTIMB = _byteswap_ulong(*reinterpret_cast<unsigned *>(cur));
    cur += 4;

    for (unsigned i = 0; i < lTIMB; i += 2)
    {
        std::cout << "patch@bank: " << std::setw(3) << static_cast<int>(*cur) << "@" << static_cast<int>(*(cur + 1)) << "\n";
        cur += 2;
    }

    if (!std::memcmp(cur, "RBRN", 4))
    {
        cur += 4;
        std::cout << "(RBRN)\n";
        unsigned lRBRN = _byteswap_ulong(*reinterpret_cast<unsigned *>(cur));
        cur += 4;

        unsigned short nBranch = *reinterpret_cast<unsigned short *>(cur);
        cur += 2;

        for (unsigned i = 0; i < nBranch; i++)
        {
            unsigned short id = *reinterpret_cast<unsigned short *>(cur);
            cur += 2;
            unsigned dest = *reinterpret_cast<unsigned *>(cur);
            cur += 4;
            std::cout << "id/dest: " << std::setfill('0') << std::setw(4) << std::hex << id << "@" << std::setw(8) << dest << "\n";
        }
    }

    if (!std::equal(cur, cur + 4, "EVNT"))
        std::cerr << "Not XMIDI file (EVNT)\n";
    cur += 4;

    unsigned lEVNT = _byteswap_ulong(*reinterpret_cast<unsigned *>(cur));
    cur += 4;
    std::cout << "whole event length: " << lEVNT << '\n';

    std::vector<unsigned char> midi_decode(fsize * 2);
    if (midi_decode.empty())
    {
        std::cerr << "Memory (decode buffer) allocation error\n";
        std::exit(EXIT_FAILURE);
    }

    unsigned char *dcur = midi_decode.data();

    int next_is_delta = 1;
    unsigned char *st = cur;
    unsigned oevents = 0;
    while (cur - st < lEVNT)
    {
        if (*cur < 0x80)
        {
            unsigned delay = 0;
            while (*cur == 0x7F)
            {
                delay += *cur++;
            }
            delay += *cur++;

            while (delay > off_events[0].delta)
            {
                unsigned no_delta = off_events[0].delta;
                unsigned tdelay = no_delta & 0x7F;

                while ((no_delta >>= 7))
                {
                    tdelay <<= 8;
                    tdelay |= (no_delta & 0x7F) | 0x80;
                }

                while (1)
                {
                    *dcur++ = tdelay & 0xFF;
                    if (tdelay & 0x80)
                    {
                        tdelay >>= 8;
                    }
                    else
                    {
                        break;
                    }
                }
                *dcur++ = off_events[0].off[0] & 0x8F;
                *dcur++ = off_events[0].off[1];
                *dcur++ = 0x7F;

                delay -= off_events[0].delta;
                for (unsigned i = 1; i < oevents; i++)
                {
                    off_events[i].delta -= off_events[0].delta;
                }
                off_events[0].delta = 0xFFFFFFFFL;

                std::sort(off_events.begin(), off_events.begin() + oevents, comp_events);

                oevents--;
            }

            for (unsigned i = 0; i < oevents; i++)
            {
                off_events[i].delta -= delay;
            }

            unsigned tdelay = delay & 0x7F;

            while ((delay >>= 7))
            {
                tdelay <<= 8;
                tdelay |= (delay & 0x7F) | 0x80;
            }

            while (1)
            {
                *dcur++ = tdelay & 0xFF;
                if (tdelay & 0x80)
                {
                    tdelay >>= 8;
                }
                else
                {
                    break;
                }
            }
            next_is_delta = 0;
        }
        else
        {
            if (next_is_delta)
            {
                if (*cur >= 0x80)
                {
                    *dcur++ = 0;
                }
            }

            next_is_delta = 1;
            if (*cur == 0xFF)
            {
                if (*(cur + 1) == 0x2F)
                {
                    std::cout << "flush " << std::setw(3) << oevents << " note offs\n";

                    for (unsigned i = 0; i < oevents; i++)
                    {
                        *dcur++ = off_events[i].off[0] & 0x8F;
                        *dcur++ = off_events[i].off[1];
                        *dcur++ = 0x7F;
                        *dcur++ = 0;
                    }
                    *dcur++ = *cur++;
                    *dcur++ = *cur++;
                    *dcur++ = 0;

                    std::cout << "Track Ends\n";

                    break;
                }
                *dcur++ = *cur++;
                *dcur++ = *cur++;
                unsigned textlen = *cur + 1;
                while (textlen--)
                {
                    *dcur++ = *cur++;
                }
            }
            else if (0x80 == (*cur & 0xF0))
            {
                *dcur++ = *cur++;
                *dcur++ = *cur++;
                *dcur++ = *cur++;
            }
            else if (0x90 == (*cur & 0xF0))
            {
                *dcur++ = *cur++;
                *dcur++ = *cur++;
                *dcur++ = *cur++;
                unsigned delta = *cur & 0x7F;

                while (*cur++ > 0x80)
                {
                    delta <<= 7;
                    delta += *cur;
                }

                off_events[oevents].delta = delta;
                off_events[oevents].off[0] = *(dcur - 3);
                off_events[oevents].off[1] = *(dcur - 2);

                oevents++;

                std::sort(std::begin(off_events), std::begin(off_events) + oevents, comp_events);
            }
            // Key pressure
            else if (0xA0 == (*cur & 0xF0))
            {
                *dcur++ = *cur++;
                *dcur++ = *cur++;
                *dcur++ = *cur++;
            }
            // Control Change
            else if (0xB0 == (*cur & 0xF0))
            {
                *dcur++ = *cur++;
                *dcur++ = *cur++;
                *dcur++ = *cur++;
            }
            // Program Change
            else if (0xC0 == (*cur & 0xF0))
            {
                *dcur++ = *cur++;
                *dcur++ = *cur++;
            }
            // Channel Pressure
            else if (0xD0 == (*cur & 0xF0))
            {
                *dcur++ = *cur++;
                *dcur++ = *cur++;
            }
            // Pitch Bend
            else if (0xE0 == (*cur & 0xF0))
            {
                *dcur++ = *cur++;
                *dcur++ = *cur++;
                *dcur++ = *cur++;
            }
            else
            {
                std::cout << "wrong event\n";
                cur++;
            }
        }
    }

    ptrdiff_t dlen = dcur - midi_decode.data();

    std::cout << std::setw(7) << dlen << std::endl;

    std::vector<unsigned char> midi_write(fsize * 2);
    if (midi_write.empty())
    {
        std::cerr << "Memory (write buffer) allocation error\n";
        std::exit(EXIT_FAILURE);
    }

    unsigned char *tcur = midi_write.data();

    unsigned char *pos = midi_decode.data();

    while (pos < dcur)
    {
        // first delta-time
        unsigned delta = 0;
        while (*pos & 0x80)
        {
            delta += *pos++ & 0x7F;
            delta <<= 7;
        }
        delta += *pos++ & 0x7F;

        // change delta here!!
        double factor = (double)timebase * DEFAULT_QN / ((double)qnlen * DEFAULT_TIMEBASE);
        delta = (double)delta * factor + 0.5;

        unsigned tdelta = delta & 0x7F;
        while ((delta >>= 7))
        {
            tdelta <<= 8;
            tdelta |= (delta & 0x7F) | 0x80;
        }
        while (1)
        {
            *tcur++ = tdelta & 0xFF;
            if (tdelta & 0x80)
            {
                tdelta >>= 8;
            }
            else
            {
                break;
            }
        }
        // last -  event

        // Note Off
        if (0x80 == (*pos & 0xF0))
        {
            *tcur++ = *pos++;
            *tcur++ = *pos++;
            *tcur++ = *pos++;
        }
        // Note On
        else if (0x90 == (*pos & 0xF0))
        {
            *tcur++ = *pos++;
            *tcur++ = *pos++;
            *tcur++ = *pos++;
        }
        // Key Pressure
        else if (0xA0 == (*pos & 0xF0))
        {
            *tcur++ = *pos++;
            *tcur++ = *pos++;
            *tcur++ = *pos++;
        }
        // Control Change
        else if (0xB0 == (*pos & 0xF0))
        {
            *tcur++ = *pos++;
            *tcur++ = *pos++;
            *tcur++ = *pos++;
        }
        // Program Change
        else if (0xC0 == (*pos & 0xF0))
        {
            *tcur++ = *pos++;
            *tcur++ = *pos++;
        }
        // Channel Pressure
        else if (0xD0 == (*pos & 0xF0))
        {
            *tcur++ = *pos++;
            *tcur++ = *pos++;
        }
        // Pitch Bend
        else if (0xE0 == (*pos & 0xF0))
        {
            *tcur++ = *pos++;
            *tcur++ = *pos++;
            *tcur++ = *pos++;
        }
        else if (0xF0 == *pos)
        {
            unsigned exlen = 0;
            *tcur++ = *pos++;
            while (*pos < 0)
            {
                exlen += *pos & 0x7F;
                exlen <<= 7;
                *tcur++ = *pos++;
            }
            exlen += *pos & 0x7F;
            *tcur++ = *pos++;
            while (exlen--)
            {
                *tcur++ = *pos++;
            }
        }
        else if (0xF7 == *pos)
        {
            unsigned exlen = 0;
            *tcur++ = *pos++;
            while (*pos < 0)
            {
                exlen += *pos & 0x7F;
                exlen <<= 7;
                *tcur++ = *pos++;
            }
            exlen += *pos & 0x7F;
            *tcur++ = *pos++;
            while (exlen--)
            {
                *tcur++ = *pos++;
            }
        }
        else if (0xFF == *pos)
        {
            *tcur++ = *pos++;
            if (0x51 == *pos)
            {
                *tcur++ = *pos++;
                *tcur++ = *pos++;
                qnlen = (*(unsigned char *)(pos) << 16) + (*(unsigned char *)(pos + 1) << 8) + *(unsigned char *)(pos + 2);
                *tcur++ = *pos++;
                *tcur++ = *pos++;
                *tcur++ = *pos++;
            }
            else
            {
                *tcur++ = *pos++;
                unsigned textlen = *pos;
                *tcur++ = *pos++;
                while (textlen--)
                {
                    *tcur++ = *pos++;
                }
            }
        }
        else
        {
            std::cout << "Bad event " << std::hex << (int)*pos << " at " << std::dec << (pos - midi_decode.data()) << std::endl;
        }
    }

    ptrdiff_t tlen = tcur - midi_write.data();

    std::cout << std::setw(7) << tlen << std::endl;

    // Output
    std::filesystem::path path{argv[1]};
    std::string fn = path.stem().string();
    std::filesystem::path pt = path.replace_extension(".mid");

    std::ofstream out_file(pt, std::ios::binary);
    if (!out_file)
    {
        std::cerr << "File " << pt << " cannot open\n";
        std::exit(EXIT_FAILURE);
    }

    unsigned short swappedTimebase = _byteswap_ushort(timebase);    // Little Endian
    midiheader[12] = static_cast<unsigned char>(swappedTimebase & 0xFF);
    midiheader[13] = static_cast<unsigned char>(swappedTimebase >> 8);

    if (!out_file.write(reinterpret_cast<const char *>(midiheader.data()), 18))
    {
        std::cerr << "File write error\n";
        out_file.close();
        std::exit(EXIT_FAILURE);
    }

    //unsigned bs_tlen = _byteswap_ulong(tlen);
    unsigned long bs_tlen = _byteswap_ulong(static_cast<unsigned long>(tlen));

    if (!out_file.write(reinterpret_cast<const char *>(&bs_tlen), sizeof(unsigned)))
    {
        std::cerr << "File write error\n";
        out_file.close();
        std::exit(EXIT_FAILURE);
    }

    if (!out_file.write(reinterpret_cast<const char *>(midi_write.data()), tlen))
    {
        std::cerr << "File write error\n";
        out_file.close();
        std::exit(EXIT_FAILURE);
    }

    out_file.close();
	
    return 0;
}