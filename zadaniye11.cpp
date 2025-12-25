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
#include <pwd.h>
#include <shadow.h>
#include <grp.h>
#include <dirent.h>
#include <sys/mount.h>
#include <errno.h>

using namespace std;

// Глобальная переменная для отслеживания сигнала SIGHUP
volatile sig_atomic_t sighup_received = 0;

// Структура для хранения информации о пользователе
struct UserInfo {
    string username;
    string uid;
    string gid;
    string home;
    string shell;
};

// Глобальные переменные для VFS
string users_dir;
vector<UserInfo> users_list;

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

// Функция для раскрытия тильды в пути
string expand_tilde(const string& path) {
    if (path.empty() || path[0] != '~') {
        return path;
    }
   
    if (path.length() == 1 || path[1] == '/') {
        // ~ или ~/path
        string home = getenv("HOME");
        return home + path.substr(1);
    } else {
        // ~username/path (упрощенная версия)
        // В реальности нужно парсить /etc/passwd, но для простоты вернем как есть
        return path;
    }
}

// Функция для разбиения строки на аргументы с раскрытием тильды
vector<string> split_args(const string& input) {
    vector<string> args;
    stringstream ss(input);
    string token;
   
    while (ss >> token) {
        // Убираем кавычки если есть
        if (!token.empty() && token.front() == '"' && token.back() == '"') {
            token = token.substr(1, token.size() - 2);
        }
       
        // Раскрываем тильду
        token = expand_tilde(token);
       
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
   
    string device = input.substr(3);
   
    size_t start = device.find_first_not_of(" \t");
    size_t end = device.find_last_not_of(" \t");
    if (start != string::npos && end != string::npos) {
        device = device.substr(start, end - start + 1);
    }
   
    if (device.empty()) {
        cout << "Использование: \\l /dev/sda (или другое устройство)" << endl;
        return;
    }
   
    struct stat st;
    if (stat(device.c_str(), &st) != 0) {
        cout << "Устройство '" << device << "' не найдено" << endl;
        return;
    }
   
    if (!S_ISBLK(st.st_mode)) {
        cout << "'" << device << "' не является блочным устройством" << endl;
        return;
    }
   
    cout << "Информация о разделах на " << device << ":" << endl;
    cout << "==========================================" << endl;
   
    pid_t pid = fork();
    if (pid == 0) {
        execlp("fdisk", "fdisk", "-l", device.c_str(), NULL);
        execlp("parted", "parted", device.c_str(), "print", NULL);
        execlp("lsblk", "lsblk", device.c_str(), NULL);
       
        cerr << "Не удалось получить информацию о разделах" << endl;
        cerr << "Установите fdisk, parted или lsblk" << endl;
        exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
       
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            cout << "Для получения подробной информации могут потребоваться права root" << endl;
           
            cout << "\nБазовая информация из /proc/partitions:" << endl;
           
            ifstream partitions("/proc/partitions");
            if (partitions.is_open()) {
                string line;
                bool found = false;
                while (getline(partitions, line)) {
                    if (line.find(device.substr(5)) != string::npos) {
                        cout << line << endl;
                        found = true;
                    }
                }
                partitions.close();
               
                if (!found) {
                    cout << "Информация о устройстве не найдена в /proc/partitions" << endl;
                }
            }
           
            cout << "\nДополнительная информация:" << endl;
            cout << "Устройство: " << device << endl;
            cout << "Размер блока: " << st.st_blksize << " байт" << endl;
           
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

// ================ ЗАДАНИЕ 11: VFS с информацией о пользователях ================

// Функция для получения списка пользователей из /etc/passwd
vector<UserInfo> get_system_users() {
    vector<UserInfo> users;
   
    setpwent(); // Сброс позиции в файле паролей
   
    struct passwd *pwd;
    while ((pwd = getpwent()) != NULL) {
        // Исключаем системных пользователей без логина
        string shell(pwd->pw_shell);
        if (shell != "/bin/false" && shell != "/usr/sbin/nologin" &&
            shell != "/sbin/nologin") {
            UserInfo user;
            user.username = pwd->pw_name;
            user.uid = to_string(pwd->pw_uid);
            user.gid = to_string(pwd->pw_gid);
            user.home = pwd->pw_dir;
            user.shell = pwd->pw_shell;
            users.push_back(user);
        }
    }
   
    endpwent(); // Закрытие файла паролей
   
    return users;
}

// Функция для создания каталога ~/users
bool create_users_directory() {
    string home_dir = getenv("HOME");
    users_dir = home_dir + "/users";
   
    // Создаем каталог, если его нет
    if (mkdir(users_dir.c_str(), 0755) != 0) {
        if (errno != EEXIST) {
            cerr << "Ошибка при создании каталога " << users_dir << ": "
                 << strerror(errno) << endl;
            return false;
        }
    }
   
    return true;
}

// Функция для создания VFS структуры
void create_vfs_structure() {
    // Получаем список пользователей
    users_list = get_system_users();
   
    // Для каждого пользователя создаем каталог и файлы
    for (const auto& user : users_list) {
        string user_dir = users_dir + "/" + user.username;
       
        // Создаем каталог пользователя
        if (mkdir(user_dir.c_str(), 0755) != 0) {
            if (errno != EEXIST) {
                cerr << "Ошибка при создании каталога " << user_dir << ": "
                     << strerror(errno) << endl;
                continue;
            }
        }
       
        // Создаем файл id
        ofstream id_file(user_dir + "/id");
        if (id_file.is_open()) {
            id_file << user.uid << endl;
            id_file.close();
        }
       
        // Создаем файл home
        ofstream home_file(user_dir + "/home");
        if (home_file.is_open()) {
            home_file << user.home << endl;
            home_file.close();
        }
       
        // Создаем файл shell
        ofstream shell_file(user_dir + "/shell");
        if (shell_file.is_open()) {
            shell_file << user.shell << endl;
            shell_file.close();
        }
    }
   
    cout << "VFS создана в " << users_dir << endl;
    cout << "Пользователей отображено: " << users_list.size() << endl;
}

// Функция для добавления пользователя через adduser
bool add_user_vfs(const string& username) {
    cout << "Добавление пользователя: " << username << endl;
   
    // Вызываем adduser
    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс
        execlp("sudo", "sudo", "adduser", username.c_str(), NULL);
        // Если не сработал, пробуем без sudo
        execlp("adduser", "adduser", username.c_str(), NULL);
        cerr << "Ошибка: не удалось выполнить adduser" << endl;
        exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
       
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            // Обновляем VFS
            create_vfs_structure();
            return true;
        } else {
            cout << "Ошибка при добавлении пользователя" << endl;
            return false;
        }
    }
   
    return false;
}

// Функция для удаления пользователя через userdel
bool remove_user_vfs(const string& username) {
    cout << "Удаление пользователя: " << username << endl;
   
    // Вызываем userdel
    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс
        execlp("sudo", "sudo", "userdel", username.c_str(), NULL);
        // Если не сработал, пробуем без sudo
        execlp("userdel", "userdel", username.c_str(), NULL);
        cerr << "Ошибка: не удалось выполнить userdel" << endl;
        exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
       
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            // Удаляем каталог пользователя
            string user_dir = users_dir + "/" + username;
            if (system(("rm -rf " + user_dir).c_str()) == 0) {
                cout << "Каталог пользователя удален: " << user_dir << endl;
            }
           
            // Обновляем список пользователей
            users_list = get_system_users();
            return true;
        } else {
            cout << "Ошибка при удалении пользователя" << endl;
            return false;
        }
    }
   
    return false;
}

// Функция для проверки команды \adduser
bool is_adduser_command(const string& input) {
    return input.rfind("\\adduser", 0) == 0;
}

// Функция для обработки команды \adduser
void handle_adduser(const string& input) {
    if (input.length() <= 9) { // "\adduser " - 9 символов
        cout << "Использование: \\adduser username" << endl;
        return;
    }
   
    string username = input.substr(9);
   
    // Убираем лишние пробелы
    size_t start = username.find_first_not_of(" \t");
    size_t end = username.find_last_not_of(" \t");
    if (start != string::npos && end != string::npos) {
        username = username.substr(start, end - start + 1);
    }
   
    if (username.empty()) {
        cout << "Использование: \\adduser username" << endl;
        return;
    }
   
    add_user_vfs(username);
}

// Функция для проверки команды \deluser
bool is_deluser_command(const string& input) {
    return input.rfind("\\deluser", 0) == 0;
}

// Функция для обработки команды \deluser
void handle_deluser(const string& input) {
    if (input.length() <= 9) { // "\deluser " - 9 символов
        cout << "Использование: \\deluser username" << endl;
        return;
    }
   
    string username = input.substr(9);
   
    // Убираем лишние пробелы
    size_t start = username.find_first_not_of(" \t");
    size_t end = username.find_last_not_of(" \t");
    if (start != string::npos && end != string::npos) {
        username = username.substr(start, end - start + 1);
    }
   
    if (username.empty()) {
        cout << "Использование: \\deluser username" << endl;
        return;
    }
   
    remove_user_vfs(username);
}

int main() {
    string input;
    vector<string> history;
    string history_file = string(getenv("HOME")) + "/.kubsh_history";
   
    setup_signal_handlers();
    load_history(history, history_file);
   
    // ============ ЗАДАНИЕ 11: Инициализация VFS ============
    cout << "=== Инициализация VFS для задания 11 ===" << endl;
    if (!create_users_directory()) {
        cerr << "Ошибка: не удалось создать каталог ~/users" << endl;
        return 1;
    }
   
    create_vfs_structure();
    cout << "========================================" << endl;
   
    cout << "\n=== Shell с выполнением внешних команд ===" << endl;
    cout << "PID: " << getpid() << endl;
    cout << "Встроенные команды: echo, \\e, \\q, \\l, \\adduser, \\deluser" << endl;
    cout << "  echo <text>         - вывести текст" << endl;
    cout << "  \\e $VAR            - вывести переменную окружения" << endl;
    cout << "  \\l /dev/sda        - информация о разделах диска" << endl;
    cout << "  \\adduser username  - добавить пользователя" << endl;
    cout << "  \\deluser username  - удалить пользователя" << endl;
    cout << "  \\q                 - выход из шелла" << endl;
    cout << "Можно выполнять внешние команды: ls, pwd, cat, и т.д." << endl;
    cout << "Поддерживаются пайпы: command1 | command2" << endl;
    cout << "Сигнал SIGHUP выводит 'Configuration reloaded'" << endl;
    cout << "VFS с пользователями создана в: " << users_dir << endl;
    cout << "Теперь команды с тильдой (например: ls -la ~/users/) должны работать!" << endl;
   
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
       
        // Проверяем встроенные команды в порядке приоритета
        if (is_adduser_command(input)) {
            handle_adduser(input);
        } else if (is_deluser_command(input)) {
            handle_deluser(input);
        } else if (is_l_command(input)) {
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
