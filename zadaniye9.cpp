#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include <signal.h>
#include <csignal>
#include <limits> 

using namespace std;

// Глобальная переменная для отслеживания сигнала SIGHUP
volatile sig_atomic_t sighup_received = 0;

// Обработчик сигнала SIGHUP
void sighup_handler(int sig) {
    if (sig == SIGHUP) {
        const char* msg = "\nConfiguration reloaded\n";
        write(STDOUT_FILENO, msg, 24);
        sighup_received = 1;
    }
}

// Функция для установки обработчиков сигналов
void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = sighup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // Не используем SA_RESTART, чтобы getline мог прерываться
   
    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        cerr << "Ошибка при установке обработчика SIGHUP" << endl;
    }
}

// Функция для сохранения истории
void save_history(const vector<string>& history, const string& filename) {
    ofstream file(filename);
    for (const auto& cmd : history) {
        file << cmd << endl;
    }
    file.close();
}

// Функция для загрузки истории
void load_history(vector<string>& history, const string& filename) {
    ifstream file(filename);
    string line;
    while (getline(file, line)) {
        if (!line.empty()) {
            history.push_back(line);
        }
    }
    file.close();
}

// Функция для разбиения строки на аргументы
vector<string> split_args(const string& input) {
    vector<string> args;
    stringstream ss(input);
    string token;
   
    while (ss >> token) {
        // Убираем кавычки если есть
        if (!token.empty() && token.front() == '"' && token.back() == '"') {
            token = token.substr(1, token.size() - 2);
        }
        args.push_back(token);
    }
   
    return args;
}

// Функция для проверки, является ли команда встроенной echo
bool is_echo_command(const string& input) {
    if (input.length() < 4) return false;
    if (input.substr(0, 4) != "echo") return false;
   
    // После "echo" должен быть либо конец строки, либо пробел
    return input.length() == 4 || input[4] == ' ';
}

// Функция для обработки команды echo
void handle_echo(const string& input) {
    if (input.length() == 4) {  // Просто "echo"
        cout << endl;
    } else {
        // Берем текст после "echo " (5 символов)
        string text = input.substr(5);
        cout << text << endl;
    }
}

// Функция для обработки команды \e (вывод переменной окружения)
bool is_env_command(const string& input) {
    return input.rfind("\\e", 0) == 0;
}

void handle_env_var(const string& input) {
    if (input.length() <= 2) {  // Просто "\e"
        cout << "Использование: \\e $VARNAME (например: \\e $PATH)" << endl;
        return;
    }
   
    string var_part = input.substr(3);  // После "\e "
   
    if (!var_part.empty() && var_part[0] == '$') {
        var_part = var_part.substr(1);
    }
   
    const char* value = getenv(var_part.c_str());
   
    if (value != nullptr) {
        string env_value(value);
       
        if (env_value.find(':') != string::npos) {
            istringstream iss(env_value);
            string item;
            while (getline(iss, item, ':')) {
                cout << item << endl;
            }
        } else {
            cout << env_value << endl;
        }
    } else {
        cout << "Переменная окружения '" << var_part << "' не найдена" << endl;
    }
}

// Функция для выполнения команды с пайпами
void execute_command_with_pipes(const string& input) {
    vector<string> commands;
    stringstream ss(input);
    string command;
   
    // Разделяем команды по пайпам
    while (getline(ss, command, '|')) {
        // Убираем лишние пробелы
        size_t start = command.find_first_not_of(" \t");
        size_t end = command.find_last_not_of(" \t");
        if (start != string::npos && end != string::npos) {
            commands.push_back(command.substr(start, end - start + 1));
        } else if (!command.empty()) {
            commands.push_back(command);
        }
    }
   
    if (commands.empty()) return;
   
    int num_commands = commands.size();
    if (num_commands == 1) {
        // Одиночная команда
        vector<string> args = split_args(commands[0]);
        if (args.empty()) return;
       
        vector<char*> c_args;
        for (size_t i = 0; i < args.size(); i++) {
            c_args.push_back(const_cast<char*>(args[i].c_str()));
        }
        c_args.push_back(nullptr);
       
        pid_t pid = fork();
        if (pid == 0) {
            execvp(c_args[0], c_args.data());
            cerr << "Ошибка: команда '" << args[0] << "' не найдена" << endl;
            exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
        }
        return;
    }
   
    // Несколько команд с пайпами
    int pipefds[2 * (num_commands - 1)];
   
    // Создаем пайпы
    for (int i = 0; i < num_commands - 1; i++) {
        if (pipe(pipefds + i * 2) < 0) {
            cerr << "Ошибка при создании пайпа" << endl;
            return;
        }
    }
   
    vector<pid_t> pids;
   
    for (int i = 0; i < num_commands; i++) {
        pid_t pid = fork();
       
        if (pid == 0) {
            // Дочерний процесс
           
            // Если не первая команда - перенаправляем ввод из предыдущего пайпа
            if (i > 0) {
                if (dup2(pipefds[(i-1)*2], STDIN_FILENO) < 0) {
                    cerr << "Ошибка перенаправления ввода" << endl;
                    exit(1);
                }
            }
           
            // Если не последняя команда - перенаправляем вывод в следующий пайп
            if (i < num_commands - 1) {
                if (dup2(pipefds[i*2 + 1], STDOUT_FILENO) < 0) {
                    cerr << "Ошибка перенаправления вывода" << endl;
                    exit(1);
                }
            }
           
            // Закрываем все файловые дескрипторы пайпов
            for (int j = 0; j < 2 * (num_commands - 1); j++) {
                close(pipefds[j]);
            }
           
            // Разбиваем команду на аргументы
            vector<string> args = split_args(commands[i]);
            if (args.empty()) exit(0);
           
            // Подготавливаем аргументы для execvp
            vector<char*> c_args;
            for (size_t j = 0; j < args.size(); j++) {
                c_args.push_back(const_cast<char*>(args[j].c_str()));
            }
            c_args.push_back(nullptr);
           
            // Выполняем команду
            execvp(c_args[0], c_args.data());
           
            // Если execvp вернул ошибку
            cerr << "Ошибка: команда '" << args[0] << "' не найдена" << endl;
            exit(1);
        } else if (pid > 0) {
            pids.push_back(pid);
        } else {
            cerr << "Ошибка при создании процесса" << endl;
        }
    }
   
    // Закрываем все файловые дескрипторы пайпов в родительском процессе
    for (int i = 0; i < 2 * (num_commands - 1); i++) {
        close(pipefds[i]);
    }
   
    // Ждем завершения всех дочерних процессов
    for (size_t i = 0; i < pids.size(); i++) {
        int status;
        waitpid(pids[i], &status, 0);
    }
}

int main() {
    string input;
    vector<string> history;
    string history_file = string(getenv("HOME")) + "/.kubsh_history";
   
    // Устанавливаем обработчики сигналов
    setup_signal_handlers();
   
    // Загружаем историю
    load_history(history, history_file);
   
    cout << "=== Shell с выполнением внешних команд ===" << endl;
    cout << "PID: " << getpid() << endl;  // Выводим PID для удобства тестирования
    cout << "Встроенные команды: echo, \\e, \\q" << endl;
    cout << "Можно выполнять внешние команды: ls, pwd, cat, и т.д." << endl;
    cout << "Поддерживаются пайпы: command1 | command2" << endl;
    cout << "Сигнал SIGHUP выводит 'Configuration reloaded'" << endl;
   
    while (true) {
        // Проверяем, был ли получен сигнал SIGHUP
        if (sighup_received) {
            // Выводим сообщение в стандартный вывод
            cout << "Configuration reloaded" << endl;
            sighup_received = 0;
        }
       
        cout << "> ";
       
        // Используем getline для удобства
        if (!getline(cin, input)) {
            cout << endl << "Выход по Ctrl+D" << endl;
            break;
        }
       
        if (input == "\\q") {
            cout << "Выход по команде \\q" << endl;
            break;
        }
       
        if (input.empty()) {
            continue;
        }
       
        // Добавляем в историю
        history.push_back(input);
       
        // Проверяем встроенные команды
        if (is_echo_command(input)) {
            handle_echo(input);
        } else if (is_env_command(input)) {
            handle_env_var(input);
        } else {
            // Внешняя команда или команда с пайпами
            execute_command_with_pipes(input);
        }
    }
   
    // Сохраняем историю
    save_history(history, history_file);
    cout << "История сохранена в " << history_file << endl;
   
    return 0;
}
