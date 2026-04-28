#ifndef XMI2MID_HPP
#define XMI2MID_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace xmi2mid
{
inline std::vector<std::uint8_t> convert(std::span<const std::uint8_t> xmi)
{
    struct NoteOffEvent
    {
        std::uint32_t delta = 0;
        std::uint8_t status = 0;
        std::uint8_t note = 0;
    };

    constexpr std::size_t MaxNoteOffs = 1000;
    constexpr std::uint32_t DefaultTempo = 120;
    constexpr std::uint32_t XmiFreq = 120;
    constexpr std::uint32_t DefaultTimebase = XmiFreq * 60 / DefaultTempo;
    constexpr std::uint32_t DefaultQuarterNoteMicros = 60 * 1'000'000 / DefaultTempo;
    constexpr std::uint16_t MidiTimebase = 960;
    constexpr std::size_t TrackLengthOffset = 18;
    constexpr std::size_t TrackDataOffset = 22;

    if (xmi.empty())
    {
        throw std::runtime_error("Invalid XMI: empty file");
    }

    const std::uint8_t* cursor = xmi.data();
    const std::uint8_t* const end = cursor + xmi.size();

    auto need_bytes = [](const std::uint8_t* cursor, const std::uint8_t* end, std::size_t count,
                         std::string_view context)
    {
        if (count > static_cast<std::size_t>(end - cursor))
        {
            throw std::runtime_error("Invalid XMI: truncated " + std::string(context));
        }
    };

    auto has_tag = [](const std::uint8_t* cursor, const std::uint8_t* end, std::string_view tag)
    {
        return tag.size() <= static_cast<std::size_t>(end - cursor) && std::equal(tag.begin(), tag.end(), cursor);
    };

    auto skip_bytes = [&](const std::uint8_t*& cursor, const std::uint8_t* end, std::size_t count,
                          std::string_view context)
    {
        need_bytes(cursor, end, count, context);
        cursor += count;
    };

    auto read_be32 = [&](const std::uint8_t*& cursor, const std::uint8_t* end) -> std::uint32_t
    {
        need_bytes(cursor, end, 4, "32-bit integer");
        const std::uint32_t value = (static_cast<std::uint32_t>(cursor[0]) << 24) |
                                    (static_cast<std::uint32_t>(cursor[1]) << 16) |
                                    (static_cast<std::uint32_t>(cursor[2]) << 8) |
                                    static_cast<std::uint32_t>(cursor[3]);
        cursor += 4;
        return value;
    };

    auto append_be32 = [](std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value >> 24));
        bytes.push_back(static_cast<std::uint8_t>(value >> 16));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
        bytes.push_back(static_cast<std::uint8_t>(value));
    };

    auto patch_be32 = [](std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
    {
        bytes[offset] = static_cast<std::uint8_t>(value >> 24);
        bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16);
        bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8);
        bytes[offset + 3] = static_cast<std::uint8_t>(value);
    };

    auto read_varlen = [&](const std::uint8_t*& cursor, const std::uint8_t* end) -> std::uint32_t
    {
        std::uint32_t value = 0;
        for (int byteCount = 0; byteCount < 5; ++byteCount)
        {
            need_bytes(cursor, end, 1, "variable-length integer");
            const std::uint8_t byte = *cursor++;
            value = (value << 7) | (byte & 0x7F);
            if ((byte & 0x80) == 0)
            {
                return value;
            }
        }
        throw std::runtime_error("Invalid XMI: variable-length integer is too large");
    };

    auto append_varlen = [](std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        std::array<std::uint8_t, 5> encoded{};
        std::size_t count = 0;
        encoded[count++] = static_cast<std::uint8_t>(value & 0x7F);

        while ((value >>= 7) != 0)
        {
            encoded[count++] = static_cast<std::uint8_t>((value & 0x7F) | 0x80);
        }

        while (count != 0)
        {
            bytes.push_back(encoded[--count]);
        }
    };

    auto read_xmi_delta = [&](const std::uint8_t*& cursor, const std::uint8_t* end) -> std::uint32_t
    {
        std::uint32_t delay = 0;
        while (cursor != end && *cursor == 0x7F)
        {
            delay += *cursor++;
        }

        need_bytes(cursor, end, 1, "XMI delta");
        return delay + *cursor++;
    };

    auto scale_delta = [](std::uint32_t delta, std::uint32_t quarterNoteMicros) -> std::uint32_t
    {
        const std::uint64_t denominator = static_cast<std::uint64_t>(quarterNoteMicros) * DefaultTimebase;
        if (denominator == 0)
        {
            throw std::runtime_error("Invalid MIDI tempo: zero quarter-note length");
        }

        const std::uint64_t numerator =
            static_cast<std::uint64_t>(delta) * MidiTimebase * DefaultQuarterNoteMicros;
        return static_cast<std::uint32_t>((numerator + denominator / 2) / denominator);
    };

    auto append_scaled_delta = [&](std::vector<std::uint8_t>& midi, std::uint32_t delta,
                                   std::uint32_t quarterNoteMicros)
    {
        append_varlen(midi, scale_delta(delta, quarterNoteMicros));
    };

    auto append_bytes = [&](std::vector<std::uint8_t>& midi, const std::uint8_t*& cursor,
                            const std::uint8_t* end, std::size_t count)
    {
        need_bytes(cursor, end, count, "event payload");
        midi.insert(midi.end(), cursor, cursor + count);
        cursor += count;
    };

    auto channel_event_size = [](std::uint8_t status) -> std::size_t
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
    };

    auto chunk_payload_end = [&](const std::uint8_t* payload, const std::uint8_t* limit,
                                 std::uint32_t length, std::string_view context)
    {
        need_bytes(payload, limit, length, context);
        return payload + length;
    };

    auto next_chunk = [&](const std::uint8_t* payloadEnd, const std::uint8_t* limit, std::uint32_t length)
    {
        const std::uint8_t* next = payloadEnd;
        if ((length & 1U) != 0 && next < limit)
        {
            ++next;
        }
        return next;
    };

    const std::uint8_t* sequenceStart = nullptr;
    const std::uint8_t* sequenceEnd = nullptr;

    auto set_sequence_from_form = [&](const std::uint8_t* payload, const std::uint8_t* chunkEnd,
                                      std::uint32_t length)
    {
        if (length < 4)
        {
            throw std::runtime_error("Invalid XMI: FORM chunk is too small");
        }

        if (!has_tag(payload, chunkEnd, "XMID"))
        {
            return false;
        }

        sequenceStart = payload + 4;
        sequenceEnd = chunkEnd;
        return true;
    };

    auto scan_catalog = [&](const std::uint8_t* payload, const std::uint8_t* chunkEnd, std::uint32_t length)
    {
        if (length < 4)
        {
            throw std::runtime_error("Invalid XMI: CAT chunk is too small");
        }

        if (!has_tag(payload, chunkEnd, "XMID"))
        {
            return false;
        }

        const std::uint8_t* child = payload + 4;
        while (child < chunkEnd)
        {
            need_bytes(child, chunkEnd, 8, "CAT child chunk header");
            const bool isForm = has_tag(child, chunkEnd, "FORM");
            child += 4;
            const std::uint32_t childLength = read_be32(child, chunkEnd);
            const std::uint8_t* const childPayload = child;
            const std::uint8_t* const childEnd =
                chunk_payload_end(childPayload, chunkEnd, childLength, "CAT child chunk");

            if (isForm && set_sequence_from_form(childPayload, childEnd, childLength))
            {
                return true;
            }

            child = next_chunk(childEnd, chunkEnd, childLength);
        }

        return false;
    };

    const std::uint8_t* root = cursor;
    while (root < end && sequenceStart == nullptr)
    {
        need_bytes(root, end, 8, "root IFF chunk header");
        const bool isForm = has_tag(root, end, "FORM");
        const bool isCatalog = has_tag(root, end, "CAT ");
        root += 4;
        const std::uint32_t rootLength = read_be32(root, end);
        const std::uint8_t* const rootPayload = root;
        const std::uint8_t* const rootEnd = chunk_payload_end(rootPayload, end, rootLength, "root IFF chunk");

        if (isForm)
        {
            set_sequence_from_form(rootPayload, rootEnd, rootLength);
        }
        else if (isCatalog)
        {
            scan_catalog(rootPayload, rootEnd, rootLength);
        }

        root = next_chunk(rootEnd, end, rootLength);
    }

    if (sequenceStart == nullptr)
    {
        throw std::runtime_error("Invalid XMI: missing FORM XMID sequence");
    }

    cursor = sequenceStart;
    const std::uint8_t* eventEnd = nullptr;
    while (cursor < sequenceEnd)
    {
        need_bytes(cursor, sequenceEnd, 8, "sequence chunk header");
        const bool isEventChunk = has_tag(cursor, sequenceEnd, "EVNT");
        cursor += 4;
        const std::uint32_t localLength = read_be32(cursor, sequenceEnd);
        const std::uint8_t* const localPayload = cursor;
        const std::uint8_t* const localEnd =
            chunk_payload_end(localPayload, sequenceEnd, localLength, "sequence chunk");

        if (isEventChunk)
        {
            cursor = localPayload;
            eventEnd = localEnd;
            break;
        }

        cursor = next_chunk(localEnd, sequenceEnd, localLength);
    }

    if (eventEnd == nullptr)
    {
        throw std::runtime_error("Invalid XMI: missing EVNT chunk");
    }

    std::vector<std::uint8_t> midi;
    midi.reserve((xmi.size() * 2) + TrackDataOffset);
    midi.insert(midi.end(), {'M', 'T', 'h', 'd'});
    append_be32(midi, 6);
    midi.push_back(0);
    midi.push_back(0);
    midi.push_back(0);
    midi.push_back(1);
    midi.push_back(static_cast<std::uint8_t>(MidiTimebase >> 8));
    midi.push_back(static_cast<std::uint8_t>(MidiTimebase));
    midi.insert(midi.end(), {'M', 'T', 'r', 'k', 0, 0, 0, 0});

    std::array<NoteOffEvent, MaxNoteOffs> noteOffs{};
    std::size_t noteOffCount = 0;
    std::uint32_t quarterNoteMicros = DefaultQuarterNoteMicros;
    bool expectDelta = true;

    auto append_note_off = [&](const NoteOffEvent& event)
    {
        midi.push_back(event.status & 0x8F);
        midi.push_back(event.note);
        midi.push_back(0x7F);
    };

    auto queue_note_off = [&](std::uint32_t delta, std::uint8_t status, std::uint8_t note)
    {
        if (noteOffCount == noteOffs.size())
        {
            throw std::runtime_error("Too many pending note-off events");
        }

        const auto first = noteOffs.begin();
        const auto last = first + static_cast<std::ptrdiff_t>(noteOffCount);
        const auto insertAt = std::upper_bound(first, last, delta, [](std::uint32_t value, const NoteOffEvent& event)
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

        for (std::size_t i = 1; i < noteOffCount; ++i)
        {
            noteOffs[i].delta -= event.delta;
        }

        std::move(noteOffs.begin() + 1, noteOffs.begin() + static_cast<std::ptrdiff_t>(noteOffCount),
                  noteOffs.begin());
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
            std::uint32_t delay = read_xmi_delta(cursor, eventEnd);

            while (noteOffCount != 0 && delay > noteOffs[0].delta)
            {
                delay -= noteOffs[0].delta;
                pop_note_off();
            }

            for (std::size_t i = 0; i < noteOffCount; ++i)
            {
                noteOffs[i].delta -= delay;
            }

            append_scaled_delta(midi, delay, quarterNoteMicros);
            expectDelta = false;
            continue;
        }

        const std::uint8_t status = *cursor;
        if (status == 0xFF)
        {
            need_bytes(cursor, eventEnd, 2, "meta event");
            const std::uint8_t metaType = cursor[1];
            begin_event();

            if (metaType == 0x2F)
            {
                cursor += 2;
                const std::uint32_t metaLength = read_varlen(cursor, eventEnd);
                skip_bytes(cursor, eventEnd, metaLength, "end-of-track payload");

                for (std::size_t i = 0; i < noteOffCount; ++i)
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
            const std::uint8_t* const lengthStart = cursor;
            const std::uint32_t metaLength = read_varlen(cursor, eventEnd);
            midi.insert(midi.end(), lengthStart, cursor);
            need_bytes(cursor, eventEnd, metaLength, "meta payload");

            if (metaType == 0x51 && metaLength == 3)
            {
                quarterNoteMicros = (static_cast<std::uint32_t>(cursor[0]) << 16) |
                                    (static_cast<std::uint32_t>(cursor[1]) << 8) |
                                    static_cast<std::uint32_t>(cursor[2]);
            }

            midi.insert(midi.end(), cursor, cursor + metaLength);
            cursor += metaLength;
        }
        else if (status == 0xF0 || status == 0xF7)
        {
            begin_event();
            midi.push_back(*cursor++);
            const std::uint8_t* const lengthStart = cursor;
            const std::uint32_t sysexLength = read_varlen(cursor, eventEnd);
            midi.insert(midi.end(), lengthStart, cursor);
            append_bytes(midi, cursor, eventEnd, sysexLength);
        }
        else
        {
            const std::size_t eventSize = channel_event_size(status);
            if (eventSize == 0)
            {
                ++cursor;
                expectDelta = true;
                continue;
            }

            begin_event();
            const std::uint8_t eventStatus = cursor[0];
            const std::uint8_t eventNote = eventSize > 1 ? cursor[1] : 0;
            append_bytes(midi, cursor, eventEnd, eventSize);

            if ((eventStatus & 0xF0) == 0x90)
            {
                queue_note_off(read_varlen(cursor, eventEnd), eventStatus, eventNote);
            }
        }
    }

    const std::size_t trackLength = midi.size() - TrackDataOffset;
    if (trackLength > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("MIDI track is too large");
    }

    patch_be32(midi, TrackLengthOffset, static_cast<std::uint32_t>(trackLength));
    return midi;
}
}

#endif
