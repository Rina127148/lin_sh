#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>

using namespace std;

// Функция для сохранения истории
void save_history(const vector<string>& history, const string& filename) {
    ofstream file(filename);
    for (const auto& cmd : history) {
        file << cmd << endl;
    }
    file.close();
}

// Функция для проверки существования команды в системе
bool command_exists(const string& command) {
    // Встроенные команды
    if (command == "echo" || command == "\\q") {
        return true;
    }
   
    // Проверяем внешние команды через which
    string test_cmd = "which " + command + " > /dev/null 2>&1";
    return system(test_cmd.c_str()) == 0;
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
       
        // Извлекаем первое слово (команду)
        stringstream ss(input);
        string command;
        ss >> command;
       
        // Проверяем существование команды
        if (command == "echo") {
            handle_echo(input);
        } else if (command_exists(command)) {
            // Команда существует
            cout << "Команда '" << command << "' найдена в системе" << endl;
            // Для внешних команд просто сообщаем о существовании
            // В задании 8 мы будем их выполнять
            cout << input << endl;
        } else {
            // Команда не найдена
            cout << "Ошибка: команда '" << command << "' не найдена" << endl;
        }
    }
   
    // Сохраняем историю в файл
    save_history(history, history_file);
    cout << "История сохранена в " << history_file << endl;
   
    return 0;
}
