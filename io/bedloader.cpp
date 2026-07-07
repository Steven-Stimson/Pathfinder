// Copyright 2022 Andrey Zakharov

// This file is part of Pathfinder

// Pathfinder is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Pathfinder is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Pathfinder.  If not, see <http://www.gnu.org/licenses/>.

#include "bedloader.h"

#include <csv/csv.hpp>

#include <fstream>
#include <sstream>

namespace bed {

static std::vector<int64_t> parseIntArray(const std::string &intArrayString) {
    std::stringstream ss{intArrayString};
    std::string intString;
    std::vector<int64_t> res;
    while (std::getline(ss, intString, ',')) {
        if (intString.empty()) {
            break;
        }
        res.push_back(std::stoll(intString));
    }
    return res;
}

std::vector<Line> load(const std::filesystem::path &path) {
    // Read file and filter out comment lines (starting with #)
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open BED file: " + path.string());
    }

    std::stringstream filteredContent;
    std::string line;
    while (std::getline(file, line)) {
        // Skip comment lines and empty lines
        if (line.empty() || line[0] == '#') {
            continue;
        }
        filteredContent << line << '\n';
    }

    csv::CSVFormat format;
    format.delimiter('\t')
          .quote('"')
          .no_header();  // Parse TSVs without a header row
    format.column_names({
            "chrom", "chromStart", "chromEnd",
            "name", "score", "strand",
            "thickStart", "thickEnd",
            "itemRgb", "blockCount",
            "blockSizes", "blockStarts"
        });
    csv::CSVReader csvReader(filteredContent, format);

    csv::CSVRow row;
    std::vector<Line> res;
    while (csvReader.read_row(row)) {
        // At least 3 columns are mandatory
        if (row.size() < 3)
            throw std::logic_error("Mandatory columns were not found");

        Line bedLine;
        std::string itemRgbString = "0";
        int64_t blockCount = 0;
        std::string blockSizesString;
        std::string blockStartsString;
        for (size_t i = 0; i < row.size(); ++i) {
            auto cell = row[i];
            switch (i) {
            default:
                break;
            case 0:
                bedLine.chrom = cell.get();
                break;
            case 1:
                bedLine.chromStart = cell.get<int64_t>();
                break;
            case 2:
                bedLine.chromEnd = cell.get<int64_t>();
                break;
            case 3:
                bedLine.name = cell.get();
                break;
            case 4:
                bedLine.score = cell.is_int() ? cell.get<int>() : 0;
                break;
            case 5:
                {
                    auto s = cell.get();
                    bedLine.strand = Strand{s.empty() ? '.' : s[0]};
                }
                break;
            case 6:
                bedLine.thickStart = (cell.get() == "." || cell.get().empty()) ? -1 : cell.get<int64_t>();
                break;
            case 7:
                bedLine.thickEnd = (cell.get() == "." || cell.get().empty()) ? -1 : cell.get<int64_t>();
                break;
            case 8:
                itemRgbString = cell.get();
                break;
            case 9:
                blockCount = cell.get<int64_t>();
                break;
            case 10:
                blockSizesString = cell.get();
                break;
            case 11:
                blockStartsString = cell.get();
                break;
            }
        }

        if (bedLine.thickStart == -1 || bedLine.thickEnd == -1) {
            bedLine.thickStart = bedLine.chromStart;
            bedLine.thickEnd = bedLine.chromEnd;
        }

        {
            std::vector<int64_t> rgbArray;
            if (itemRgbString != "0") {
                rgbArray = parseIntArray(itemRgbString);
            } else {
                rgbArray = { rand() & 0xFF, rand() & 0xFF, rand() & 0xFF };
            }
            bedLine.itemRgb.r = rgbArray[0];
            bedLine.itemRgb.g = rgbArray[1];
            bedLine.itemRgb.b = rgbArray[2];
        }

        if (blockCount != 0) {
            auto blockSizes = parseIntArray(blockSizesString);
            auto blockStarts = parseIntArray(blockStartsString);
            for (int i = 0; i < blockCount; i++) {
                auto blockStart = bedLine.chromStart + blockStarts[i];
                auto blockEnd = blockStart + blockSizes[i];
                bedLine.blocks.push_back(Block{.start = blockStart, .end = blockEnd});
            }
        }
        res.emplace_back(bedLine);
    }

    return res;
}

}
