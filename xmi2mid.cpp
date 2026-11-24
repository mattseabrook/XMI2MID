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

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
struct BatchOptions
{
    bool help = false;
    bool list = false;
    bool recursive = false;
    bool all = false;
    std::optional<std::size_t> sequenceIndex;
    std::filesystem::path outputDirectory = ".";
    std::vector<std::filesystem::path> inputs;
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path)
{
    const std::uintmax_t fileSize = std::filesystem::file_size(path);
    if (fileSize > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()) ||
        fileSize > std::numeric_limits<std::size_t>::max())
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

void write_conversion(const std::filesystem::path& inputPath, const std::filesystem::path& outputPath,
                      std::span<const std::uint8_t> midiData, std::size_t index)
{
    std::ofstream file(outputPath, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Cannot open output file " + outputPath.string());
    }

    file.write(reinterpret_cast<const char*>(midiData.data()), static_cast<std::streamsize>(midiData.size()));
    file.close();
    if (!file)
    {
        throw std::runtime_error("Cannot write output file " + outputPath.string());
    }
}

void convert_file(const std::filesystem::path& inputPath, const std::filesystem::path& outputPath,
                  std::size_t index)
{
    const auto xmiData = read_file(inputPath);
    write_conversion(inputPath, outputPath, xmi2mid::convert(xmiData, index), index);
}

std::size_t parse_sequence_index(std::string_view text)
{
    if (text.empty())
    {
        throw std::runtime_error("Missing sequence index");
    }

    std::size_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (end != text.data() + text.size() || error == std::errc::invalid_argument)
    {
        throw std::runtime_error("Invalid sequence index " + std::string(text));
    }
    if (error == std::errc::result_out_of_range)
    {
        throw std::runtime_error("Sequence index is too large");
    }
    return value;
}

bool is_signed_index(std::string_view text)
{
    if (text.starts_with('-') || text.starts_with('+'))
    {
        text.remove_prefix(1);
    }
    return !text.empty() && text.find_first_not_of("0123456789") == std::string_view::npos;
}

void select_sequences(BatchOptions& options, std::string_view index)
{
    if (!index.empty())
    {
        const bool negative = index.starts_with('-');
        const auto value = parse_sequence_index(index.substr(negative || index.starts_with('+') ? 1 : 0));
        if (!negative || value == 0)
        {
            options.sequenceIndex = value;
            options.all = false;
            return;
        }
    }
    options.sequenceIndex.reset();
    options.all = true;
}

bool is_option(std::string_view text)
{
    return text.size() > 1 && text.front() == '-';
}

BatchOptions parse_batch_options(int argc, char* argv[])
{
    using LongOption = std::pair<std::string_view, std::string_view>;
    constexpr LongOption longOptions[]{
        {"--sequence", "s"}, {"--output-directory", "d"}, {"--list", "l"},
        {"--recursive", "r"}, {"--help", "h"}
    };
    BatchOptions options;
    bool endOfOptions = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (endOfOptions || !is_option(arg))
        {
            options.inputs.emplace_back(argv[i]);
            continue;
        }
        if (arg == "--")
        {
            endOfOptions = true;
            continue;
        }
        if (arg == "--all")
        {
            select_sequences(options, {});
            continue;
        }

        std::string_view flags = arg.substr(1);
        std::string_view value;
        if (arg.starts_with("--"))
        {
            const auto separator = arg.find('=');
            const auto name = arg.substr(0, separator);
            const auto* option = std::ranges::find(longOptions, name, &LongOption::first);
            if (option == std::end(longOptions) ||
                (separator != std::string_view::npos && name != "--sequence" && name != "--output-directory"))
            {
                throw std::runtime_error("Unknown option " + std::string(arg));
            }
            flags = option->second;

            if (separator != std::string_view::npos)
            {
                value = arg.substr(separator + 1);
                if (value.empty())
                {
                    throw std::runtime_error("Missing value for " + std::string(name));
                }
            }
        }

        for (std::size_t j = 0; j < flags.size(); ++j)
        {
            const char flag = flags[j];
            switch (flag)
            {
            case 'h':
                options.help = true;
                continue;
            case 'l':
                options.list = true;
                continue;
            case 'r':
                options.recursive = true;
                continue;
            case 's':
            case 'd':
                break;
            default:
                throw std::runtime_error("Unknown option " + std::string(arg));
            }

            if (j + 1 < flags.size())
            {
                value = flags.substr(j + 1);
            }
            if (flag == 's')
            {
                if (value.empty() && i + 1 < argc && is_signed_index(argv[i + 1]))
                {
                    value = argv[++i];
                }
                select_sequences(options, value);
            }
            else
            {
                if (value.empty() && i + 1 < argc && !is_option(argv[i + 1]))
                {
                    value = argv[++i];
                }
                if (value.empty())
                {
                    throw std::runtime_error("Missing output directory");
                }
                options.outputDirectory = value;
            }
            break;
        }
    }
    return options;
}

struct SequencePaths
{
    std::filesystem::path prefix;
    std::filesystem::path extension;
    std::size_t width;

    std::filesystem::path operator()(std::size_t index) const
    {
        auto number = std::to_string(index);
        if (number.size() < width)
        {
            number.insert(0, width - number.size(), '0');
        }
        auto path = prefix;
        path += "_" + number;
        path += extension;
        return path;
    }
};

SequencePaths sequence_paths(const std::filesystem::path& inputPath,
                             const std::filesystem::path& outputTarget, std::size_t count)
{
    const auto width = std::max<std::size_t>(2, std::to_string(count - 1).size());
    if (std::filesystem::is_directory(outputTarget))
    {
        return {outputTarget / inputPath.stem(), ".mid", width};
    }

    std::filesystem::path extension = outputTarget.extension();
    if (extension.empty())
    {
        extension = ".mid";
    }

    auto stem = outputTarget.stem();
    if (stem.empty())
    {
        stem = inputPath.stem();
    }

    return {outputTarget.parent_path() / stem, extension, width};
}

void print_sequence_list(size_t i, const std::filesystem::path& inputPath, const std::vector<xmi2mid::sequence_info>& sequences, const std::filesystem::path& outputPath = ".")
{
	printf("[%7zu] %s\n", i, inputPath.c_str());
	fprintf(stdout, "[SEQ:IDX] [XMID:OFF|XMID:LEN] [EVNT:OFF|EVNT:LEN] [TIMB|RBRN]\n");
	for (const xmi2mid::sequence_info& sequence : sequences)
	{
		fprintf(stdout, "[%7zu] [%8zu|%8zu] [%8zu|%8zu] [%4s|%4s]\n",
				sequence.index, sequence.form_offset,
				sequence.form_size, sequence.event_offset,
				sequence.event_size, sequence.has_timb ? " on " : "    ",
				sequence.has_rbrn ? " on " : "    ");
	}
	fprintf(stdout, "[%7zu] %s\n", sequences.size(), outputPath.c_str());
	puts("");
}

std::vector<std::filesystem::path> collect_input_files(const BatchOptions& options,
                                                       const std::filesystem::path& excludedDirectory)
{
    std::vector<std::filesystem::path> files;
    for (const auto& input : options.inputs)
    {
        const auto status = std::filesystem::status(input);
        if (std::filesystem::is_regular_file(status))
        {
            files.push_back(input);
        }
        else if (std::filesystem::is_directory(status))
        {
            const auto first = static_cast<std::ptrdiff_t>(files.size());
            auto add_file = [&](const std::filesystem::directory_entry& entry)
            {
                if (entry.is_regular_file())
                {
                    files.push_back(entry.path());
                }
            };

            if (options.recursive)
            {
                for (auto entry = std::filesystem::recursive_directory_iterator(input);
                     entry != std::filesystem::recursive_directory_iterator(); ++entry)
                {
                    if (!excludedDirectory.empty() && entry->is_directory() &&
                        std::filesystem::canonical(entry->path()) == excludedDirectory)
                    {
                        entry.disable_recursion_pending();
                        continue;
                    }
                    add_file(*entry);
                }
            }
            else
            {
                for (const auto& entry : std::filesystem::directory_iterator(input))
                {
                    add_file(entry);
                }
            }
            std::sort(files.begin() + first, files.end());
        }
        else
        {
            throw std::runtime_error("Cannot read input path " + input.string());
        }
    }
    return files;
}

bool has_xmi_extension(const std::filesystem::path& path)
{
    const auto extension = path.extension().string();
    return extension.size() == 4 && extension[0] == '.' &&
           (extension[1] == 'x' || extension[1] == 'X') &&
           (extension[2] == 'm' || extension[2] == 'M') &&
           (extension[3] == 'i' || extension[3] == 'I');
}

int run_batch(const BatchOptions& options, const std::filesystem::path& sourceDirectory = {})
{
    const auto excludedDirectory = sourceDirectory.empty() ? std::filesystem::path{} :
        std::filesystem::weakly_canonical(options.outputDirectory);
    const auto files = collect_input_files(options, excludedDirectory);
    std::set<std::filesystem::path> seenInputs;
    std::set<std::filesystem::path> writtenOutputs;
    std::size_t sourceCount = 0;
    std::size_t sequenceCount = 0;
    std::size_t outputCount = 0;
    bool failed = false;
    for (const auto& inputPath : files)
    {
        try
        {
            if (!seenInputs.insert(std::filesystem::canonical(inputPath)).second)
            {
                continue;
            }
            const auto xmiData = read_file(inputPath);
            std::vector<xmi2mid::sequence_info> sequences;
            try
            {
                sequences = xmi2mid::sequence_infos(xmiData);
            }
            catch (const std::runtime_error&)
            {
                if (has_xmi_extension(inputPath))
                {
                    throw;
                }
                continue;
            }

            ++sourceCount;
            sequenceCount += sequences.size();
            if (options.list)
            {
                print_sequence_list(sourceCount-1, inputPath, sequences, options.outputDirectory);
            }
            if (!options.all && !options.sequenceIndex)
            {
                continue;
            }

            auto outputDirectory = options.outputDirectory;
            if (!sourceDirectory.empty())
            {
                const auto relativeDirectory = inputPath.parent_path().lexically_relative(sourceDirectory);
                if (relativeDirectory != ".")
                {
                    outputDirectory /= relativeDirectory;
                }
            }

            std::filesystem::create_directories(outputDirectory);
            const auto paths = sequence_paths(inputPath, outputDirectory, sequences.size());
            const auto write_sequence = [&](std::span<const std::uint8_t> midiData, std::size_t index)
            {
                const auto outputPath = paths(index);
                if (!writtenOutputs.insert(std::filesystem::weakly_canonical(outputPath)).second)
                {
                    throw std::runtime_error("Multiple inputs produce " + outputPath.string());
                }
                write_conversion(inputPath, outputPath, midiData, index);
                ++outputCount;
            };

            if (options.all)
            {
                const auto midiFiles = xmi2mid::detail::convert_sequences(xmiData, sequences);
                for (std::size_t index = 0; index < midiFiles.size(); ++index)
                {
                    write_sequence(midiFiles[index], index);
                }
            }
            else
            {
                const auto index = *options.sequenceIndex;
                xmi2mid::detail::check_sequence_index(index, sequences.size());
                write_sequence(xmi2mid::detail::convert_sequence(xmiData, sequences[index]), index);
            }
        }
        catch (const std::runtime_error& error)
        {
            std::cerr << "Error: " << inputPath.string() << ": " << error.what() << '\n';
            failed = true;
        }
    }
    if (sourceCount == 0)
    {
        std::cerr << "Error: No XMI files found\n";
        return 1;
    }
    fprintf(stdout, "[XMI:CNT] [ SEQ:CNT| MID:CNT] [DST:DIR]\n");
    fprintf(stdout, "[%7zu] [%8zu|%8zu] %s\n", sourceCount, sequenceCount, outputCount, options.outputDirectory.c_str());
    return failed ? 1 : 0;
}

void print_usage(const char* program)
{
    std::cerr << "Usage:\n"
              << "  " << program << " Reference/AIL2/DEMO.XMI demo.mid\n"
              << "  " << program << " --sequence 0 Reference/AIL2/DEMO.XMI demo.mid\n"
              << "  " << program << " --all Reference/AIL2/DEMO.XMI demo\n"
              << "  " << program << " --all music/ midi/             Convert a folder and its subfolders\n"
              << "  " << program << " --list Reference/AIL2/DEMO.XMI\n"
              << "  " << program << " [options] input...\n"
              << "    -l, --list                      List XMI sequences\n"
              << "    -r, --recursive                 Include subdirectories\n"
              << "    -s, --sequence[=N]              Convert sequence N, or all if omitted or negative\n"
              << "    -d, --output-directory DIR      Write MIDI files to DIR (default: current directory)\n"
              << "    -h, --help                      Show usage\n";
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

        if (command == "--list" && argc == 3 && !is_option(argv[2]) &&
            std::filesystem::is_regular_file(argv[2]))
        {
            const std::filesystem::path inputPath = argv[2];
            const auto xmiData = read_file(inputPath);;
            print_sequence_list(0, inputPath, xmi2mid::sequence_infos(xmiData), ".");
            return 0;
        }

        if (command == "--sequence" && argc == 5 &&
            !is_option(argv[2]) && !std::string_view(argv[2]).starts_with('+') &&
            !is_option(argv[3]) && !is_option(argv[4]))
        {
            const std::size_t sequenceIndex = parse_sequence_index(argv[2]);
            convert_file(argv[3], argv[4], sequenceIndex);
            return 0;
        }

        if (command == "--all" && argc == 4 && !is_option(argv[2]) && !is_option(argv[3]))
        {
            const std::filesystem::path inputPath = argv[2];
            const std::filesystem::path outputTarget = argv[3];
            if (std::filesystem::is_directory(inputPath))
            {
                BatchOptions options;
                options.all = true;
                options.recursive = true;
                options.outputDirectory = outputTarget;
                options.inputs.push_back(inputPath);
                return run_batch(options, inputPath);
            }

            const auto xmiData = read_file(inputPath);
            const auto midiFiles = xmi2mid::convert_all(xmiData);
            const auto paths = sequence_paths(inputPath, outputTarget, midiFiles.size());

            for (std::size_t index = 0; index < midiFiles.size(); ++index)
            {
                write_conversion(inputPath, paths(index), midiFiles[index], index);
            }
            return 0;
        }

        if (argc == 3 && !is_option(command) && !is_option(argv[2]))
        {
            convert_file(argv[1], argv[2], 0);
            return 0;
        }

        const auto options = parse_batch_options(argc, argv);
        if (options.help || options.inputs.empty())
        {
            print_usage(argv[0]);
            return options.help ? 0 : 1;
        }
        return run_batch(options);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
