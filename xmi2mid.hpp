#ifndef XMI2MID_HPP
#define XMI2MID_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace xmi2mid
{
struct sequence_info
{
    std::size_t index = 0;
    std::size_t form_offset = 0;
    std::size_t form_size = 0;
    std::size_t event_offset = 0;
    std::size_t event_size = 0;
    bool has_timb = false;
    bool has_rbrn = false;
};

namespace detail
{
inline void need_bytes(const std::uint8_t* cursor, const std::uint8_t* end, std::size_t count,
                       std::string_view context)
{
    if (cursor > end || count > static_cast<std::size_t>(end - cursor))
    {
        throw std::runtime_error("Invalid XMI: truncated " + std::string(context));
    }
}

inline bool has_tag(const std::uint8_t* cursor, const std::uint8_t* end, std::string_view tag)
{
    return cursor <= end && tag.size() <= static_cast<std::size_t>(end - cursor) &&
           std::equal(tag.begin(), tag.end(), cursor);
}

inline std::uint32_t read_be32(const std::uint8_t*& cursor, const std::uint8_t* end)
{
    need_bytes(cursor, end, 4, "32-bit integer");
    const std::uint32_t value = (static_cast<std::uint32_t>(cursor[0]) << 24) |
                                (static_cast<std::uint32_t>(cursor[1]) << 16) |
                                (static_cast<std::uint32_t>(cursor[2]) << 8) |
                                static_cast<std::uint32_t>(cursor[3]);
    cursor += 4;
    return value;
}

inline const std::uint8_t* chunk_payload_end(const std::uint8_t* payload, const std::uint8_t* limit,
                                             std::uint32_t length, std::string_view context)
{
    need_bytes(payload, limit, length, context);
    return payload + length;
}

inline const std::uint8_t* next_chunk(const std::uint8_t* payloadEnd, const std::uint8_t* limit,
                                      std::uint32_t length)
{
    const std::uint8_t* next = payloadEnd;
    if ((length & 1U) != 0 && next < limit)
    {
        ++next;
    }
    return next;
}

inline std::size_t offset_of(std::span<const std::uint8_t> xmi, const std::uint8_t* cursor)
{
    return static_cast<std::size_t>(cursor - xmi.data());
}

inline void scan_form_xmid(std::span<const std::uint8_t> xmi, const std::uint8_t* chunkStart,
                           const std::uint8_t* payload, const std::uint8_t* chunkEnd,
                           std::uint32_t length, std::vector<sequence_info>& sequences)
{
    if (length < 4)
    {
        throw std::runtime_error("Invalid XMI: FORM chunk is too small");
    }

    if (!has_tag(payload, chunkEnd, "XMID"))
    {
        return;
    }

    sequence_info info{};
    info.index = sequences.size();
    info.form_offset = offset_of(xmi, chunkStart);
    info.form_size = static_cast<std::size_t>(8) + length;

    const std::uint8_t* local = payload + 4;
    while (local < chunkEnd)
    {
        need_bytes(local, chunkEnd, 8, "sequence chunk header");
        const bool isTimb = has_tag(local, chunkEnd, "TIMB");
        const bool isRbrn = has_tag(local, chunkEnd, "RBRN");
        const bool isEvnt = has_tag(local, chunkEnd, "EVNT");
        local += 4;
        const std::uint32_t localLength = read_be32(local, chunkEnd);
        const std::uint8_t* const localPayload = local;
        const std::uint8_t* const localEnd =
            chunk_payload_end(localPayload, chunkEnd, localLength, "sequence chunk");

        if (isTimb)
        {
            info.has_timb = true;
        }
        else if (isRbrn)
        {
            info.has_rbrn = true;
        }
        else if (isEvnt)
        {
            info.event_offset = offset_of(xmi, localPayload);
            info.event_size = localLength;
        }

        local = next_chunk(localEnd, chunkEnd, localLength);
    }

    if (info.event_size == 0)
    {
        throw std::runtime_error("Invalid XMI: FORM XMID is missing EVNT chunk");
    }

    sequences.push_back(info);
}

inline void scan_catalog_xmid(std::span<const std::uint8_t> xmi, const std::uint8_t* payload,
                              const std::uint8_t* chunkEnd, std::uint32_t length,
                              std::vector<sequence_info>& sequences)
{
    if (length < 4)
    {
        throw std::runtime_error("Invalid XMI: CAT chunk is too small");
    }

    if (!has_tag(payload, chunkEnd, "XMID"))
    {
        return;
    }

    const std::uint8_t* child = payload + 4;
    while (child < chunkEnd)
    {
        need_bytes(child, chunkEnd, 8, "CAT child chunk header");
        const std::uint8_t* const childStart = child;
        const bool isForm = has_tag(child, chunkEnd, "FORM");
        child += 4;
        const std::uint32_t childLength = read_be32(child, chunkEnd);
        const std::uint8_t* const childPayload = child;
        const std::uint8_t* const childEnd =
            chunk_payload_end(childPayload, chunkEnd, childLength, "CAT child chunk");

        if (isForm)
        {
            scan_form_xmid(xmi, childStart, childPayload, childEnd, childLength, sequences);
        }

        child = next_chunk(childEnd, chunkEnd, childLength);
    }
}
}

inline std::vector<sequence_info> sequence_infos(std::span<const std::uint8_t> xmi)
{
    if (xmi.empty())
    {
        throw std::runtime_error("Invalid XMI: empty file");
    }

    std::vector<sequence_info> sequences;
    const std::uint8_t* root = xmi.data();
    const std::uint8_t* const end = root + xmi.size();

    while (root < end)
    {
        detail::need_bytes(root, end, 8, "root IFF chunk header");
        const std::uint8_t* const rootStart = root;
        const bool isForm = detail::has_tag(root, end, "FORM");
        const bool isCatalog = detail::has_tag(root, end, "CAT ");
        root += 4;
        const std::uint32_t rootLength = detail::read_be32(root, end);
        const std::uint8_t* const rootPayload = root;
        const std::uint8_t* const rootEnd =
            detail::chunk_payload_end(rootPayload, end, rootLength, "root IFF chunk");

        if (isForm)
        {
            detail::scan_form_xmid(xmi, rootStart, rootPayload, rootEnd, rootLength, sequences);
        }
        else if (isCatalog)
        {
            detail::scan_catalog_xmid(xmi, rootPayload, rootEnd, rootLength, sequences);
        }

        root = detail::next_chunk(rootEnd, end, rootLength);
    }

    if (sequences.empty())
    {
        throw std::runtime_error("Invalid XMI: missing FORM XMID sequence");
    }

    return sequences;
}

inline std::size_t sequence_count(std::span<const std::uint8_t> xmi)
{
    return sequence_infos(xmi).size();
}

inline std::vector<std::uint8_t> convert(std::span<const std::uint8_t> xmi, std::size_t sequenceIndex)
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

    auto need_bytes = [](const std::uint8_t* cursor, const std::uint8_t* end, std::size_t count,
                         std::string_view context)
    {
        if (cursor > end || count > static_cast<std::size_t>(end - cursor))
        {
            throw std::runtime_error("Invalid XMI: truncated " + std::string(context));
        }
    };

    auto skip_bytes = [&](const std::uint8_t*& cursor, const std::uint8_t* end, std::size_t count,
                          std::string_view context)
    {
        need_bytes(cursor, end, count, context);
        cursor += count;
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

    const auto sequences = sequence_infos(xmi);
    if (sequenceIndex >= sequences.size())
    {
        throw std::runtime_error("Invalid XMI: sequence index " + std::to_string(sequenceIndex) +
                                 " is out of range for " + std::to_string(sequences.size()) + " sequence(s)");
    }

    const sequence_info& sequence = sequences[sequenceIndex];
    cursor = xmi.data() + sequence.event_offset;
    const std::uint8_t* const eventEnd = cursor + sequence.event_size;

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

inline std::vector<std::uint8_t> convert(std::span<const std::uint8_t> xmi)
{
    return convert(xmi, 0);
}

inline std::vector<std::vector<std::uint8_t>> convert_all(std::span<const std::uint8_t> xmi)
{
    const auto sequences = sequence_infos(xmi);
    std::vector<std::vector<std::uint8_t>> midis;
    midis.reserve(sequences.size());

    for (const sequence_info& sequence : sequences)
    {
        midis.push_back(convert(xmi, sequence.index));
    }

    return midis;
}
}

#endif
