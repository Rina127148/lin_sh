#include <iostream>
#include <string>

using namespace std;

int main() {
    string input;
   
    while (true) {
        // Читаем строку
        if (!getline(cin, input)) {
            // Если Ctrl+D - выходим
            break;
        }
       
        // Проверяем, не команда ли выхода
        if (input == "\\q") {
            break;  // Выход по команде \q
        }
       
        // Печатаем строку
        cout << input << endl;
    }
   
    return 0;
}
