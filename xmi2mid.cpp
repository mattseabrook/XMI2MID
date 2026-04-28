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

#include "xmi2mid.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
std::vector<std::uint8_t> read_file(const std::filesystem::path& path)
{
    const std::uintmax_t fileSize = std::filesystem::file_size(path);
    if (fileSize > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
    {
        throw std::runtime_error("Input file is too large");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileSize));
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

void write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
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

std::size_t parse_sequence_index(std::string_view text)
{
    if (text.empty())
    {
        throw std::runtime_error("Missing sequence index");
    }

    std::size_t value = 0;
    for (const char ch : text)
    {
        if (ch < '0' || ch > '9')
        {
            throw std::runtime_error("Invalid sequence index " + std::string(text));
        }

        const std::size_t digit = static_cast<std::size_t>(ch - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10)
        {
            throw std::runtime_error("Sequence index is too large");
        }
        value = (value * 10) + digit;
    }
    return value;
}

std::string sequence_suffix(std::size_t index, std::size_t count)
{
    const std::size_t width = std::max<std::size_t>(2, std::to_string(count == 0 ? 0 : count - 1).size());
    std::ostringstream suffix;
    suffix << std::setfill('0') << std::setw(static_cast<int>(width)) << index;
    return suffix.str();
}

std::filesystem::path sequence_output_path(const std::filesystem::path& inputPath,
                                           const std::filesystem::path& outputTarget,
                                           std::size_t index,
                                           std::size_t count)
{
    const std::string suffix = sequence_suffix(index, count);
    if (std::filesystem::exists(outputTarget) && std::filesystem::is_directory(outputTarget))
    {
        return outputTarget / (inputPath.stem().string() + "_" + suffix + ".mid");
    }

    std::filesystem::path extension = outputTarget.extension();
    if (extension.empty())
    {
        extension = ".mid";
    }

    std::string stem = outputTarget.stem().string();
    if (stem.empty())
    {
        stem = inputPath.stem().string();
    }

    return outputTarget.parent_path() / (stem + "_" + suffix + extension.string());
}

void print_sequence_list(const std::filesystem::path& inputPath, const std::vector<xmi2mid::sequence_info>& sequences)
{
    std::cout << inputPath.string() << ": " << sequences.size() << " sequence(s)\n";
    for (const xmi2mid::sequence_info& sequence : sequences)
    {
        std::cout << "  [" << sequence.index << "] "
                  << "FORM offset " << sequence.form_offset
                  << ", FORM bytes " << sequence.form_size
                  << ", EVNT offset " << sequence.event_offset
                  << ", EVNT bytes " << sequence.event_size
                  << ", TIMB " << (sequence.has_timb ? "yes" : "no")
                  << ", RBRN " << (sequence.has_rbrn ? "yes" : "no") << '\n';
    }
}

void print_usage(const char* program)
{
    std::cerr << "Usage:\n"
              << "  " << program << " Reference/AIL2/DEMO.XMI demo.mid\n"
              << "  " << program << " --sequence 0 Reference/AIL2/DEMO.XMI demo.mid\n"
              << "  " << program << " --all Reference/AIL2/DEMO.XMI demo\n"
              << "  " << program << " --list Reference/AIL2/DEMO.XMI\n";
}
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    try
    {
        const std::string_view command = argv[1];

        if (command == "--help" || command == "-h")
        {
            print_usage(argv[0]);
            return 0;
        }

        if (command == "--list")
        {
            if (argc != 3)
            {
                print_usage(argv[0]);
                return 1;
            }

            const std::filesystem::path inputPath = argv[2];
            const auto xmiData = read_file(inputPath);
            print_sequence_list(inputPath, xmi2mid::sequence_infos(xmiData));
            return 0;
        }

        if (command == "--sequence")
        {
            if (argc != 5)
            {
                print_usage(argv[0]);
                return 1;
            }

            const std::size_t sequenceIndex = parse_sequence_index(argv[2]);
            const std::filesystem::path inputPath = argv[3];
            const std::filesystem::path outputPath = argv[4];
            const auto xmiData = read_file(inputPath);
            const auto midiData = xmi2mid::convert(xmiData, sequenceIndex);
            write_file(outputPath, midiData);
            std::cout << "Converted sequence " << sequenceIndex << " from "
                      << inputPath.string() << " to " << outputPath.string() << '\n';
            return 0;
        }

        if (command == "--all")
        {
            if (argc != 4)
            {
                print_usage(argv[0]);
                return 1;
            }

            const std::filesystem::path inputPath = argv[2];
            const std::filesystem::path outputTarget = argv[3];
            const auto xmiData = read_file(inputPath);
            const auto midiFiles = xmi2mid::convert_all(xmiData);

            for (std::size_t index = 0; index < midiFiles.size(); ++index)
            {
                const std::filesystem::path outputPath =
                    sequence_output_path(inputPath, outputTarget, index, midiFiles.size());
                write_file(outputPath, midiFiles[index]);
                std::cout << "Converted sequence " << index << " from "
                          << inputPath.string() << " to " << outputPath.string() << '\n';
            }
            return 0;
        }

        if (argc == 3)
        {
            const std::filesystem::path inputPath = argv[1];
            const std::filesystem::path outputPath = argv[2];
            const auto xmiData = read_file(inputPath);
            const auto midiData = xmi2mid::convert(xmiData);
            write_file(outputPath, midiData);
            std::cout << "Converted sequence 0 from "
                      << inputPath.string() << " to " << outputPath.string() << '\n';
            return 0;
        }

        print_usage(argv[0]);
        return 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
