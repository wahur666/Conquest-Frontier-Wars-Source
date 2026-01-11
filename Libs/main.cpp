#include <iostream>
#include <cstdlib>

#include "DACOM.H"

ICOManager *DACOM;

int main() {
    DACOM = DACOM_Acquire();
    std::cout << "Hello, World!" << std::endl;
    system("pause");
    return 0;
}