#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

using namespace std;

// Функция для сохранения истории
void save_history(const vector<string>& history, const string& filename) {
    ofstream file(filename);
    for (const auto& cmd : history) {
        file << cmd << endl;
    }
    file.close();
}

// Функция для обработки команды echo
void handle_echo(const string& input) {
    // Ищем "echo " в начале строки
    if (input.rfind("echo ", 0) == 0) {
        // Выводим всё после "echo "
        string text = input.substr(5);
        cout << text << endl;
    } else if (input == "echo") {
        // Если просто "echo" без аргументов
        cout << endl;
    } else {
        // Если не команда echo, просто выводим строку
        cout << input << endl;
    }
}

int main() {
    string input;
    vector<string> history;
    string history_file = "/home/rina/.kubsh_history";
   
    cout << "Вводите команды. \\q для выхода, Ctrl+D для выхода." << endl;
   
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
       
        // Пропускаем пустые строки
        if (input.empty()) {
            continue;
        }
       
        // Добавляем в историю
        history.push_back(input);
       
        // Обработка команды echo
        if (input.rfind("echo ", 0) == 0 || input == "echo") {
            handle_echo(input);
        } else {
            // Для других команд просто выводим их
            cout << input << endl;
        }
    }
   
    // Сохраняем историю в файл
    save_history(history, history_file);
    cout << "История сохранена в " << history_file << endl;
   
    return 0;
}
