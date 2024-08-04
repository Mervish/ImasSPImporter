#include <execution>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>

struct SPData {
  std::string origin;
  std::string transl;
  std::string_view filename;
};

void appendNewline(std::string &str) {
  if (!str.empty()) {
    str += "\\n";
  }
}

std::string makeFuzzyString(std::string const &str) {
  std::string res(str);
  // strip string out of 'problematic' characters
  for (auto const &symbol :
       {"\\\\n", "\n", "。", "、", "…", "　" /*This is a wide whitespace.*/,
        "～", "〜", " ", "「", "」"}) {
    res = std::regex_replace(res, std::regex(symbol), "");
  }
  return res;
}

int levenshteinDist(std::string const &word1, std::string const &word2) {
  int size1 = word1.size();
  int size2 = word2.size();
  int verif[size1 + 1][size2 + 1]; // Verification matrix i.e. 2D array which
                                   // will store the calculated distance.

  // If one of the words has zero length, the distance is equal to the size of
  // the other word.
  if (size1 == 0)
    return size2;
  if (size2 == 0)
    return size1;

  // Sets the first row and the first column of the verification matrix with the
  // numerical order from 0 to the length of each word.
  for (int i = 0; i <= size1; i++)
    verif[i][0] = i;
  for (int j = 0; j <= size2; j++)
    verif[0][j] = j;

  // Verification step / matrix filling.
  for (int i = 1; i <= size1; i++) {
    for (int j = 1; j <= size2; j++) {
      // Sets the modification cost.
      // 0 means no modification (i.e. equal letters) and 1 means that a
      // modification is needed (i.e. unequal letters).
      int cost = (word2[j - 1] == word1[i - 1]) ? 0 : 1;

      // Sets the current position of the matrix as the minimum value between a
      // (deletion), b (insertion) and c (substitution). a = the upper adjacent
      // value plus 1: verif[i - 1][j] + 1 b = the left adjacent value plus 1:
      // verif[i][j - 1] + 1 c = the upper left adjacent value plus the
      // modification cost: verif[i - 1][j - 1] + cost
      verif[i][j] = std::min(std::min(verif[i - 1][j] + 1, verif[i][j - 1] + 1),
                             verif[i - 1][j - 1] + cost);
    }
  }

  // The last position of the matrix will contain the Levenshtein distance.
  return verif[size1][size2];
}

bool compareStrings(std::string const &str1, std::string const &str2) {
  if (std::abs(int(str1.size()) - int(str2.size())) > 5) {
    return false;
  }
  if (str1 == str2) {
    return true;
  }
  auto const total_size = std::max(str1.size(), str2.size());
  auto const projected_diff = total_size / 5;
  auto const diff = levenshteinDist(str1, str2);
  return diff <= projected_diff;
}

void loadSPFile(std::filesystem::path const &sp_file,
                std::vector<SPData> &sp_data, std::string_view filename) {
  // load SP file
  std::ifstream sp_stream;
  sp_stream.open(sp_file);
  if (!sp_stream) {
    std::cout << "Can't open " << sp_file << std::endl;
    return;
  }
  // skip header
  while (!sp_stream.eof()) {
    std::string line;
    std::getline(sp_stream, line);
    if (line.front() == '#') {
      break;
    }
  }
  // parse SP file
  std::string origin;
  std::string transl;
  bool order = false;
  bool newSection = false;
  auto const WriteCollectedData = [&sp_data, &origin, &transl, &order,
                                   &newSection, &filename]() {
    sp_data.push_back({makeFuzzyString(origin), transl, filename});
    origin.clear();
    transl.clear();
    order = false;
    newSection = true;
  };
  while (!sp_stream.eof()) {
    std::string line;
    std::getline(sp_stream, line);
    if (!line.empty()) { // have data
      if (newSection) {
        // find name start
        if (line.substr(0, 3) == "# [") {
          newSection = false;
        }
        continue; // skip character name
      }
      // remove "# " prefix
      if (line.substr(0, 2) == "# ") {
        line = line.substr(2);
      }
      // remove "Choice: " prefix
      if (line.substr(0, 8) == "Choice: ") {
        line = line.substr(8);
        if (!order) {
          WriteCollectedData(); // Choice means new section
          newSection = false;
        }
      }
      if (!order) {
        origin += line;
      } else {
        appendNewline(transl);
        transl += line;
      }
      order = !order;
    } else { // write collected data
      WriteCollectedData();
    }
  }
  if (!origin.empty() || !transl.empty()) {
    WriteCollectedData();
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cout << "Usage: SPImporter <SP files directory> <CSV traslation files "
                 "directory>"
              << std::endl;
    std::cout << "Mervish " << __DATE__ << std::endl;
    return 1;
  }

  auto const sp_dir = std::filesystem::path(argv[1]);
  auto const work_dir = std::filesystem::path(argv[2]);

  std::vector<SPData> sp_data;
  std::vector<std::string> sp_filenames;
  for (auto const &sp_file :
       std::filesystem::recursive_directory_iterator(sp_dir)) {
    if (!sp_file.is_regular_file()) {
      continue;
    }
    sp_filenames.push_back(sp_file.path().filename().string());
    loadSPFile(sp_file, sp_data, sp_filenames.back());
  }

  std::string report;
  std::vector<std::filesystem::path> work_files;
  for (auto const &work_file :
       std::filesystem::recursive_directory_iterator(work_dir)) {
    if (!work_file.is_regular_file()) {
      continue;
    }
    if (work_file.path().extension() != ".csv" &&
        work_file.path().extension() != ".CSV") {
      continue;
    }
    work_files.push_back(work_file.path());
  }
  // read csv files
  std::for_each(std::execution::par, work_files.begin(), work_files.end(), [&sp_data](auto const &work_file) {
    std::string report;
    if (work_file.extension() != ".csv" && work_file.extension() != ".CSV") {
      return;
    }

    std::ifstream csv_file(work_file);
    if (!csv_file) {
      std::cout << "Can't open " << work_file << std::endl;
      return;
    }
    bool modified = false;
    int replaced = 0;
    // skip header
    std::string output;
    std::optional<std::string_view> file_set;
    std::getline(csv_file, output);
    // parse csv file
    while (!csv_file.eof()) {
      std::string line;
      std::getline(csv_file, line);
      if (line.empty()) {
        continue;
      }
      if (line == ";;;") {
        output += "\n" + line;
        continue;
      }
      // get the first token
      auto const pos = line.find_first_of(';');
      auto origin = line.substr(0, pos);
      if (auto const match = std::find_if(
              sp_data.begin(), sp_data.end(),
              [origin = makeFuzzyString(origin), &file_set](SPData const &sp) {
                if(file_set && sp.filename != *file_set) {
                  return false;
                }
                return compareStrings(origin, sp.origin);
              });
          match != sp_data.end()) {
        modified = true;
        file_set = match->filename;
        ++replaced;
        output += "\n" + origin + ";" + match->transl + ";" + "imported from " +
                  match->filename.data() + ";";
      } else {
        output += "\n" + origin + ";" + origin + ";;";
      }
    }
    if (modified) {
      std::ofstream output_file(work_file);
      output_file << output;
      auto const report_line = "Matched " + std::to_string(replaced) +
                               " strings in " + work_file.string();
      std::cout << report_line << std::endl;
      report += report_line + "\n";
    } else {
      std::cout << "No changes in " << work_file << std::endl;
    }
  });
  // for (auto const &work_file :
  //      std::filesystem::recursive_directory_iterator(work_dir)) {
  //   if (!work_file.is_regular_file()) {
  //     continue;
  //   }
  //   if (work_file.path().extension() != ".csv" &&
  //       work_file.path().extension() != ".CSV") {
  //     continue;
  //   }

  //   std::ifstream csv_file(work_file.path());
  //   if (!csv_file) {
  //     std::cout << "Can't open " << work_file.path() << std::endl;
  //     continue;
  //   }
  //   bool modified = false;
  //   int replaced = 0;
  //   // skip header
  //   std::string output;
  //   std::getline(csv_file, output);
  //   // parse csv file
  //   while (!csv_file.eof()) {
  //     std::string line;
  //     std::getline(csv_file, line);
  //     if (line.empty()) {
  //       continue;
  //     }
  //     if (line == ";;;") {
  //       output += line;
  //       continue;
  //     }
  //     // get the first token
  //     auto const pos = line.find_first_of(';');
  //     auto origin = line.substr(0, pos);
  //     if (auto const match = std::find_if(
  //             sp_data.begin(), sp_data.end(),
  //             [origin = makeFuzzyString(origin)](SPData const &sp) {
  //               return compareStrings(origin, sp.origin);
  //             });
  //         match != sp_data.end()) {
  //       modified = true;
  //       ++replaced;
  //       output += "\n" + origin + ";" + match->transl + ";" + "imported from " +
  //                 match->filename.data() + ";";
  //     } else {
  //       output += "\n" + origin + ";" + origin + ";;";
  //     }
  //   }
  //   if (modified) {
  //     std::ofstream output_file(work_file.path());
  //     output_file << output;
  //     auto const report_line = "Matched " + std::to_string(replaced) +
  //                              " strings in " + work_file.path().string();
  //     std::cout << report_line << std::endl;
  //     report += report_line + "\n";
  //   } else {
  //     std::cout << "No changes in " << work_file.path() << std::endl;
  //   }
  // }

  if (!report.empty()) {
    std::ofstream report_file("import_report.txt");
    report_file << report;
    std::cout << "Report saved to import_report.txt" << std::endl;
  }

  return 0;
}
