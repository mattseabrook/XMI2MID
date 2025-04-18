// xmi2mid.cpp

/*

    XMI2MID: XMIDI to MIDI converter

    Converts the *.XMI supplied as argv[1] to a MIDI Format 0 file (argv[2]).

    Author: Matt Seabrook
    Email: info@mattseabrook.net
    GitHub: https://github.com/mattseabrook

    Copyright (c) 2025 Markus Hein, Matt Seabrook, Kimio Ito

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
#include <vector>
#include <cstdint>
#include <algorithm>
#include <array>
#include <fstream>
#include <cstdint>
#include <iostream>

std::vector<uint8_t> xmiConverter(const std::vector<uint8_t> &xmi)
{
    //
    // Types, Constants, and Helpers
    //
    struct NoteOffEvent
    {
        uint32_t delta = 0xFFFFFFFF;
        std::array<uint8_t, 3> data{};
    };
    constexpr size_t MaxNoteOffs = 1000;

    constexpr std::array<uint8_t, 18> midiHeader = {
        'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 60, 'M', 'T', 'r', 'k'};
    constexpr uint32_t DefaultTempo = 120;
    constexpr uint32_t XmiFreq = 120;
    constexpr uint32_t DefaultTimebase = (XmiFreq * 60 / DefaultTempo);
    constexpr uint32_t DefaultQN = (60 * 1000000 / DefaultTempo);

    uint16_t timebase = 960;
    uint32_t qnlen = DefaultQN;

    // Sort function for note-off events
    auto eventSort = [](const NoteOffEvent &a, const NoteOffEvent &b)
    {
        return a.delta < b.delta;
    };

    // Parse note-off delta time
    auto parse_noteoff_delta = [](auto &it) -> uint32_t
    {
        uint32_t delta = *it & 0x7F;
        while (*it++ > 0x80)
        {
            delta <<= 7;
            delta += *it;
        }
        return delta;
    };

    // Read SysEx length
    auto read_sysex_length = [](auto &it) -> uint32_t
    {
        uint32_t len = 0;
        while (*it < 0)
        {
            len = (len << 7) + (*it & 0x7F);
            ++it;
        }
        len = (len << 7) + (*it & 0x7F);
        ++it;
        return len;
    };

    // Read variable-length values
    auto read_varlen = [](auto &inIt) -> uint32_t
    {
        uint32_t value = 0;
        while (*inIt & 0x80)
        {
            value = (value << 7) | (*inIt++ & 0x7F);
        }
        value = (value << 7) | (*inIt++ & 0x7F);
        return value;
    };

    // Write variable-length values
    auto write_varlen = [](auto &outIt, uint32_t value)
    {
        uint32_t buffer = value & 0x7F;
        while (value >>= 7)
        {
            buffer <<= 8;
            buffer |= ((value & 0x7F) | 0x80);
        }
        while (true)
        {
            *outIt++ = buffer & 0xFF;
            if (buffer & 0x80)
                buffer >>= 8;
            else
                break;
        }
    };

    ////////////////////////////////////////////////////////////////////////

    //
    // Process XMI data
    //
    auto it = xmi.begin();

    //
    // XMI Header, Branch skip
    //
    it += 4 * 12 + 2;
    uint32_t lTIMB = _byteswap_ulong(*reinterpret_cast<const uint32_t *>(&*it));
    it += 4 + lTIMB;

    if (std::equal(it, it + 4, "RBRN"))
    {
        it += 8;
        uint16_t nBranch = *reinterpret_cast<const uint16_t *>(&*it);
        it += 2 + nBranch * 6;
    }

    it += 4;
    uint32_t lEVNT = _byteswap_ulong(*reinterpret_cast<const uint32_t *>(&*it));
    it += 4;

    //
    // Decode Events
    //
    std::vector<uint8_t> midiDecode(xmi.size() * 2);
    auto decodeIt = midiDecode.begin();

    std::array<NoteOffEvent, MaxNoteOffs> noteOffs;
    size_t noteOffCount = 0;

    bool expectDelta = true;
    auto eventStart = it;

    while (std::distance(eventStart, it) < static_cast<ptrdiff_t>(lEVNT))
    {
        if (*it < 0x80)
        {
            // Delta time
            uint32_t delay = 0;
            while (*it == 0x7F)
                delay += *it++;
            delay += *it++;

            // Handle pending note-offs
            while (delay > noteOffs[0].delta)
            {
                write_varlen(decodeIt, noteOffs[0].delta);
                *decodeIt++ = noteOffs[0].data[0] & 0x8F;
                *decodeIt++ = noteOffs[0].data[1];
                *decodeIt++ = 0x7F;

                delay -= noteOffs[0].delta;
                for (size_t i = 1; i < noteOffCount; ++i)
                    noteOffs[i].delta -= noteOffs[0].delta;
                noteOffs[0].delta = 0xFFFFFFFF;
                std::sort(noteOffs.begin(), noteOffs.begin() + noteOffCount, eventSort);
                --noteOffCount;
            }
            for (size_t i = 0; i < noteOffCount; ++i)
                noteOffs[i].delta -= delay;

            // Write delta
            write_varlen(decodeIt, delay);
            expectDelta = false;
        }
        else
        {
            if (expectDelta && *it >= 0x80)
                *decodeIt++ = 0;
            expectDelta = true;

            if (*it == 0xFF)
            {
                if (*(it + 1) == 0x2F)
                {
                    for (size_t i = 0; i < noteOffCount; ++i)
                    {
                        *decodeIt++ = noteOffs[i].data[0] & 0x8F;
                        *decodeIt++ = noteOffs[i].data[1];
                        *decodeIt++ = 0x7F;
                        *decodeIt++ = 0;
                    }
                    *decodeIt++ = *it++;
                    *decodeIt++ = *it++;
                    *decodeIt++ = 0;
                    break;
                }
                *decodeIt++ = *it++;
                *decodeIt++ = *it++;
                uint32_t textlen = *it + 1;
                decodeIt = std::copy_n(it, textlen, decodeIt);
                it += textlen;
            }
            else if ((*it & 0xF0) == 0x80)
            { // Note Off
                decodeIt = std::copy_n(it, 3, decodeIt);
                it += 3;
            }
            else if ((*it & 0xF0) == 0x90)
            { // Note On
                decodeIt = std::copy_n(it, 3, decodeIt);
                it += 3;
                uint32_t delta = parse_noteoff_delta(it);
                noteOffs[noteOffCount].delta = delta;
                noteOffs[noteOffCount].data[0] = *(decodeIt - 3);
                noteOffs[noteOffCount].data[1] = *(decodeIt - 2);
                ++noteOffCount;
                std::sort(noteOffs.begin(), noteOffs.begin() + noteOffCount, eventSort);
            }
            else if ((*it & 0xF0) == 0xA0)
            { // Key Pressure
                decodeIt = std::copy_n(it, 3, decodeIt);
                it += 3;
            }
            else if ((*it & 0xF0) == 0xB0)
            { // Control Change
                decodeIt = std::copy_n(it, 3, decodeIt);
                it += 3;
            }
            else if ((*it & 0xF0) == 0xC0)
            { // Program Change
                decodeIt = std::copy_n(it, 2, decodeIt);
                it += 2;
            }
            else if ((*it & 0xF0) == 0xD0)
            { // Channel Pressure
                decodeIt = std::copy_n(it, 2, decodeIt);
                it += 2;
            }
            else if ((*it & 0xF0) == 0xE0)
            { // Pitch Bend
                decodeIt = std::copy_n(it, 3, decodeIt);
                it += 3;
            }
            else
            {
                ++it;
            }
        }
    }

    //
    // Write final MIDI data
    //
    std::vector<uint8_t> midiWrite(xmi.size() * 2);
    auto writeIt = midiWrite.begin();
    auto readIt = midiDecode.begin();

    while (readIt < decodeIt)
    {
        // Delta-time
        uint32_t delta = read_varlen(readIt);

        // Adjust delta based on tempo
        double factor = static_cast<double>(timebase) * DefaultQN / (static_cast<double>(qnlen) * DefaultTimebase);
        delta = static_cast<uint32_t>(static_cast<double>(delta) * factor + 0.5);
        write_varlen(writeIt, delta);

        // Event handling
        if ((*readIt & 0xF0) == 0x80)
        { // Note Off
            writeIt = std::copy_n(readIt, 3, writeIt);
            readIt += 3;
        }
        else if ((*readIt & 0xF0) == 0x90)
        { // Note On
            writeIt = std::copy_n(readIt, 3, writeIt);
            readIt += 3;
        }
        else if ((*readIt & 0xF0) == 0xA0)
        { // Key Pressure
            writeIt = std::copy_n(readIt, 3, writeIt);
            readIt += 3;
        }
        else if ((*readIt & 0xF0) == 0xB0)
        { // Control Change
            writeIt = std::copy_n(readIt, 3, writeIt);
            readIt += 3;
        }
        else if ((*readIt & 0xF0) == 0xC0)
        { // Program Change
            writeIt = std::copy_n(readIt, 2, writeIt);
            readIt += 2;
        }
        else if ((*readIt & 0xF0) == 0xD0)
        { // Channel Pressure
            writeIt = std::copy_n(readIt, 2, writeIt);
            readIt += 2;
        }
        else if ((*readIt & 0xF0) == 0xE0)
        { // Pitch Bend
            writeIt = std::copy_n(readIt, 3, writeIt);
            readIt += 3;
        }
        else if (*readIt == 0xF0 || *readIt == 0xF7)
        { // Sysex
            *writeIt++ = *readIt++;
            uint32_t exlen = read_sysex_length(readIt);
            writeIt = std::copy_n(readIt, exlen, writeIt);
            readIt += exlen;
        }
        else if (*readIt == 0xFF)
        { // Meta Event
            *writeIt++ = *readIt++;
            if (*readIt == 0x51)
            { // Tempo
                *writeIt++ = *readIt++;
                *writeIt++ = *readIt++;
                qnlen = (static_cast<uint32_t>(readIt[0]) << 16) | (static_cast<uint32_t>(readIt[1]) << 8) | static_cast<uint32_t>(readIt[2]);
                writeIt = std::copy_n(readIt, 3, writeIt);
                readIt += 3;
            }
            else
            {
                *writeIt++ = *readIt++; // Meta type
                uint32_t textlen = *readIt;
                *writeIt++ = *readIt++; // Length
                writeIt = std::copy_n(readIt, textlen, writeIt);
                readIt += textlen;
            }
        }
    }

    //
    // MIDI Return output
    //
    std::vector<uint8_t> midiData;
    auto header = midiHeader;
    uint16_t swappedTimebase = _byteswap_ushort(timebase);
    header[12] = static_cast<uint8_t>(swappedTimebase & 0xFF);
    header[13] = static_cast<uint8_t>(swappedTimebase >> 8);

    midiData.insert(midiData.end(), header.begin(), header.end());
    uint32_t trackLen = static_cast<uint32_t>(std::distance(midiWrite.begin(), writeIt));
    uint32_t swappedTrackLen = _byteswap_ulong(trackLen);
    midiData.insert(midiData.end(), reinterpret_cast<uint8_t *>(&swappedTrackLen), reinterpret_cast<uint8_t *>(&swappedTrackLen) + 4);
    midiData.insert(midiData.end(), midiWrite.begin(), writeIt);

    return midiData;
}

////////////////////////////////////////////////////////////////////////
//      Main Entry Point
////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <input.xmi> <output.mid>\n";
        return 1;
    }

    // Read XMI file
    std::ifstream inFile(argv[1], std::ios::binary);
    if (!inFile)
    {
        std::cerr << "Error: Cannot open input file " << argv[1] << "\n";
        return 1;
    }
    std::vector<uint8_t> xmiData((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    inFile.close();

    // Convert to MIDI
    std::vector<uint8_t> midiData = xmiConverter(xmiData);
    if (midiData.empty())
    {
        std::cerr << "Error: Conversion failed (empty output)\n";
        return 1;
    }

    // Write MIDI file
    std::ofstream outFile(argv[2], std::ios::binary);
    if (!outFile)
    {
        std::cerr << "Error: Cannot open output file " << argv[2] << "\n";
        return 1;
    }
    outFile.write(reinterpret_cast<const char *>(midiData.data()), midiData.size());
    outFile.close();

    std::cout << "Converted " << argv[1] << " to " << argv[2] << "\n";
    return 0;
}