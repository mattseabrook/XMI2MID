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
    if (count > static_cast<std::size_t>(end - cursor))
    {
        throw std::runtime_error("Invalid XMI: truncated " + std::string(context));
    }
}

inline std::string_view fourcc(const std::uint8_t* bytes)
{
    return {reinterpret_cast<const char*>(bytes), 4};
}

inline std::uint32_t read_be(const std::uint8_t* bytes, std::size_t count)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        value = (value << 8) | bytes[i];
    }
    return value;
}

struct Chunk
{
    std::string_view tag;
    const std::uint8_t* data;
    const std::uint8_t* end;

    std::size_t size() const { return static_cast<std::size_t>(end - data); }
};

inline Chunk read_chunk(const std::uint8_t*& cursor, const std::uint8_t* end,
                        std::string_view header, std::string_view payload)
{
    need_bytes(cursor, end, 8, header);
    const auto tag = fourcc(cursor);
    const auto length = read_be(cursor + 4, 4);
    const auto* data = cursor + 8;
    need_bytes(data, end, length, payload);
    cursor = data + length;
    const Chunk chunk{tag, data, cursor};
    if ((length & 1U) != 0 && cursor < end)
    {
        ++cursor;
    }
    return chunk;
}

template <typename Visitor>
inline std::size_t scan_sequences(std::span<const std::uint8_t> xmi, Visitor visit)
{
    if (xmi.empty())
    {
        throw std::runtime_error("Invalid XMI: empty file");
    }

    std::size_t count = 0;
    const auto scan_form = [&](const Chunk& form)
    {
        if (form.size() < 4)
        {
            throw std::runtime_error("Invalid XMI: FORM chunk is too small");
        }
        if (fourcc(form.data) != "XMID")
        {
            return;
        }

        sequence_info info{};
        info.index = count;
        info.form_offset = static_cast<std::size_t>((form.data - 8) - xmi.data());
        info.form_size = 8 + form.size();
        const auto* local = form.data + 4;
        while (local < form.end)
        {
            const auto chunk = read_chunk(local, form.end, "sequence chunk header", "sequence chunk");
            if (chunk.tag == "TIMB")
            {
                info.has_timb = true;
            }
            else if (chunk.tag == "RBRN")
            {
                info.has_rbrn = true;
            }
            else if (chunk.tag == "EVNT")
            {
                info.event_offset = static_cast<std::size_t>(chunk.data - xmi.data());
                info.event_size = chunk.size();
            }
        }
        if (info.event_size == 0)
        {
            throw std::runtime_error("Invalid XMI: FORM XMID is missing EVNT chunk");
        }
        visit(info);
        ++count;
    };

    const auto* root = xmi.data();
    const auto* end = root + xmi.size();
    while (root < end)
    {
        const auto chunk = read_chunk(root, end, "root IFF chunk header", "root IFF chunk");
        if (chunk.tag == "FORM")
        {
            scan_form(chunk);
        }
        else if (chunk.tag == "CAT ")
        {
            if (chunk.size() < 4)
            {
                throw std::runtime_error("Invalid XMI: CAT chunk is too small");
            }
            if (fourcc(chunk.data) != "XMID")
            {
                continue;
            }

            const auto* child = chunk.data + 4;
            while (child < chunk.end)
            {
                const auto form = read_chunk(child, chunk.end, "CAT child chunk header", "CAT child chunk");
                if (form.tag == "FORM")
                {
                    scan_form(form);
                }
            }
        }
    }
    if (count == 0)
    {
        throw std::runtime_error("Invalid XMI: missing FORM XMID sequence");
    }
    return count;
}

inline void check_sequence_index(std::size_t index, std::size_t count)
{
    if (index >= count)
    {
        throw std::runtime_error("Invalid XMI: sequence index " + std::to_string(index) +
                                 " is out of range for " + std::to_string(count) + " sequence(s)");
    }
}

inline std::uint32_t read_varlen(const std::uint8_t*& cursor, const std::uint8_t* end)
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
}

inline std::size_t channel_event_size(std::uint8_t status)
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

inline std::vector<std::uint8_t> convert_sequence(std::span<const std::uint8_t> xmi,
                                                  const sequence_info& sequence)
{
    struct NoteOffEvent
    {
        std::uint64_t tick;
        std::uint32_t order;
        std::uint8_t status;
        std::uint8_t note;
    };

    constexpr std::size_t MaxNoteOffs = 1000;
    constexpr std::uint32_t DefaultTempo = 120;
    constexpr std::uint32_t XmiFreq = 120;
    constexpr std::uint32_t DefaultQuarterNoteMicros = 60 * 1'000'000 / DefaultTempo;
    constexpr std::uint16_t MidiTimebase = 960;
    constexpr std::uint32_t TickScale = MidiTimebase / (XmiFreq * 60 / DefaultTempo);
    constexpr std::size_t TrackLengthOffset = 18;
    constexpr std::array<std::uint8_t, 22> MidiHeader{
        'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1,
        static_cast<std::uint8_t>(MidiTimebase >> 8), static_cast<std::uint8_t>(MidiTimebase),
        'M', 'T', 'r', 'k', 0, 0, 0, 0
    };

    const auto events = xmi.subspan(sequence.event_offset, sequence.event_size);
    const auto* cursor = events.data();
    const auto* const eventEnd = cursor + events.size();
    std::vector<std::uint8_t> midi;
    const auto inputSize = std::min(events.size(), (midi.max_size() - MidiHeader.size()) / 2);
    midi.reserve(MidiHeader.size() + 2 * inputSize);
    midi.insert(midi.end(), MidiHeader.begin(), MidiHeader.end());

    std::array<NoteOffEvent, MaxNoteOffs> noteOffs;
    auto first = noteOffs.begin();
    auto last = first;
    std::uint64_t tick = 0;
    std::uint32_t noteOrder = 0;
    std::uint32_t quarterNoteMicros = DefaultQuarterNoteMicros;
    bool expectDelta = true;

    // Keep the order in which notes started when their end ticks match.
    const auto later = [](const NoteOffEvent& left, const NoteOffEvent& right)
    {
        return left.tick != right.tick ? left.tick > right.tick : left.order > right.order;
    };

    const auto append_delta = [&](std::uint32_t delta)
    {
        if (quarterNoteMicros == 0)
        {
            throw std::runtime_error("Invalid MIDI tempo: zero quarter-note length");
        }

        std::uint32_t value = delta * TickScale;
        if (quarterNoteMicros != DefaultQuarterNoteMicros)
        {
            const auto numerator = static_cast<std::uint64_t>(delta) * TickScale * DefaultQuarterNoteMicros;
            value = static_cast<std::uint32_t>((numerator + quarterNoteMicros / 2) / quarterNoteMicros);
        }
        if (value < 0x80)
        {
            midi.push_back(static_cast<std::uint8_t>(value));
            return;
        }

        std::array<std::uint8_t, 5> encoded;
        auto digit = encoded.end();
        *--digit = static_cast<std::uint8_t>(value & 0x7F);
        while ((value >>= 7) != 0)
        {
            *--digit = static_cast<std::uint8_t>((value & 0x7F) | 0x80);
        }
        midi.insert(midi.end(), digit, encoded.end());
    };

    const auto pop_note_off = [&]
    {
        std::pop_heap(first, last, later);
        --last;
        return *last;
    };

    const auto append_note_off = [&](const NoteOffEvent& event)
    {
        midi.push_back(event.status);
        midi.push_back(event.note);
        midi.push_back(0x7F);
    };

    while (cursor < eventEnd)
    {
        if (*cursor < 0x80)
        {
            std::uint32_t delay = 0;
            while (cursor != eventEnd && *cursor == 0x7F)
            {
                delay += *cursor++;
            }
            need_bytes(cursor, eventEnd, 1, "XMI delta");
            const auto targetTick = tick + (delay + *cursor++);

            while (first != last && first->tick < targetTick)
            {
                const auto event = pop_note_off();
                append_delta(static_cast<std::uint32_t>(event.tick - tick));
                append_note_off(event);
                tick = event.tick;
            }
            append_delta(static_cast<std::uint32_t>(targetTick - tick));
            tick = targetTick;
            expectDelta = false;
            continue;
        }

        const auto* start = cursor;
        const auto status = *cursor;
        const auto eventSize = channel_event_size(status);
        if (eventSize == 0 && status != 0xFF && status != 0xF0 && status != 0xF7)
        {
            ++cursor;
            expectDelta = true;
            continue;
        }

        if (status == 0xFF)
        {
            need_bytes(cursor, eventEnd, 2, "meta event");
        }
        if (expectDelta)
        {
            append_delta(0);
        }
        expectDelta = true;

        if (eventSize != 0)
        {
            need_bytes(cursor, eventEnd, eventSize, "event payload");
            cursor += eventSize;
            midi.insert(midi.end(), start, cursor);
            if ((status & 0xF0) == 0x90)
            {
                const auto duration = read_varlen(cursor, eventEnd);
                if (last == noteOffs.end())
                {
                    throw std::runtime_error("Too many pending note-off events");
                }
                *last++ = {tick + duration, noteOrder++, static_cast<std::uint8_t>(status & 0x8F), start[1]};
                std::push_heap(first, last, later);
            }
            continue;
        }

        const bool isMeta = status == 0xFF;
        cursor += isMeta ? 2 : 1;
        const auto payloadSize = read_varlen(cursor, eventEnd);
        if (isMeta && start[1] == 0x2F)
        {
            need_bytes(cursor, eventEnd, payloadSize, "end-of-track payload");
            while (first != last)
            {
                append_note_off(pop_note_off());
                append_delta(0);
            }
            midi.insert(midi.end(), {0xFF, 0x2F, 0});
            break;
        }

        need_bytes(cursor, eventEnd, payloadSize, isMeta ? "meta payload" : "event payload");
        if (isMeta && start[1] == 0x51 && payloadSize == 3)
        {
            quarterNoteMicros = read_be(cursor, 3);
        }
        cursor += payloadSize;
        midi.insert(midi.end(), start, cursor);
    }

    const auto trackLength = midi.size() - MidiHeader.size();
    if (trackLength > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("MIDI track is too large");
    }
    for (std::size_t byte = 0; byte < 4; ++byte)
    {
        midi[TrackLengthOffset + byte] = static_cast<std::uint8_t>(trackLength >> (24 - 8 * byte));
    }
    return midi;
}

inline std::vector<std::vector<std::uint8_t>> convert_sequences(std::span<const std::uint8_t> xmi,
                                                                 std::span<const sequence_info> sequences)
{
    std::vector<std::vector<std::uint8_t>> midis;
    midis.reserve(sequences.size());
    for (const auto& sequence : sequences)
    {
        midis.push_back(convert_sequence(xmi, sequence));
    }
    return midis;
}
}

inline std::vector<sequence_info> sequence_infos(std::span<const std::uint8_t> xmi)
{
    std::vector<sequence_info> sequences;
    detail::scan_sequences(xmi, [&](const sequence_info& info) { sequences.push_back(info); });
    return sequences;
}

inline std::size_t sequence_count(std::span<const std::uint8_t> xmi)
{
    return detail::scan_sequences(xmi, [](const sequence_info&) {});
}

inline std::vector<std::uint8_t> convert(std::span<const std::uint8_t> xmi, std::size_t sequenceIndex)
{
    sequence_info sequence{};
    const auto count = detail::scan_sequences(xmi, [&](const sequence_info& info)
    {
        if (info.index == sequenceIndex)
        {
            sequence = info;
        }
    });
    detail::check_sequence_index(sequenceIndex, count);
    return detail::convert_sequence(xmi, sequence);
}

inline std::vector<std::uint8_t> convert(std::span<const std::uint8_t> xmi)
{
    return convert(xmi, 0);
}

inline std::vector<std::vector<std::uint8_t>> convert_all(std::span<const std::uint8_t> xmi)
{
    return detail::convert_sequences(xmi, sequence_infos(xmi));
}
}

#endif
