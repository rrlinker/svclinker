#include "coff.h"

template<typename T>
std::istream& binary_read(std::istream &is, T &out) {
    return is.read((char*)&out, sizeof(T));
}

coff::coff(fs::path path) :
    path_(std::move(path))
{
    file_.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    file_.open(path_);

    read_file_header();
    read_sections_headers();
    read_symbols();
    read_string_table();
}

void coff::read_file_header() {
    file_.seekg(0, file_.beg);
    binary_read(file_, file_header_);
}

void coff::read_sections_headers() {
    file_.seekg(sizeof(IMAGE_FILE_HEADER), file_.beg);
    sections_.resize(file_header_.NumberOfSections);
    for (auto &section : sections_) {
        binary_read(file_, section);
    }
}
void coff::read_symbols() {
    if (file_header_.NumberOfSymbols) {
        symbols_.resize(file_header_.NumberOfSymbols);
        file_.seekg(file_header_.PointerToSymbolTable, file_.beg);
        for (auto &symbol : symbols_) {
            binary_read(file_, symbol);
        }
    }
}

void coff::read_string_table() {
    std::streamoff string_table_offset =
        file_header_.PointerToSymbolTable
        + file_header_.NumberOfSymbols * sizeof(IMAGE_SYMBOL);

    DWORD sizeof_table;

    file_.seekg(string_table_offset, file_.beg);
    binary_read(file_, sizeof_table);

    string_table_.resize(sizeof_table);

    file_.seekg(string_table_offset, file_.beg);
    file_.read(string_table_.data(), sizeof_table);
}

std::experimental::string_view coff::get_symbol_name(IMAGE_SYMBOL const &symbol) const {
    if (symbol.N.Name.Short) {
        const char *name = (const char*)symbol.N.ShortName;
        size_t len = strnlen(name, sizeof(IMAGE_SYMBOL::N));
        return std::experimental::string_view(name, len);
    } else {
        return &string_table_[symbol.N.Name.Long];
    }
}

bool coff::symbol_is_exported(IMAGE_SYMBOL const &symbol) const {
    return symbol.SectionNumber > 0;
}

coff::exports coff::get_exports() const {
    exports exs;
    for (int i = 0; i < symbols_.size(); i++) {
        IMAGE_SYMBOL const &symbol = symbols_[i];
        if (!symbol_is_exported(symbol))
            continue;

        auto name = get_symbol_name(symbol);
        exs.emplace(name, symbol.Value);

        // No Aux Symbols are supported yet
        if (symbol.NumberOfAuxSymbols) {
            std::cerr << "COFF(" << path_ << ") Ignoring " << symbol.NumberOfAuxSymbols << " AUX symbols @ " << i << '\n';
            i += symbol.NumberOfAuxSymbols;
        }
    }
    return exs;
}

coff::imports coff::get_imports() const {
    imports ims;
    for (int i = 0; i < symbols_.size(); i++) {
        IMAGE_SYMBOL const &symbol = symbols_[i];
        if (symbol_is_exported(symbol))
            continue;
        
        auto name = get_symbol_name(symbol);
        ims.emplace_back(name);

        // No Aux Symbols are supported yet
        if (symbol.NumberOfAuxSymbols) {
            std::cerr << "COFF(" << path_ << ") Ignoring " << symbol.NumberOfAuxSymbols << " AUX symbols @ " << i << '\n';
            i += symbol.NumberOfAuxSymbols;
        }
    }
    return ims;
}

std::vector<char> coff::read_section_data(IMAGE_SECTION_HEADER const &section) {
    std::vector<char> data;
    data.resize(section.SizeOfRawData);
    file_.seekg(section.PointerToRawData, file_.beg);
    file_.read(data.data(), section.SizeOfRawData);
    return data;
}
