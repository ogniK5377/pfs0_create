#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <tuple>
#include <vector>
#include <stdint.h>

namespace fs = std::experimental::filesystem;

struct PFS0Header {
    uint32_t magic;
    uint32_t file_count;
    uint32_t string_table_sz;
    uint32_t padding{};
};

struct FileHeader {
    uint64_t file_offset;
    uint64_t file_sz;
    uint32_t string_table_offset;
    uint32_t padding{};
};

std::string GetFilename(const std::string& file_path) {
    return file_path.substr(file_path.find_last_of("/\\") + 1);
}

constexpr uint32_t MakeMagic(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (d << 24) | (c << 16) | (b << 8) | a;
}

void FastAppend(std::ofstream& dst, std::ifstream& src) {
    const size_t buffer_size = 1000000; // File buffer(1MB)
    std::array<char, buffer_size> memory_buffer{};
    while (!src.eof()) {
        size_t bytes_read =
            static_cast<size_t>(src.read(memory_buffer.data(), buffer_size).gcount());
        dst.write(memory_buffer.data(), bytes_read);
    }
}

int main(int argc, char** argv) {
    const auto begin = std::chrono::system_clock::now();
    if (argc < 3) {
        std::cerr << argv[0] << " <input dir> <output file>" << std::endl;
        return 0;
    }
    std::string input_path{argv[1]};
    const std::string output_file{argv[2]};

    if (!fs::exists(input_path)) {
        std::cerr << "File path \"" << input_path << "\" does not exist!" << std::endl;
        return 0;
    }

    const auto dir_perms = fs::status(input_path);
    if (dir_perms.type() != fs::file_type::directory) {
        std::cerr << "\"" << input_path << "\" is not a directory!" << std::endl;
        return 0;
    }

    PFS0Header header{};
    header.magic = MakeMagic('P', 'F', 'S', '0');
    std::vector<std::tuple<std::ifstream, std::string>> file_handles{};
    const auto dir_iterator = fs::directory_iterator(input_path);

    std::cout << "> PFS0 Header Generation" << std::endl;

    // Grab file handles and prepare PFS0 header
    std::for_each(fs::begin(dir_iterator), fs::end(dir_iterator),
                  [&file_handles, &header](const fs::directory_entry& file) {
                      if (file.status().type() == fs::file_type::regular) {
                          const std::string file_str = file.path().string();
                          const std::string clean_file_str = GetFilename(file_str);
                          file_handles.push_back(std::make_tuple(
                              std::ifstream(file_str, std::ios::binary), clean_file_str));
                          header.string_table_sz +=
                              static_cast<uint32_t>(clean_file_str.length() + 1);
                          header.file_count++;
                      }
                  });
    std::ofstream nsp_fp(output_file, std::ios::binary);
    nsp_fp.write(reinterpret_cast<const char*>(&header), sizeof(PFS0Header));

    std::vector<std::string> string_table{};
    size_t file_offset{};
    size_t string_table_sz{};
    std::cout << "> Building string table" << std::endl;

    // Build the file section and the string table
    std::for_each(file_handles.begin(), file_handles.end(),
                  [&nsp_fp, &file_offset, &string_table,
                   &string_table_sz](std::tuple<std::ifstream, std::string>& entry) {
                      auto& file = std::get<0>(entry);
                      auto& filename = std::get<1>(entry);
                      FileHeader file_header{};
                      string_table.push_back(filename + '\0');
                      file.seekg(0, std::ios_base::end);
                      size_t file_length{static_cast<size_t>(file.tellg())};

                      file_header.file_offset = static_cast<uint64_t>(file_offset);
                      file_header.file_sz = static_cast<uint64_t>(file_length);
                      file_header.string_table_offset = static_cast<uint32_t>(string_table_sz);

                      nsp_fp.write(reinterpret_cast<const char*>(&file_header), sizeof(FileHeader));

                      string_table_sz += static_cast<size_t>(filename.length() + 1);
                      file_offset += file_length;
                      file.seekg(0, std::ios_base::beg);
                  });

    // Write our string table out
    std::for_each(string_table.begin(), string_table.end(),
                  [&nsp_fp](const std::string& str) { nsp_fp.write(str.c_str(), str.length()); });

    std::cout << "> Adding file entries:" << std::endl;

    // Actually write our data
    const size_t indentation_level = 6;
    std::ios::sync_with_stdio(false); // Allow streams to flush their buffers independently
    std::for_each(file_handles.begin(), file_handles.end(),
                  [&nsp_fp, &indentation_level](std::tuple<std::ifstream, std::string>& entry) {
                      auto& file = std::get<0>(entry);
                      auto& filename = std::get<1>(entry);
                      std::fill_n(std::ostreambuf_iterator<char>{std::cout}, indentation_level,
                                  ' ');
                      std::cout << filename << " ...";
                      // nsp_fp << file.rdbuf();
                      FastAppend(nsp_fp,
                                 file); // Buffering instead of iterating per
                                        // character is significantly faster
                      std::cout << "done" << std::endl;
                  });
    const auto finish = std::chrono::system_clock::now();

    const auto difference = finish - begin;
    std::cout << "PFS0 file \"" << output_file << "\" is now built" << std::endl;
    std::cout << "Took "
              << std::chrono::duration_cast<std::chrono::milliseconds>(difference).count() << "ms"
              << std::endl;
    return 0;
}
