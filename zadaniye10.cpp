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
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <linux/hdreg.h>

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
    sa.sa_flags = 0;
   
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
    return input.length() == 4 || input[4] == ' ';
}

// Функция для обработки команды echo
void handle_echo(const string& input) {
    if (input.length() == 4) {
        cout << endl;
    } else {
        string text = input.substr(5);
        cout << text << endl;
    }
}

// Функция для обработки команды \e (вывод переменной окружения)
bool is_env_command(const string& input) {
    return input.rfind("\\e", 0) == 0;
}

void handle_env_var(const string& input) {
    if (input.length() <= 2) {
        cout << "Использование: \\e $VARNAME (например: \\e $PATH)" << endl;
        return;
    }
   
    string var_part = input.substr(3);
   
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

// Функция для проверки команды \l (информация о разделах диска)
bool is_l_command(const string& input) {
    return input.rfind("\\l", 0) == 0;
}

// Функция для обработки команды \l
void handle_l_command(const string& input) {
    if (input.length() <= 2) {
        cout << "Использование: \\l /dev/sda (или другое устройство)" << endl;
        return;
    }
   
    // Получаем аргумент после "\l "
    string device = input.substr(3);
   
    // Убираем лишние пробелы
    size_t start = device.find_first_not_of(" \t");
    size_t end = device.find_last_not_of(" \t");
    if (start != string::npos && end != string::npos) {
        device = device.substr(start, end - start + 1);
    }
   
    if (device.empty()) {
        cout << "Использование: \\l /dev/sda (или другое устройство)" << endl;
        return;
    }
   
    // Проверяем существование устройства
    struct stat st;
    if (stat(device.c_str(), &st) != 0) {
        cout << "Устройство '" << device << "' не найдено" << endl;
        return;
    }
   
    // Проверяем, что это блочное устройство
    if (!S_ISBLK(st.st_mode)) {
        cout << "'" << device << "' не является блочным устройством" << endl;
        return;
    }
   
    cout << "Информация о разделах на " << device << ":" << endl;
    cout << "==========================================" << endl;
   
   
    // 1. Попробуем использовать fdisk -l 
    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс для fdisk
        execlp("fdisk", "fdisk", "-l", device.c_str(), NULL);
       
        // пробуем parted
        execlp("parted", "parted", device.c_str(), "print", NULL);
       
        // пробуем lsblk
        execlp("lsblk", "lsblk", device.c_str(), NULL);
       
        // ничего не работает
        cerr << "Не удалось получить информацию о разделах" << endl;
        cerr << "Установите fdisk, parted или lsblk" << endl;
        exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
       
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            cout << "Для получения подробной информации могут потребоваться права root" << endl;
           
            // чтение из /proc/partitions
            cout << "\nБазовая информация из /proc/partitions:" << endl;
           
            ifstream partitions("/proc/partitions");
            if (partitions.is_open()) {
                string line;
                bool found = false;
                while (getline(partitions, line)) {
                    // Ищем устройство в выводе
                    if (line.find(device.substr(5)) != string::npos) { // Без /dev/
                        cout << line << endl;
                        found = true;
                    }
                }
                partitions.close();
               
                if (!found) {
                    cout << "Информация о устройстве не найдена в /proc/partitions" << endl;
                }
            }
           
            // stat для получения размера
            cout << "\nДополнительная информация:" << endl;
            cout << "Устройство: " << device << endl;
            cout << "Размер блока: " << st.st_blksize << " байт" << endl;
           
            // размер устройства через ioctl
            int fd = open(device.c_str(), O_RDONLY);
            if (fd >= 0) {
                unsigned long long size = 0;
                if (ioctl(fd, BLKGETSIZE64, &size) == 0) {
                    cout << "Общий размер: " << size << " байт ("
                         << size / (1024*1024*1024.0) << " GB)" << endl;
                }
                close(fd);
            }
        }
    } else {
        cout << "Ошибка при создании процесса" << endl;
    }
}

// Функция для выполнения команды с пайпами
void execute_command_with_pipes(const string& input) {
    vector<string> commands;
    stringstream ss(input);
    string command;
   
    while (getline(ss, command, '|')) {
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
   
    int pipefds[2 * (num_commands - 1)];
   
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
            if (i > 0) {
                if (dup2(pipefds[(i-1)*2], STDIN_FILENO) < 0) {
                    cerr << "Ошибка перенаправления ввода" << endl;
                    exit(1);
                }
            }
           
            if (i < num_commands - 1) {
                if (dup2(pipefds[i*2 + 1], STDOUT_FILENO) < 0) {
                    cerr << "Ошибка перенаправления вывода" << endl;
                    exit(1);
                }
            }
           
            for (int j = 0; j < 2 * (num_commands - 1); j++) {
                close(pipefds[j]);
            }
           
            vector<string> args = split_args(commands[i]);
            if (args.empty()) exit(0);
           
            vector<char*> c_args;
            for (size_t j = 0; j < args.size(); j++) {
                c_args.push_back(const_cast<char*>(args[j].c_str()));
            }
            c_args.push_back(nullptr);
           
            execvp(c_args[0], c_args.data());
           
            cerr << "Ошибка: команда '" << args[0] << "' не найдена" << endl;
            exit(1);
        } else if (pid > 0) {
            pids.push_back(pid);
        } else {
            cerr << "Ошибка при создании процесса" << endl;
        }
    }
   
    for (int i = 0; i < 2 * (num_commands - 1); i++) {
        close(pipefds[i]);
    }
   
    for (size_t i = 0; i < pids.size(); i++) {
        int status;
        waitpid(pids[i], &status, 0);
    }
}

int main() {
    string input;
    vector<string> history;
    string history_file = string(getenv("HOME")) + "/.kubsh_history";
   
    setup_signal_handlers();
    load_history(history, history_file);
   
    cout << "=== Shell с выполнением внешних команд ===" << endl;
    cout << "PID: " << getpid() << endl;
    cout << "Встроенные команды: echo, \\e, \\q, \\l" << endl;
    cout << "  echo <text>         - вывести текст" << endl;
    cout << "  \\e $VAR            - вывести переменную окружения" << endl;
    cout << "  \\l /dev/sda        - информация о разделах диска" << endl;
    cout << "  \\q                 - выход из шелла" << endl;
    cout << "Можно выполнять внешние команды: ls, pwd, cat, и т.д." << endl;
    cout << "Поддерживаются пайпы: command1 | command2" << endl;
    cout << "Сигнал SIGHUP выводит 'Configuration reloaded'" << endl;
   
    while (true) {
        if (sighup_received) {
            cout << "Configuration reloaded" << endl;
            sighup_received = 0;
        }
       
        cout << "> ";
       
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
       
        history.push_back(input);
       
        if (is_l_command(input)) {
            handle_l_command(input);
        } else if (is_echo_command(input)) {
            handle_echo(input);
        } else if (is_env_command(input)) {
            handle_env_var(input);
        } else {
            execute_command_with_pipes(input);
        }
    }
   
    save_history(history, history_file);
    cout << "История сохранена в " << history_file << endl;
   
    return 0;
}
