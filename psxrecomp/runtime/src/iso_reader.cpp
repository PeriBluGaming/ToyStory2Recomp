#include "iso_reader.h"
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace PS1 {

// PS1 CD-ROM sector size (Mode 2, Form 1 user data)
constexpr size_t SECTOR_SIZE = 2048;

// Full sector size including headers/subchannel (2352 bytes for raw BIN files)
constexpr size_t RAW_SECTOR_SIZE = 2352;

// Offset to user data in raw sector (Mode 2, Form 1)
constexpr size_t RAW_DATA_OFFSET = 24;

// Primary Volume Descriptor location
constexpr uint32_t PVD_SECTOR = 16;

ISOReader::ISOReader()
    : is_open_(false) {
    root_dir_.lba = 0;
    root_dir_.size = 0;
}

ISOReader::~ISOReader() {
    Close();
}

bool ISOReader::Open(const std::string& filename) {
    // Close any previously opened image (also resets tracks/segments)
    Close();

    // Check if file exists
    if (!std::filesystem::exists(filename)) {
        return false;
    }

    // Ordered list of BINARY files backing the disc. A bare .bin/.iso is a
    // single-entry list; a .cue contributes one entry per FILE line (redump
    // multi-track dumps ship one file per track).
    std::vector<std::string> bin_files;

    // Tracks parsed from the cue. INDEX times in a cue are relative to the
    // OWNING FILE, so remember which file each track belongs to and convert
    // to disc-relative LBAs once the segment table (with each file's first
    // disc sector) is built below.
    struct PendingTrack {
        int      number;
        bool     is_audio;
        size_t   file_index;  // index into bin_files
        uint32_t index01;     // INDEX 01 as a file-relative LBA
        uint32_t index00;     // INDEX 00 pregap, file-relative
        bool     has_index00;
    };
    std::vector<PendingTrack> pending_tracks;

    auto ends_with = [](const std::string& s, const std::string& suffix) {
        return s.size() >= suffix.size() &&
               s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    if (ends_with(filename, ".cue") || ends_with(filename, ".CUE")) {
        // Parse the .cue: resolve every FILE, AND build the track TOC from
        // the TRACK/INDEX lines. Single-file cues keep their historical
        // behavior; multi-file cues (data track + CD-DA audio tracks in
        // separate .bin files) map each file to a contiguous run of disc
        // sectors, concatenated in cue order.
        std::ifstream cue_file(filename);
        if (!cue_file.is_open()) {
            return false;
        }

        std::string line;
        int  cur_track_num   = -1;
        bool cur_track_audio = false;
        uint32_t cur_index00 = 0;
        bool cur_has_index00 = false;
        while (std::getline(cue_file, line)) {
            // FILE "filename.bin" BINARY
            size_t file_pos = line.find("FILE");
            if (file_pos != std::string::npos) {
                size_t quote1 = line.find('"', file_pos);
                size_t quote2 = (quote1 == std::string::npos)
                                    ? std::string::npos : line.find('"', quote1 + 1);
                if (quote1 != std::string::npos && quote2 != std::string::npos) {
                    // Only raw BINARY payloads are supported (WAVE/MP3 audio
                    // files have no fixed sector geometry). Mis-mapping the
                    // TOC would be worse than refusing the image.
                    if (line.find("BINARY", quote2) == std::string::npos) {
                        return false;
                    }
                    std::string bin_name = line.substr(quote1 + 1, quote2 - quote1 - 1);
                    std::filesystem::path cue_path(filename);
                    std::filesystem::path bin_path(bin_name);
                    if (bin_path.is_relative()) {
                        bin_files.push_back((cue_path.parent_path() / bin_name).string());
                    } else {
                        bin_files.push_back(bin_name);
                    }
                }
                continue;
            }

            // TRACK NN MODE2/2352 | TRACK NN AUDIO
            int  tn = 0;
            char type[32] = {0};
            if (std::sscanf(line.c_str(), " TRACK %d %31s", &tn, type) == 2) {
                cur_track_num   = tn;
                cur_track_audio = (std::strstr(type, "AUDIO") != nullptr);
                cur_index00 = 0;
                cur_has_index00 = false;
                continue;
            }

            // INDEX 01 MM:SS:FF — the track's start (INDEX 00 is its pregap).
            int idx = 0, mm = 0, ss = 0, ff = 0;
            if (std::sscanf(line.c_str(), " INDEX %d %d:%d:%d", &idx, &mm, &ss, &ff) == 4
                && cur_track_num >= 1 && !bin_files.empty()) {
                const uint32_t index_lba = (uint32_t)(((mm * 60 + ss) * 75) + ff);
                if (idx == 0) {
                    cur_index00 = index_lba;
                    cur_has_index00 = true;
                    continue;
                }
                if (idx != 1) continue;
                PendingTrack t;
                t.number     = cur_track_num;
                t.is_audio   = cur_track_audio;
                t.file_index = bin_files.size() - 1;
                t.index01    = index_lba;
                t.index00    = cur_index00;
                t.has_index00 = cur_has_index00;
                pending_tracks.push_back(t);
                cur_track_num = -1;
            }
        }
        cue_file.close();

        // A cue with no FILE line has nothing to mount.
        if (bin_files.empty()) {
            return false;
        }
    } else {
        bin_files.push_back(filename);
    }

    // Build the segment table: open every file and lay it out at the next
    // disc-relative sector. Every file must open — a multi-file dump with a
    // missing track file would otherwise silently read the wrong sectors.
    uint32_t next_lba = 0;
    for (const std::string& bin_name : bin_files) {
        BinSegment seg;
        seg.path = bin_name;
        seg.file.open(bin_name, std::ios::binary);
        if (!seg.file.is_open()) {
            Close();
            return false;
        }
        seg.file.seekg(0, std::ios::end);
        const std::streampos file_size = seg.file.tellg();
        seg.file.clear();
        if (file_size <= 0) {
            Close();
            return false;
        }
        const uint64_t size = static_cast<uint64_t>(file_size);
        seg.raw          = (size % RAW_SECTOR_SIZE) == 0;
        seg.sector_count = static_cast<uint32_t>(size / (seg.raw ? RAW_SECTOR_SIZE
                                                                 : SECTOR_SIZE));
        seg.start_lba    = next_lba;
        next_lba += seg.sector_count;
        segments_.push_back(std::move(seg));
    }

    // Convert the pending cue tracks to disc-relative LBAs.
    for (const PendingTrack& p : pending_tracks) {
        CDTrack t;
        t.number    = p.number;
        t.is_audio  = p.is_audio;
        t.start_lba = segments_[p.file_index].start_lba + p.index01;
        t.pregap_lba = segments_[p.file_index].start_lba +
                       (p.has_index00 ? p.index00 : p.index01);
        tracks_.push_back(t);
    }

    // Synthesize a single data track for a bare .bin/.iso or a .cue with no
    // parseable TRACK entries, so TrackCount() is always >= 1.
    if (tracks_.empty()) {
        CDTrack t;
        t.number = 1; t.is_audio = false; t.start_lba = 0; t.pregap_lba = 0;
        tracks_.push_back(t);
    }

    // Store the resolved data-track path for callers
    bin_path_ = segments_.front().path;

    is_open_ = true;

    // Parse the volume descriptor to extract filesystem metadata when
    // present. Runtime CD-ROM access only needs sector reads, so keep the
    // image mounted even if an ISO9660 header is missing or nonstandard.
    if (!ParseVolumeDescriptor()) {
        volume_id_.clear();
        root_dir_.lba = 0;
        root_dir_.size = 0;
    }

    return true;
}

void ISOReader::Close() {
    for (BinSegment& seg : segments_) {
        if (seg.file.is_open()) {
            seg.file.close();
        }
    }
    segments_.clear();
    tracks_.clear();
    bin_path_.clear();
    volume_id_.clear();
    root_dir_.lba = 0;
    root_dir_.size = 0;
    is_open_ = false;
}

BinSegment* ISOReader::SegmentForLBA(uint32_t lba) {
    // Linear scan: images carry a handful of segments (one per track file).
    for (BinSegment& seg : segments_) {
        if (lba >= seg.start_lba && lba - seg.start_lba < seg.sector_count) {
            return &seg;
        }
    }
    return nullptr;
}

bool ISOReader::ReadSector(uint32_t lba, uint8_t* buffer) {
    if (!is_open_ || !buffer) {
        return false;
    }

    BinSegment* seg = SegmentForLBA(lba);
    if (!seg) {
        return false;
    }
    const uint32_t local_lba = lba - seg->start_lba;

    // Clear any error flags from a previous failed read
    seg->file.clear();

    if (seg->raw) {
        // Raw BIN format - read full sector, extract user data
        std::streampos offset =
            static_cast<std::streampos>(local_lba) * RAW_SECTOR_SIZE + RAW_DATA_OFFSET;
        seg->file.seekg(offset, std::ios::beg);

        if (!seg->file.good()) {
            seg->file.clear();
            return false;
        }

        seg->file.read(reinterpret_cast<char*>(buffer), SECTOR_SIZE);
        std::streamsize bytes_read = seg->file.gcount();
        bool success = (bytes_read == SECTOR_SIZE);
        seg->file.clear();
        return success;
    } else {
        // ISO format - sectors are already 2048 bytes
        std::streampos offset = static_cast<std::streampos>(local_lba) * SECTOR_SIZE;
        seg->file.seekg(offset, std::ios::beg);

        if (!seg->file.good()) {
            seg->file.clear();
            return false;
        }

        seg->file.read(reinterpret_cast<char*>(buffer), SECTOR_SIZE);
        std::streamsize bytes_read = seg->file.gcount();
        bool success = (bytes_read == SECTOR_SIZE);
        seg->file.clear();
        return success;
    }
}

bool ISOReader::ReadRawSector(uint32_t lba, uint8_t* buffer) {
    if (!is_open_ || !buffer) {
        return false;
    }

    BinSegment* seg = SegmentForLBA(lba);
    if (!seg || !seg->raw) {
        return false;
    }
    const uint32_t local_lba = lba - seg->start_lba;

    seg->file.clear();
    std::streampos offset = static_cast<std::streampos>(local_lba) * RAW_SECTOR_SIZE;
    seg->file.seekg(offset, std::ios::beg);
    if (!seg->file.good()) {
        seg->file.clear();
        return false;
    }

    seg->file.read(reinterpret_cast<char*>(buffer), RAW_SECTOR_SIZE);
    std::streamsize bytes_read = seg->file.gcount();
    bool success = (bytes_read == RAW_SECTOR_SIZE);
    seg->file.clear();
    return success;
}

bool ISOReader::IsOpen() const {
    return is_open_;
}

std::string ISOReader::GetVolumeID() const {
    return volume_id_;
}

std::string ISOReader::GetBinPath() const {
    return bin_path_;
}

uint32_t ISOReader::GetSectorCount() {
    if (!is_open_ || segments_.empty()) {
        return 0;
    }

    const BinSegment& last = segments_.back();
    return last.start_lba + last.sector_count;
}

int ISOReader::TrackCount() const {
    return static_cast<int>(tracks_.size());
}

uint32_t ISOReader::TrackStartLBA(int track) const {
    for (const auto& t : tracks_) {
        if (t.number == track) return t.start_lba;
    }
    return 0;
}

uint32_t ISOReader::TrackPregapLBA(int track) const {
    for (const auto& t : tracks_) {
        if (t.number == track) return t.pregap_lba;
    }
    return 0;
}

bool ISOReader::TrackIsAudio(int track) const {
    for (const auto& t : tracks_) {
        if (t.number == track) return t.is_audio;
    }
    return false;
}

RootDirectoryInfo ISOReader::GetRootDirectory() const {
    return root_dir_;
}

uint32_t ISOReader::Read733(const uint8_t* data) const {
    // Read little-endian half of both-endian 32-bit value
    return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

bool ISOReader::ParseVolumeDescriptor() {
    // Read Primary Volume Descriptor from sector 16
    uint8_t pvd[SECTOR_SIZE];
    if (!ReadSector(PVD_SECTOR, pvd)) {
        return false;
    }

    // Verify ISO9660 signature: offset 1 should contain "CD001"
    if (pvd[0] != 0x01 || std::memcmp(&pvd[1], "CD001", 5) != 0) {
        return false;  // Not a valid ISO9660 disc
    }

    // Extract volume ID (offset 40, 32 bytes, space-padded ASCII)
    volume_id_.clear();
    for (int i = 0; i < 32; i++) {
        char c = pvd[40 + i];
        if (c != ' ' && c != '\0') {
            volume_id_ += c;
        }
    }

    // Extract root directory record (offset 156, 34 bytes)
    const uint8_t* root_record = &pvd[156];

    // Root directory LBA is at offset 2 within the directory record (both-endian 32-bit)
    root_dir_.lba = Read733(&root_record[2]);

    // Root directory size is at offset 10 within the directory record (both-endian 32-bit)
    root_dir_.size = Read733(&root_record[10]);

    return true;
}

bool ISOReader::ParseDirectoryRecord(const uint8_t* data, ISOFileEntry& entry) const {
    // Check record length (offset 0)
    uint8_t record_len = data[0];
    if (record_len == 0 || record_len < 33) {
        return false;  // Invalid or padding record
    }

    // Extract LBA (offset 2, both-endian 32-bit)
    entry.lba = Read733(&data[2]);

    // Extract file size (offset 10, both-endian 32-bit)
    entry.size = Read733(&data[10]);

    // Extract file flags (offset 25)
    uint8_t flags = data[25];
    entry.is_directory = (flags & 0x02) != 0;

    // Extract filename length (offset 32)
    uint8_t name_len = data[32];
    if (name_len == 0) {
        return false;  // Invalid record
    }

    // Extract filename (offset 33)
    const char* name_ptr = reinterpret_cast<const char*>(&data[33]);

    // Handle special directory entries
    if (name_len == 1 && name_ptr[0] == '\x00') {
        entry.name = ".";  // Current directory
        return true;
    }
    if (name_len == 1 && name_ptr[0] == '\x01') {
        entry.name = "..";  // Parent directory
        return true;
    }

    // Parse regular filename, strip version suffix (";1")
    entry.name.clear();
    for (uint8_t i = 0; i < name_len; i++) {
        char c = name_ptr[i];
        if (c == ';') {
            break;  // Stop at version separator
        }
        entry.name += c;
    }

    return true;
}

std::vector<ISOFileEntry> ISOReader::ListFilesByLBA(uint32_t lba, uint32_t dir_size) {
    std::vector<ISOFileEntry> results;

    if (!is_open_ || lba == 0 || dir_size == 0) {
        return results;
    }

    // Calculate number of sectors needed for directory data
    uint32_t num_sectors = (dir_size + 2047) / 2048;

    // Allocate buffer for directory data
    std::vector<uint8_t> dir_data(num_sectors * 2048, 0);

    // Read all directory sectors
    for (uint32_t i = 0; i < num_sectors; i++) {
        if (!ReadSector(lba + i, &dir_data[i * 2048])) {
            return results;  // Error reading sector
        }
    }

    // Parse directory records
    uint32_t offset = 0;
    while (offset < dir_size) {
        uint8_t record_len = dir_data[offset];

        if (record_len == 0) {
            // Skip to next sector boundary
            uint32_t sector_offset = offset % 2048;
            if (sector_offset != 0) {
                offset += (2048 - sector_offset);
                continue;
            }
            break;
        }

        if (offset + record_len > dir_data.size()) break;

        ISOFileEntry entry;
        if (ParseDirectoryRecord(&dir_data[offset], entry)) {
            if (entry.name != "." && entry.name != "..") {
                results.push_back(entry);
            }
        }

        offset += record_len;
    }

    return results;
}

std::vector<ISOFileEntry> ISOReader::ListFiles(const std::string& path) {
    std::vector<ISOFileEntry> results;

    // Check if file is open
    if (!is_open_) {
        return results;
    }

    if (path.empty()) {
        // List root directory
        RootDirectoryInfo root = GetRootDirectory();
        return ListFilesByLBA(root.lba, root.size);
    }

    // Non-empty path: navigate to that subdirectory within the root
    // Find the matching directory entry in root
    RootDirectoryInfo root = GetRootDirectory();
    std::vector<ISOFileEntry> root_entries = ListFilesByLBA(root.lba, root.size);

    std::string path_upper = path;
    std::transform(path_upper.begin(), path_upper.end(), path_upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    for (const auto& e : root_entries) {
        if (!e.is_directory) continue;
        std::string name_upper = e.name;
        std::transform(name_upper.begin(), name_upper.end(), name_upper.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (name_upper == path_upper) {
            // Found the subdirectory — list its contents
            return ListFilesByLBA(e.lba, e.size > 0 ? e.size : 2048);
        }
    }

    return results;  // Directory not found
}

bool ISOReader::FindFile(const std::string& path, ISOFileEntry& entry) {
    // Check if file is open
    if (!is_open_) {
        return false;
    }

    // Check if path contains a directory separator
    size_t sep = path.find('/');
    if (sep == std::string::npos) {
        sep = path.find('\\');
    }

    if (sep != std::string::npos) {
        // Subdirectory path: "DIR/FILE" or "DIR\FILE"
        std::string dir_name  = path.substr(0, sep);
        std::string file_name = path.substr(sep + 1);

        // List the subdirectory
        std::vector<ISOFileEntry> sub_files = ListFiles(dir_name);

        std::string file_upper = file_name;
        std::transform(file_upper.begin(), file_upper.end(), file_upper.begin(),
                       [](unsigned char c) { return std::toupper(c); });

        for (const auto& f : sub_files) {
            std::string name_upper = f.name;
            std::transform(name_upper.begin(), name_upper.end(), name_upper.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            if (name_upper == file_upper) {
                entry = f;
                return true;
            }
        }
        return false;
    }

    // Root-level file: search root directory
    std::vector<ISOFileEntry> files = ListFiles("");

    // Search for matching filename (case-insensitive comparison)
    for (const auto& file : files) {
        std::string file_upper = file.name;
        std::string path_upper = path;

        std::transform(file_upper.begin(), file_upper.end(), file_upper.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        std::transform(path_upper.begin(), path_upper.end(), path_upper.begin(),
                       [](unsigned char c) { return std::toupper(c); });

        if (file_upper == path_upper) {
            entry = file;
            return true;
        }
    }

    // File not found
    return false;
}

size_t ISOReader::ReadFile(const std::string& path, uint8_t* buffer, size_t max_size) {
    // Validate buffer pointer
    if (!buffer) {
        return 0;
    }

    // Find the file
    ISOFileEntry entry;
    if (!FindFile(path, entry)) {
        return 0;  // File not found
    }

    // Calculate how many bytes to read (min of file size and max_size)
    size_t bytes_to_read = std::min(static_cast<size_t>(entry.size), max_size);

    // Calculate number of sectors to read
    uint32_t sectors_to_read = (bytes_to_read + SECTOR_SIZE - 1) / SECTOR_SIZE;

    // Read sectors sequentially
    size_t bytes_read = 0;
    for (uint32_t i = 0; i < sectors_to_read; i++) {
        // Calculate how many bytes to read from this sector
        size_t bytes_remaining = bytes_to_read - bytes_read;
        size_t sector_bytes = std::min(bytes_remaining, SECTOR_SIZE);

        // Read sector into temporary buffer
        uint8_t sector_buffer[SECTOR_SIZE];
        if (!ReadSector(entry.lba + i, sector_buffer)) {
            return bytes_read;  // Error - return what we've read so far
        }

        // Copy data to output buffer
        std::memcpy(buffer + bytes_read, sector_buffer, sector_bytes);
        bytes_read += sector_bytes;
    }

    return bytes_read;
}

size_t ISOReader::GetFileSize(const std::string& path) {
    // Find the file
    ISOFileEntry entry;
    if (!FindFile(path, entry)) {
        return 0;  // File not found
    }

    // Return file size
    return entry.size;
}

} // namespace PS1
