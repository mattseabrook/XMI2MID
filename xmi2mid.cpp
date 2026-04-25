// xmi2mid.cpp

/*

    XMI2MID: XMIDI to MIDI converter

    Converts the *.XMI supplied as argv[1] to a MIDI Format 0 file (argv[2]).

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

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
struct NoteOffEvent
{
    uint32_t delta = 0;
    uint8_t status = 0;
    uint8_t note = 0;
};

constexpr size_t MaxNoteOffs = 1000;
constexpr uint32_t DefaultTempo = 120;
constexpr uint32_t XmiFreq = 120;
constexpr uint32_t DefaultTimebase = XmiFreq * 60 / DefaultTempo;
constexpr uint32_t DefaultQuarterNoteMicros = 60 * 1'000'000 / DefaultTempo;
constexpr uint16_t MidiTimebase = 960;
constexpr size_t TrackLengthOffset = 18;
constexpr size_t TrackDataOffset = 22;

void need_bytes(const uint8_t* cursor, const uint8_t* end, size_t count, std::string_view context)
{
    if (count > static_cast<size_t>(end - cursor))
    {
        throw std::runtime_error("Invalid XMI: truncated " + std::string(context));
    }
}

bool has_tag(const uint8_t* cursor, const uint8_t* end, std::string_view tag)
{
    return tag.size() <= static_cast<size_t>(end - cursor) && std::equal(tag.begin(), tag.end(), cursor);
}

void skip_tag(const uint8_t*& cursor, const uint8_t* end, std::string_view tag)
{
    if (!has_tag(cursor, end, tag))
    {
        throw std::runtime_error("Invalid XMI: expected " + std::string(tag));
    }
    cursor += tag.size();
}

void skip_bytes(const uint8_t*& cursor, const uint8_t* end, size_t count, std::string_view context)
{
    need_bytes(cursor, end, count, context);
    cursor += count;
}

uint32_t read_be32(const uint8_t*& cursor, const uint8_t* end)
{
    need_bytes(cursor, end, 4, "32-bit integer");
    const uint32_t value = (static_cast<uint32_t>(cursor[0]) << 24) |
                           (static_cast<uint32_t>(cursor[1]) << 16) |
                           (static_cast<uint32_t>(cursor[2]) << 8) |
                           static_cast<uint32_t>(cursor[3]);
    cursor += 4;
    return value;
}

void append_be32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>(value >> 24));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}

void patch_be32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    bytes[offset] = static_cast<uint8_t>(value >> 24);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 3] = static_cast<uint8_t>(value);
}

uint32_t read_varlen(const uint8_t*& cursor, const uint8_t* end)
{
    uint32_t value = 0;
    for (int byteCount = 0; byteCount < 5; ++byteCount)
    {
        need_bytes(cursor, end, 1, "variable-length integer");
        const uint8_t byte = *cursor++;
        value = (value << 7) | (byte & 0x7F);
        if ((byte & 0x80) == 0)
        {
            return value;
        }
    }
    throw std::runtime_error("Invalid XMI: variable-length integer is too large");
}

void append_varlen(std::vector<uint8_t>& bytes, uint32_t value)
{
    std::array<uint8_t, 5> encoded{};
    size_t count = 0;
    encoded[count++] = static_cast<uint8_t>(value & 0x7F);

    while ((value >>= 7) != 0)
    {
        encoded[count++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
    }

    while (count != 0)
    {
        bytes.push_back(encoded[--count]);
    }
}

uint32_t read_xmi_delta(const uint8_t*& cursor, const uint8_t* end)
{
    uint32_t delay = 0;
    while (cursor != end && *cursor == 0x7F)
    {
        delay += *cursor++;
    }

    need_bytes(cursor, end, 1, "XMI delta");
    return delay + *cursor++;
}

uint32_t scale_delta(uint32_t delta, uint32_t quarterNoteMicros)
{
    const uint64_t denominator = static_cast<uint64_t>(quarterNoteMicros) * DefaultTimebase;
    if (denominator == 0)
    {
        throw std::runtime_error("Invalid MIDI tempo: zero quarter-note length");
    }

    const uint64_t numerator = static_cast<uint64_t>(delta) * MidiTimebase * DefaultQuarterNoteMicros;
    return static_cast<uint32_t>((numerator + denominator / 2) / denominator);
}

void append_scaled_delta(std::vector<uint8_t>& midi, uint32_t delta, uint32_t quarterNoteMicros)
{
    append_varlen(midi, scale_delta(delta, quarterNoteMicros));
}

void append_bytes(std::vector<uint8_t>& midi, const uint8_t*& cursor, const uint8_t* end, size_t count)
{
    need_bytes(cursor, end, count, "event payload");
    midi.insert(midi.end(), cursor, cursor + count);
    cursor += count;
}

size_t channel_event_size(uint8_t status)
{
    switch (status & 0xF0)
    {
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
    case 0xE0:
        return 3;
    case 0xC0:
    case 0xD0:
        return 2;
    default:
        return 0;
    }
}

std::vector<uint8_t> read_file(const std::filesystem::path& path)
{
    const uintmax_t fileSize = std::filesystem::file_size(path);
    if (fileSize > static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max()))
    {
        throw std::runtime_error("Input file is too large");
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Cannot open input file " + path.string());
    }

    if (!bytes.empty() &&
        !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
    {
        throw std::runtime_error("Cannot read input file " + path.string());
    }
    return bytes;
}

void write_file(const std::filesystem::path& path, std::span<const uint8_t> bytes)
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Cannot open output file " + path.string());
    }

    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file)
    {
        throw std::runtime_error("Cannot write output file " + path.string());
    }
}
}

std::vector<uint8_t> xmiConverter(std::span<const uint8_t> xmi)
{
    if (xmi.empty())
    {
        throw std::runtime_error("Invalid XMI: empty file");
    }

    const uint8_t* cursor = xmi.data();
    const uint8_t* const end = cursor + xmi.size();

    skip_tag(cursor, end, "FORM");
    read_be32(cursor, end);
    skip_tag(cursor, end, "XDIR");
    skip_tag(cursor, end, "INFO");
    skip_bytes(cursor, end, read_be32(cursor, end), "INFO chunk");
    skip_tag(cursor, end, "CAT ");
    read_be32(cursor, end);
    skip_tag(cursor, end, "XMID");
    skip_tag(cursor, end, "FORM");
    read_be32(cursor, end);
    skip_tag(cursor, end, "XMID");
    skip_tag(cursor, end, "TIMB");
    skip_bytes(cursor, end, read_be32(cursor, end), "TIMB chunk");

    if (has_tag(cursor, end, "RBRN"))
    {
        cursor += 4;
        skip_bytes(cursor, end, read_be32(cursor, end), "RBRN chunk");
    }

    skip_tag(cursor, end, "EVNT");
    const uint32_t eventLength = read_be32(cursor, end);
    need_bytes(cursor, end, eventLength, "EVNT chunk");
    const uint8_t* const eventEnd = cursor + eventLength;

    std::vector<uint8_t> midi;
    midi.reserve((xmi.size() * 2) + TrackDataOffset);
    midi.insert(midi.end(), {'M', 'T', 'h', 'd'});
    append_be32(midi, 6);
    midi.push_back(0);
    midi.push_back(0);
    midi.push_back(0);
    midi.push_back(1);
    midi.push_back(static_cast<uint8_t>(MidiTimebase >> 8));
    midi.push_back(static_cast<uint8_t>(MidiTimebase));
    midi.insert(midi.end(), {'M', 'T', 'r', 'k', 0, 0, 0, 0});

    std::array<NoteOffEvent, MaxNoteOffs> noteOffs{};
    size_t noteOffCount = 0;
    uint32_t quarterNoteMicros = DefaultQuarterNoteMicros;
    bool expectDelta = true;

    auto append_note_off = [&](const NoteOffEvent& event)
    {
        midi.push_back(event.status & 0x8F);
        midi.push_back(event.note);
        midi.push_back(0x7F);
    };

    auto queue_note_off = [&](uint32_t delta, uint8_t status, uint8_t note)
    {
        if (noteOffCount == noteOffs.size())
        {
            throw std::runtime_error("Too many pending note-off events");
        }

        const auto first = noteOffs.begin();
        const auto last = first + static_cast<std::ptrdiff_t>(noteOffCount);
        const auto insertAt = std::upper_bound(first, last, delta, [](uint32_t value, const NoteOffEvent& event)
        {
            return value < event.delta;
        });

        std::move_backward(insertAt, last, last + 1);
        *insertAt = NoteOffEvent{delta, status, note};
        ++noteOffCount;
    };

    auto pop_note_off = [&]
    {
        const NoteOffEvent event = noteOffs[0];
        append_scaled_delta(midi, event.delta, quarterNoteMicros);
        append_note_off(event);

        for (size_t i = 1; i < noteOffCount; ++i)
        {
            noteOffs[i].delta -= event.delta;
        }

        std::move(noteOffs.begin() + 1, noteOffs.begin() + static_cast<std::ptrdiff_t>(noteOffCount), noteOffs.begin());
        --noteOffCount;
    };

    auto begin_event = [&]
    {
        if (expectDelta)
        {
            append_scaled_delta(midi, 0, quarterNoteMicros);
        }
        expectDelta = true;
    };

    while (cursor < eventEnd)
    {
        if (*cursor < 0x80)
        {
            uint32_t delay = read_xmi_delta(cursor, eventEnd);

            while (noteOffCount != 0 && delay > noteOffs[0].delta)
            {
                delay -= noteOffs[0].delta;
                pop_note_off();
            }

            for (size_t i = 0; i < noteOffCount; ++i)
            {
                noteOffs[i].delta -= delay;
            }

            append_scaled_delta(midi, delay, quarterNoteMicros);
            expectDelta = false;
            continue;
        }

        const uint8_t status = *cursor;
        if (status == 0xFF)
        {
            need_bytes(cursor, eventEnd, 2, "meta event");
            const uint8_t metaType = cursor[1];
            begin_event();

            if (metaType == 0x2F)
            {
                cursor += 2;
                const uint32_t metaLength = read_varlen(cursor, eventEnd);
                skip_bytes(cursor, eventEnd, metaLength, "end-of-track payload");

                for (size_t i = 0; i < noteOffCount; ++i)
                {
                    append_note_off(noteOffs[i]);
                    append_scaled_delta(midi, 0, quarterNoteMicros);
                }

                midi.push_back(0xFF);
                midi.push_back(0x2F);
                midi.push_back(0);
                break;
            }

            midi.push_back(*cursor++);
            midi.push_back(*cursor++);
            const uint8_t* const lengthStart = cursor;
            const uint32_t metaLength = read_varlen(cursor, eventEnd);
            midi.insert(midi.end(), lengthStart, cursor);
            need_bytes(cursor, eventEnd, metaLength, "meta payload");

            if (metaType == 0x51 && metaLength == 3)
            {
                quarterNoteMicros = (static_cast<uint32_t>(cursor[0]) << 16) |
                                    (static_cast<uint32_t>(cursor[1]) << 8) |
                                    static_cast<uint32_t>(cursor[2]);
            }

            midi.insert(midi.end(), cursor, cursor + metaLength);
            cursor += metaLength;
        }
        else if (status == 0xF0 || status == 0xF7)
        {
            begin_event();
            midi.push_back(*cursor++);
            const uint8_t* const lengthStart = cursor;
            const uint32_t sysexLength = read_varlen(cursor, eventEnd);
            midi.insert(midi.end(), lengthStart, cursor);
            append_bytes(midi, cursor, eventEnd, sysexLength);
        }
        else
        {
            const size_t eventSize = channel_event_size(status);
            if (eventSize == 0)
            {
                ++cursor;
                expectDelta = true;
                continue;
            }

            begin_event();
            const uint8_t eventStatus = cursor[0];
            const uint8_t eventNote = eventSize > 1 ? cursor[1] : 0;
            append_bytes(midi, cursor, eventEnd, eventSize);

            if ((eventStatus & 0xF0) == 0x90)
            {
                queue_note_off(read_varlen(cursor, eventEnd), eventStatus, eventNote);
            }
        }
    }

    const size_t trackLength = midi.size() - TrackDataOffset;
    if (trackLength > std::numeric_limits<uint32_t>::max())
    {
        throw std::runtime_error("MIDI track is too large");
    }

    patch_be32(midi, TrackLengthOffset, static_cast<uint32_t>(trackLength));
    return midi;
}

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <input.xmi> <output.mid>\n";
        return 1;
    }

    try
    {
        const auto xmiData = read_file(argv[1]);
        const auto midiData = xmiConverter(xmiData);
        write_file(argv[2], midiData);
        std::cout << "Converted " << argv[1] << " to " << argv[2] << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
