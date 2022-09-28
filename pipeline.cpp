#include "read_write_chunk.hpp"
#include <iostream>
//#include "../nest-libs/windows/glm/include/glm/glm.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <fstream>
#include <set>
#include "PlayMode.hpp"
#include "data_path.hpp"
#include <filesystem>

int main(int argc, char** argv) {
    // Array of state names
    std::vector<std::string> state_names = {
        "start",
        "ask_age_kairos"
    };

    // Generate states
    //for (auto const& state_name : state_names) {
    for (const auto& file : std::filesystem::directory_iterator(data_path("states"))) {
        std::string state_name = file.path().filename().string();
        state_name = state_name.substr(0, state_name.size() - 4);
        std::vector<char> string_data;
        std::vector<PlayMode::Line> lines;

        // Read text lines from state file
        std::ifstream ifile(data_path("states/" + state_name + ".txt"), std::ios::binary);
        std::string line_str;
        while (std::getline(ifile, line_str)) {
            // Skip empty lines
            if (line_str.size() <= 1) {
                continue;
            }

            // Break at the footer
            if (line_str[0] == '-') {
                break;
            }

            // Read line into string data and set line to point to it
            PlayMode::Line line;
            line.text_start = string_data.size();
            for (size_t i = 0; i < line_str.size(); i++) {
                string_data.push_back(line_str[i]);
            }
            line.text_end = string_data.size();

            // Detect if the line was spoken
            if (line_str[0] == '*') {
                size_t colon_index = line_str.find(':');
                line.spoken = true;
                line.speaker_start = line.text_start + 1;
                line.speaker_end = line.text_start + colon_index;
                line.text_start += colon_index + 2;
            }

            lines.push_back(line);
        }

        // Read state footer for transitions and conditions
        std::vector<PlayMode::Transition> transitions;
        std::vector<PlayMode::Condition> conditions;
        while (std::getline(ifile, line_str)) {
            if (line_str.size() <= 1) {
                continue;
            }

            // Construct transition
            PlayMode::Transition transition;
            size_t l_index = line_str.find('[');
            size_t r_index = line_str.find("] ");
            if (l_index < r_index && r_index < line_str.size()) {
                transition.trigger_start = string_data.size();
                for (size_t i = l_index + 1; i < r_index; i++) {
                    string_data.push_back(line_str[i]);
                }
                transition.trigger_end = string_data.size();
            } else {
                std::cerr << "State transitions must start with a [name in square brackets] followed by a space" << std::endl;
                exit(1);
            }

            // Helper function to read all conditions between the given string indices and store them in the conditions vector
            auto readConditions = [&](size_t start, size_t end) {
                size_t condition_start_index = start;
                while (condition_start_index < end) {
                    PlayMode::Condition condition;

                    // If condition starts with tilde, negate it
                    if (line_str[condition_start_index] == '~') {
                        condition.negated = true;
                        condition_start_index++;
                    } else {
                        condition.negated = false;
                    }

                    // Read condition name
                    condition.name_start = string_data.size();
                    size_t condition_end_index = std::min(line_str.find(' ', condition_start_index), line_str.size() - 1);
                    for (size_t i = condition_start_index; i < condition_end_index; i ++) {
                        string_data.push_back(line_str[i]);
                    }
                    condition.name_end = string_data.size();

                    // Store condition in conditions vector
                    conditions.push_back(condition);

                    // Prepare to read next condition
                    condition_start_index = condition_end_index + 1;
                }
            };

            // Read pre- and post-conditions for the transition
            size_t arrow_index = line_str.find(" -> ", r_index + 1);
            if (arrow_index < line_str.size()) {
                // Read preconditions
                transition.preconditions_start = conditions.size();
                readConditions(r_index + 2, arrow_index);
                transition.preconditions_end = conditions.size();

                // Read postconditions
                transition.postconditions_start = conditions.size();
                readConditions(arrow_index + 4, line_str.size());
                transition.postconditions_end = conditions.size();
            } else {
                // Read postconditions
                transition.postconditions_start = conditions.size();
                readConditions(r_index + 2, line_str.size());
                transition.postconditions_end = conditions.size();
            }

            // Store transition in transitions vector
            transitions.push_back(transition);
        }

        /*
        for (size_t i = state.lines_start; i < state.lines_end; i++) {
            PlayMode::Line line = lines[i];
            if (line.spoken) {
                std::cout << std::string(string_data.begin() + line.speaker_start, string_data.begin() + line.speaker_end) << ": ";
            }
            std::cout << std::string(string_data.begin() + line.text_start, string_data.begin() + line.text_end) << std::endl << std::endl;
        }
        */

        // Write to file
        std::ofstream ofile(data_path("assets/states/" + state_name), std::ios::binary);
        write_chunk("line", lines, &ofile);
        write_chunk("tran", transitions, &ofile);
        write_chunk("cond", conditions, &ofile);
        write_chunk("strn", string_data, &ofile);
        ofile.close();
    }

	return 0;
}