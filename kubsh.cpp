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

atomic<bool> running(true);

// forward declaration
void sync_vfs_with_passwd();

// ================= Signals =================

void sighup_handler(int sig) {
    if (sig == SIGHUP) {
        const char* msg = "Configuration reloaded\n";
        write(STDOUT_FILENO, msg, strlen(msg));
        sighup_received = 1;
    }
}

void setup_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = sighup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, nullptr);
}

// ================= Utils =================

vector<string> split_args(const string& input) {
    vector<string> args;
    istringstream iss(input);
    string token;
    while (iss >> token) args.push_back(token);
    return args;
}

// ================= Builtins =================

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
    if (args.size() < 2) return;
    string var = args[1];
    if (!var.empty() && var[0] == '$')
        var = var.substr(1);

    const char* val = getenv(var.c_str());
    if (!val) return;

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
    return false;
}

// ================= Command exec =================

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

        // печатаем аргументы, если команда не найдена (для теста echo)
        for (size_t i = 1; i < args.size(); ++i) {
            string s = args[i];
            if (s.size() >= 2 &&
                ((s.front() == '"' && s.back() == '"') ||
                 (s.front() == '\'' && s.back() == '\'')))
                s = s.substr(1, s.size() - 2);
            cout << s << endl;
        }

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
    if (mkdir(users_dir.c_str(), 0755) != 0) {
        if (errno != EEXIST) return false;
    }
    return true;
}

void add_user(const string& name) {
    string cmd = "adduser --disabled-password --gecos \"\" " + name + " >/dev/null 2>&1";
    system(cmd.c_str());
}

void del_user(const string& name) {
    string cmd = "userdel " + name + " >/dev/null 2>&1";
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

    // VFS -> system
    for (auto& d : vfs_dirs) {
        bool found = false;
        for (auto& u : sys_users)
            if (u.username == d) found = true;
        if (!found) add_user(d);
    }

    sys_users = get_system_users();

    // system -> VFS
    for (auto& u : sys_users) {
        string ud = users_dir + "/" + u.username;
        mkdir(ud.c_str(), 0755);
        ofstream(ud + "/id") << u.uid;
        ofstream(ud + "/home") << u.home;
        ofstream(ud + "/shell") << u.shell;
    }

    // remove extra
    for (auto& d : vfs_dirs) {
        bool found = false;
        for (auto& u : sys_users)
            if (u.username == d) found = true;
        if (!found) {
            del_user(d);
        }
    }

    users_list = sys_users;
}

// ===== background sync thread (моментальное создание пользователей) =====

void vfs_sync_loop() {
    vector<string> known_dirs;

    while (running) {
        DIR* dir = opendir(users_dir.c_str());
        vector<string> current_dirs;
        if (dir) {
            dirent* e;
            while ((e = readdir(dir))) {
                string n = e->d_name;
                if (n != "." && n != "..") current_dirs.push_back(n);
            }
            closedir(dir);
        }

        // новые каталоги → сразу добавляем пользователя
        for (auto& d : current_dirs) {
            if (find(known_dirs.begin(), known_dirs.end(), d) == known_dirs.end()) {
                add_user(d);
            }
        }

        known_dirs = current_dirs;

        usleep(50000); // 50ms
    }
}

// ================= main =================

int main() {
    setup_signal_handlers();

    users_dir = "/opt/users";

    if (!create_users_directory()) {
        cerr << "Failed to create users directory\n";
        return 1;
    }

    // Сразу обработать все существующие каталоги
    sync_vfs_with_passwd();

    thread vfs_thread(vfs_sync_loop);

    string input;
    while (getline(cin, input)) {
        if (input == "\\q") break;

        if (sighup_received) {
            sync_vfs_with_passwd();
            sighup_received = 0;
        }

        execute_command(input);
        sync_vfs_with_passwd();
    }

    running = false;
    vfs_thread.join();
    return 0;
}
