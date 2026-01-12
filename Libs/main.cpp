#include <iostream>

#include "DACOM.H"

ICOManager *DACOM;

int main() {
    DACOM = DACOM_Acquire();
    DACOM->SetINIConfig("[Libraries]\r\nDosfile.dll", DACOM_INI_STRING);
    std::cout << "Hello, World!" << std::endl;
    return 0;
}