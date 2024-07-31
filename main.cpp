#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>

struct SPData {
  std::string origin;
  std::string transl;
};

void appendNewline(std::string &str) {
  if (!str.empty()) {
    str += "\\n";
  }
}

std::string makeFuzzyString(std::string const &str) {
  std::string res(str);
  // remove double character newlines "\n"
  for (auto const &symbol : {"\\\\n", "\n", "。", "、", "…", "　"}) {
    res = std::regex_replace(res, std::regex(symbol), "");
  }
  return res;
}

void loadSPFile(std::filesystem::path const &sp_file,
                std::vector<SPData> &sp_data) {
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
                                   &newSection]() {
    sp_data.push_back({makeFuzzyString(origin), transl});
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
        newSection = false;
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
  for (auto const &sp_file : std::filesystem::directory_iterator(sp_dir)) {
    loadSPFile(sp_file, sp_data);
  }

  std::string report;
  // read csv files
  for (auto const &work_file : std::filesystem::directory_iterator(work_dir)) {
    if (work_file.path().extension() != ".csv" &&
        work_file.path().extension() != ".CSV") {
      continue;
    }

    std::ifstream csv_file(work_file.path());
    if (!csv_file) {
      std::cout << "Can't open " << work_file.path() << std::endl;
      continue;
    }
    bool modified = false;
    int replaced = 0;
    // skip header
    std::string output;
    std::getline(csv_file, output);
    // parse csv file
    while (!csv_file.eof()) {
      std::string line;
      std::getline(csv_file, line);
      // get the first token
      auto const pos = line.find_first_of(';');
      auto origin = line.substr(0, pos);
      if (auto const match = std::find_if(
              sp_data.begin(), sp_data.end(),
              [origin = makeFuzzyString(origin)](SPData const &sp) {
                return origin == sp.origin;
              });
          match != sp_data.end()) {
        modified = true;
        ++replaced;
        output += "\n" + origin + ";" + match->transl + ";" +
                  "imported from SP" + ";";
      } else {
        output += "\n" + line;
      }
    }
    if (modified) {
      std::ofstream output_file(work_file.path());
      output_file << output;
      auto const report_line = "Matched " + std::to_string(replaced) + " strings in " + work_file.path().string();
      std::cout << report_line << std::endl;
      report += report_line + "\n";
    } else {
      std::cout << "No changes in " << work_file.path() << std::endl;
    }
  }

  if (!report.empty()) {
    std::ofstream report_file("import_report.txt");
    report_file << report;
    std::cout << "Report saved to import_report.txt" << std::endl;
  }

  return 0;
}
