#include <iostream>
#include <string>

using namespace std;

int main() {
    string input;
   
    // Читаем строки в цикле
    while (getline(cin, input)) {
        // Печатаем каждую введённую строку
        cout << input << endl;
    }
   
    // Программа выходит сама при Ctrl+D
    return 0;
}
