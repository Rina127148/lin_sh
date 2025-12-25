#include <iostream>
#include <string>
#include <vector>
#include <fstream>

using namespace std;

// Функция для сохранения истории в файл
void save_history(const vector<string>& history, const string& filename) {
    ofstream file(filename);
    for (const auto& cmd : history) {
        file << cmd << endl;
    }
    file.close();
}

int main() {
    string input;
    vector<string> history;
    string history_file = "/home/rina/.kubsh_history";  // Полный путь
   
    cout << "Вводите команды. \\q , Ctrl+D для выхода." << endl;
   
    while (true) {
        cout << "> ";
       
        if (!getline(cin, input)) {
            // Ctrl+D
            cout << endl << "Выход по Ctrl+D" << endl;
            break;
        }
       
        // Проверяем команду выхода
        if (input == "\\q") {
            cout << "Выход по команде \\q" << endl;
            break;
        }
       
        // Добавляем в историю (если не пустая строка)
        if (!input.empty()) {
            history.push_back(input);
            cout << input << endl;
        }
    }
   
    // Сохраняем историю в файл
    save_history(history, history_file);
    cout << "История сохранена в " << history_file << endl;
   
    return 0;
}
