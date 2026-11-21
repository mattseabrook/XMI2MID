#pragma once

#include "xmi2mid.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <getopt.h>

using namespace std::filesystem;
using namespace std;

namespace
{

    bool is_number(const string &str) {
        return !str.empty() && (isdigit(str[0]) || (str[0] == '-') || (str[0] == '+')) && (str.find_first_not_of("0123456789",1) == string::npos);
    }

    vector<uint8_t> read_file(const path& path)
    {
        const size_t fileSize = file_size(path);
        vector<uint8_t> bytes;
        if(fileSize > 0)
        {
            bytes.resize(fileSize);

            if(!bytes.empty())
            {
                FILE* file = fopen(path.c_str(), "rb");
                if(!file || (fread(bytes.data(), static_cast<size_t>(fileSize), 1, file) != 1))
                {
                    perror(("Cannot open input file " + path.string()).c_str());
                    bytes.clear();
                }
                if(file) fclose(file);
            }
        }
        return bytes;
    }

    bool write_file(const path& path, span<const uint8_t> bytes)
    {
        bool err = false;
        FILE* file = fopen(path.c_str(), "wb");
        if(file)
        {
            if ((err = (fwrite(bytes.data(), bytes.size(), 1, file) != 1)))
                perror(("Cannot open output file " + path.string()).c_str());
            fclose(file);
        }
        return !err;
    }

    long parse_sequence_index(string_view text)
    {
        long value = -1;

        if (!text.empty())
            if(sscanf(string{text}.c_str(), "%zd", &value) != 1)
                perror(("Cannot parse sequence index " + string{text}).c_str());

        return value;
    }

    string sequence_suffix(size_t index, size_t count)
    {
        const size_t width = max<size_t>(2, to_string(count == 0 ? 0 : count - 1).size());
        std::vector<char> suffix;
        suffix.resize(width);
        sprintf(suffix.data(),"%0*zu", (int)width, index);
        return std::string{suffix.data()};
    }

    path sequence_output_path(const path& inputPath,
            const path& outputTarget,
            size_t index,
            size_t count)
    {
        const string suffix = sequence_suffix(index, count);
        if (exists(outputTarget) && is_directory(outputTarget))
        {
            return outputTarget / (inputPath.stem().string() + "_" + suffix + ".mid");
        }

        path extension = outputTarget.extension();
        if (extension.empty())
        {
            extension = ".mid";
        }

        string stem = outputTarget.stem().string();
        if (stem.empty())
        {
            stem = inputPath.stem().string();
        }

        return outputTarget.parent_path() / (stem + "_" + suffix + extension.string());
    }
};

namespace cfg
{
    bool              seq = false;
    bool              lst = false;
    bool              rec = false;
    ssize_t           idx = -1;

    struct
    {
        size_t source = 0;        
        size_t listed = 0;
        size_t output = 0;
    } cnt;

    vector<pair<path,vector<uint8_t>>> src;
    path              dst = current_path();

    /* (Recursivly) list directory of supported formats */
    void ls(path p)
    {
        p = canonical(absolute(p));
        if(is_directory(p))
        {
            directory_iterator dir_iter(p);
            while(dir_iter != end(dir_iter))
            {
                const directory_entry& dir_entry = *dir_iter++;
                if(is_regular_file(dir_entry.path()) || (is_directory(dir_entry.path()) && cfg::rec))
                    ls(dir_entry.path());
            }
        }
        else if(exists(p) && file_size(p) > 8 && is_regular_file(p))
        {
            const vector<uint8_t>& tmp = read_file(p);
            try
            {
                xmi2mid::sequence_infos(tmp);
                cfg::src.push_back({p,tmp});
            }
            catch(const runtime_error& error)
            {
            }
        }
    }
    size_t print_sequence_list(const path& inputPath, const vector<xmi2mid::sequence_info>& sequences)
    {
        if(cfg::lst)
        {
            fprintf(stdout, "%s: %zu sequence(s)\n", inputPath.c_str(), sequences.size());
            fprintf(stdout, "[SEQ:IDX] [XMID:OFF|XMID:LEN] [EVNT:OFF|EVNT:LEN] [TIMB|RBRN]\n");
            for (const xmi2mid::sequence_info& sequence : sequences)
            {
                fprintf(stdout, "[%7zu] [%8zu|%8zu] [%8zu|%8zu] [%4s|%4s]\n",
                        sequence.index, sequence.form_offset,
                        sequence.form_size, sequence.event_offset,
                        sequence.event_size, sequence.has_timb ? " on " : "    ",
                        sequence.has_rbrn ? " on " : "    ");
            }
            puts("");
        }
        return sequences.size();
    }

    void print_sequence_summary()
    {
        fprintf(stdout, "[SEQ:IDX] [ SEQ:CNT| SEQ:OUT] [DST:DIR]\n");
        fprintf(stdout, "[%7zd] [%8zu|%8zu] %s\n", cfg::idx, cfg::cnt.listed, cfg::cnt.output, cfg::dst.c_str());
    }

    void print_usage(const char* program)
    {
        fprintf(stderr, "Usage: %s [OPTION]... [FILE]...\n", program);
        fprintf(stderr, "Mandatory arguments to long options are mandatory for short options too.\n");
        fprintf(stderr, "  -d, --output-directory=[DEST]\n        output files to DEST        \n");
        fprintf(stderr, "  -h, --help\n        print this usage information\n");
        fprintf(stderr, "  -l, --list\n        print sequence infos        \n");
        fprintf(stderr, "  -r, --recursive\n        recurse subdirectories      \n");
        fprintf(stderr, "  -s, --sequence=[IDX]\n        convert sequence IDX, < 0 means all\n");
    }

    bool program_args(int argc, char** argv)
    {
        if(argc < 2)
        {
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
        }

        int opt;
        int option_index;

        static struct option long_options[] = {
            { "list", no_argument, 0, 'l' },
            { "recursive", no_argument, 0, 'r' },
            { "sequence", optional_argument, 0, 's' },
            { "output-directory", required_argument, 0, 'd' },
            { NULL, 0, NULL, 0 },
        };

        while((opt = getopt_long(argc, argv, "hrld:s:", long_options, &option_index)) != -1)
        {
            switch(opt)
            {
                case 'r':
                    cfg::rec = true;
                    break;
                case 'l':
                    cfg::lst = true;
                    break;
                case 's':
                    cfg::seq = true;
                    cfg::idx = optarg != NULL && is_number(string{optarg}) ? parse_sequence_index(optarg) : -1;
                    break;
                case 'd':
                    cfg::dst = optarg == NULL ? current_path() : weakly_canonical(optarg);
                    break;
                case 'h':
                    print_usage(argv[0]);
                    exit(EXIT_SUCCESS);
                    break;
                default:
                    print_usage(argv[0]);
                    exit(EXIT_FAILURE);
                    break;
            };
        }

        for(int i = optind; i < argc; i++)
            cfg::ls(argv[i]);

        if(cfg::src.empty()) { fputs("No XMI file(s) found!\n",stderr); return false; }

        cfg::cnt.source = cfg::src.size();

        cfg::cnt.listed = 0;
        for(size_t i = 0; i < cfg::cnt.source; i++)
        {
            cfg::cnt.listed += cfg::print_sequence_list(cfg::src[i].first, xmi2mid::sequence_infos(cfg::src[i].second));
        }

        return true;
    }

    bool sequences_convert()
    {
        cfg::cnt.output = 0;
        for(size_t i = 0; i < cfg::src.size(); i++)
        {
            const vector<vector<uint8_t>>& midiFiles = cfg::idx < 0 ? xmi2mid::convert_all(cfg::src[i].second) :
                vector<vector<uint8_t>>(1,xmi2mid::convert(cfg::src[i].second, cfg::idx));

            for (size_t index = 0; index < midiFiles.size(); ++index)
            {
                const path dst_path = sequence_output_path(cfg::src[i].first, cfg::dst, cfg::idx < 0 ? index : cfg::idx, midiFiles.size());
                if(!write_file(dst_path, midiFiles[index])) return false;
                cfg::cnt.output++;
            }
        }
        return true;
    }

};
