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
#include <pwd.h>
#include <dirent.h>
#include <errno.h>
#include <thread>
#include <atomic>
#include <algorithm>
#include <deque>
#include <sys/inotify.h>

using namespace std;

volatile sig_atomic_t sighup_received = 0;

struct UserInfo {
    string username;
    string uid;
    string gid;
    string home;
    string shell;
};

string users_dir;
vector<UserInfo> users_list;
deque<string> history;
const int MAX_HISTORY = 100;
string history_file;

atomic<bool> running(true);

// forward declarations
void sync_vfs_with_passwd();
void load_history();
void save_history(const string& cmd);

// ================= Сигналы =================

void sighup_handler(int sig) {
    if (sig == SIGHUP) {
        const char* msg = "Configuration reloaded\n";
        write(STDOUT_FILENO, msg, strlen(msg));
        sighup_received = 1;
        sync_vfs_with_passwd();
    }
}

void setup_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = sighup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, nullptr);
}

// ================= История =================

void load_history() {
    char* home = getenv("HOME");
    if (!home) {
        return;
    }
   
    history_file = string(home) + "/.kubsh_history";
   
    ifstream file(history_file);
    if (file) {
        string line;
        while (getline(file, line) && history.size() < MAX_HISTORY) {
            if (!line.empty() && line != "\\q") {
                history.push_back(line);
            }
        }
    }
}

void save_history(const string& cmd) {
    if (cmd.empty() || cmd == "\\q") return;
   
    history.push_back(cmd);
    if (history.size() > MAX_HISTORY) {
        history.pop_front();
    }
   
    ofstream file(history_file, ios::app);
    if (file) {
        file << cmd << endl;
    }
}

// ================= Утилиты =================

vector<string> split_args(const string& input) {
    vector<string> args;
    istringstream iss(input);
    string token;
    while (iss >> token) args.push_back(token);
    return args;
}

// ================= Встроенные команды =================

void builtin_echo(const vector<string>& args) {
    for (size_t i = 1; i < args.size(); ++i) {
        string s = args[i];
        if (s.size() >= 2 &&
            ((s.front() == '"' && s.back() == '"') ||
             (s.front() == '\'' && s.back() == '\'')))
            s = s.substr(1, s.size() - 2);
        cout << s;
        if (i + 1 < args.size()) cout << " ";
    }
    cout << endl;
}

void builtin_env(const vector<string>& args) {
    if (args.size() < 2) {
        cout << "Usage: \\e $VARIABLE" << endl;
        return;
    }
   
    string var = args[1];
    if (!var.empty() && var[0] == '$') {
        var = var.substr(1);
    }
   
    const char* val = getenv(var.c_str());
    if (!val) {
        cout << "Variable $" << var << " not found" << endl;
        return;
    }

    string v(val);
    if (v.find(':') != string::npos) {
        stringstream ss(v);
        string part;
        while (getline(ss, part, ':'))
            cout << part << endl;
    } else {
        cout << v << endl;
    }
}

void builtin_disk_info(const vector<string>& args) {
    if (args.size() < 2) {
        cout << "Usage: \\l /dev/device" << endl;
        cout << "Example: \\l /dev/sda" << endl;
        return;
    }
   
    string device = args[1];
    string cmd = "lsblk " + device + " 2>/dev/null";
    int result = system(cmd.c_str());
   
    if (result != 0) {
        cmd = "fdisk -l " + device + " 2>/dev/null";
        system(cmd.c_str());
    }
}

bool handle_builtins(const vector<string>& args) {
    if (args.empty()) return true;

    if (args[0] == "echo") {
        builtin_echo(args);
        return true;
    }
    if (args[0] == "\\e") {
        builtin_env(args);
        return true;
    }
    if (args[0] == "\\l") {
        builtin_disk_info(args);
        return true;
    }
    return false;
}

// ================= Выполнение команд =================

void execute_command(const string& input) {
    vector<string> args = split_args(input);
    if (args.empty()) return;

    if (handle_builtins(args))
        return;

    vector<char*> c_args;
    for (auto& a : args)
        c_args.push_back(const_cast<char*>(a.c_str()));
    c_args.push_back(nullptr);

    pid_t pid = fork();
    if (pid == 0) {
        execvp(c_args[0], c_args.data());

        cout << args[0] << ": command not found" << endl;
        exit(127);
    } else if (pid > 0) {
        waitpid(pid, nullptr, 0);
    }
}

// ================= VFS =================

vector<UserInfo> get_system_users() {
    vector<UserInfo> users;
    ifstream f("/etc/passwd");
    string line;

    while (getline(f, line)) {
        stringstream ss(line);
        vector<string> p;
        string part;
        while (getline(ss, part, ':')) p.push_back(part);

        if (p.size() >= 7) {
            string shell = p[6];
            if (shell.size() >= 2 &&
                shell.substr(shell.size() - 2) == "sh") {
                UserInfo u;
                u.username = p[0];
                u.uid = p[2];
                u.gid = p[3];
                u.home = p[5];
                u.shell = shell;
                users.push_back(u);
            }
        }
    }
    return users;
}

bool create_users_directory() {
    char* home = getenv("HOME");
    if (!home) {
        cerr << "ERROR: HOME environment variable not set!" << endl;
        return false;
    }
   
    users_dir = string(home) + "/users";
   
    struct stat st;
    if (stat(users_dir.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
   
    if (mkdir(users_dir.c_str(), 0755) != 0) {
        perror(("Failed to create directory " + users_dir).c_str());
        return false;
    }
    return true;
}

void add_user(const string& name) {
    struct passwd* pw = getpwnam(name.c_str());
    if (pw != nullptr) {
        return;
    }
  
    string cmd = "adduser --disabled-password --gecos '' " + name + " 2>&1";
    system(cmd.c_str());
}

void del_user(const string& name) {
    string cmd = "userdel " + name + " 2>&1";
    system(cmd.c_str());
}

void sync_vfs_with_passwd() {
    vector<UserInfo> sys_users = get_system_users();
    vector<string> vfs_dirs;

    DIR* dir = opendir(users_dir.c_str());
    if (dir) {
        dirent* e;
        while ((e = readdir(dir))) {
            string n = e->d_name;
            if (n != "." && n != "..") vfs_dirs.push_back(n);
        }
        closedir(dir);
    }

    for (auto& d : vfs_dirs) {
        bool found = false;
        for (auto& u : sys_users) {
            if (u.username == d) {
                found = true;
                break;
            }
        }
        if (!found) {
            add_user(d);
        }
    }

    sys_users = get_system_users();

    for (auto& u : sys_users) {
        string ud = users_dir + "/" + u.username;
        mkdir(ud.c_str(), 0755);
        ofstream(ud + "/id") << u.uid;
        ofstream(ud + "/home") << u.home;
        ofstream(ud + "/shell") << u.shell;
    }

    for (auto& u : sys_users) {
        bool found = false;
        for (auto& d : vfs_dirs) {
            if (d == u.username) {
                found = true;
                break;
            }
        }
        if (!found) {
            del_user(u.username);
        }
    }

    users_list = sys_users;
}

void vfs_monitor_loop() {
    while (running) {
        sync_vfs_with_passwd();
        sleep(1);
    }
}

// ================= main =================

int main() {
    setup_signal_handlers();
   
    load_history();
   
    if (!create_users_directory()) {
        cerr << "Failed to create users directory" << endl;
        return 1;
    }
   
    sync_vfs_with_passwd();
   
    thread vfs_thread(vfs_monitor_loop);
   
    string input;
   
    // ВЫВОДИМ ПРИГЛАШЕНИЕ С ">" ПЕРЕД ЦИКЛОМ
    cout << "> ";
    cout.flush();
   
    while (getline(cin, input)) {
        if (input == "\\q") break;
       
        if (sighup_received) {
            sighup_received = 0;
        }
       
        if (!input.empty()) {
            save_history(input);
        }
       
        execute_command(input);
       
        // ВЫВОДИМ ПРИГЛАШЕНИЕ ПОСЛЕ КАЖДОЙ КОМАНДЫ
        cout << "> ";
        cout.flush();
    }
   
    cout << endl << "Exiting kubsh..." << endl;
    running = false;
    vfs_thread.join();
    return 0;
}

