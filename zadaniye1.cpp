#include <iostream>
#include <string>

using namespace std;

int main() {
    string input;
   
    // Читаем строку
    if (!getline(cin, input)) {
        return 0;  // Если ничего не ввели (Ctrl+D сразу)
    }
   
    // Печатаем введённую строку
    cout << input << endl;
   
    // Выходим
    return 0;
}
