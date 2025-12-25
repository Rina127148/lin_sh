#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>

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
    if (command == "echo" || command == "\\q" || command == "\\e") {
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

// Функция для обработки команды \e (вывод переменной окружения)
void handle_env_var(const string& input) {
    // Формат: \e $VARNAME или \e VARNAME
    if (input.rfind("\\e ", 0) == 0) {
        string var_part = input.substr(3); // После "\e "
       
        // Убираем $ в начале, если есть
        if (!var_part.empty() && var_part[0] == '$') {
            var_part = var_part.substr(1);
        }
       
        // Получаем значение переменной окружения
        const char* value = getenv(var_part.c_str());
       
        if (value != nullptr) {
            string env_value(value);
           
            // Если переменная содержит ':', выводим каждый элемент с новой строки
            if (env_value.find(':') != string::npos) {
                istringstream iss(env_value);
                string item;
                while (getline(iss, item, ':')) {
                    cout << item << endl;
                }
            } else {
                // Иначе выводим как есть
                cout << env_value << endl;
            }
        } else {
            cout << "Переменная окружения '" << var_part << "' не найдена" << endl;
        }
    } else if (input == "\\e") {
        cout << "Использование: \\e $VARNAME (например: \\e $PATH)" << endl;
    }
}

int main() {
    string input;
    vector<string> history;
    string history_file = "/home/rina/.kubsh_history";
   
    cout << "Вводите команды. \\q для выхода, Ctrl+D для выхода." << endl;
    cout << "Доступные команды:" << endl;
    cout << "  echo <текст>      - выводит текст" << endl;
    cout << "  \\e $<имя_переменной> - выводит переменную окружения" << endl;
    cout << "  \\q               - выход" << endl;
   
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
       
        // Обработка команд
        if (command == "echo") {
            handle_echo(input);
        } else if (command == "\\e") {
            handle_env_var(input);
        } else if (command_exists(command)) {
            // Команда существует
            cout << "Команда '" << command << "' найдена в системе" << endl;
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
